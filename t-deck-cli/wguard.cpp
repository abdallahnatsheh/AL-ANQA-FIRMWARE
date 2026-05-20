// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "wguard.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "notification_manager.h"
#include <SD.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── Static ISR state ──────────────────────────────────────────────────────────

volatile WgFrame  WGuard::s_ring[WG_RING_SIZE];
volatile uint8_t  WGuard::s_head            = 0;
volatile uint8_t  WGuard::s_tail            = 0;
volatile uint8_t  WGuard::s_bssid[6]        = {};
volatile char     WGuard::s_ssid[33]        = {};
volatile bool     WGuard::s_active          = false;
volatile uint32_t WGuard::s_isrBeacons      = 0;
volatile uint8_t  WGuard::s_bssidSeen[WG_BSSID_MAX][6] = {};
volatile uint8_t  WGuard::s_bssidSeenN      = 0;
volatile uint32_t WGuard::s_bssidFloodStart = 0;
volatile bool     WGuard::s_bssidFloodFired = false;

// ── ISR helpers ───────────────────────────────────────────────────────────────

void IRAM_ATTR WGuard::extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype, char* out) {
    out[0] = '\0';
    uint16_t pos = 24;
    if (subtype == 8 || subtype == 5) pos += 12;  // skip fixed beacon/probe-resp params
    while (pos + 2 <= len) {
        uint8_t tid = d[pos], tlen = d[pos + 1];
        if (tid == 0) {
            uint8_t l = tlen < 32 ? tlen : 32;
            for (uint8_t i = 0; i < l; i++) out[i] = (char)d[pos + 2 + i];
            out[l] = '\0';
            return;
        }
        pos += 2 + tlen;
    }
}

void IRAM_ATTR WGuard::enqueue(uint8_t sub, const uint8_t* src, const uint8_t* dst,
                                const char* ssid, uint8_t eapolMsg, int8_t rssi) {
    uint8_t next = (s_head + 1) % WG_RING_SIZE;
    if (next == s_tail) return;
    WgFrame& slot = (WgFrame&)s_ring[s_head];
    slot.subtype  = sub;
    memcpy(slot.src, src, 6);
    memcpy(slot.dst, dst, 6);
    slot.ssid[0]  = '\0';
    if (ssid) { uint8_t i = 0; while (i < 32 && ssid[i]) { slot.ssid[i] = ssid[i]; i++; } slot.ssid[i] = '\0'; }
    slot.eapolMsg = eapolMsg;
    slot.rssi     = rssi;
    s_head = next;
}

void IRAM_ATTR WGuard::rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (!s_active) return;
    const wifi_promiscuous_pkt_t* ppkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d   = ppkt->payload;
    uint16_t       len = ppkt->rx_ctrl.sig_len;
    int8_t         rssi = ppkt->rx_ctrl.rssi;
    if (len < 10) return;

    uint8_t ftype   = (d[0] >> 2) & 0x03;
    uint8_t subtype = (d[0] >> 4) & 0x0F;

    if (ftype == 0 && len >= 24) {
        const uint8_t* addr1 = d + 4;
        const uint8_t* addr2 = d + 10;
        const uint8_t* addr3 = d + 16;

        if (subtype == 8) {
            s_isrBeacons++;
            // Beacon flood: track unique BSSIDs in 30s window
            uint32_t now = millis();
            if (now - s_bssidFloodStart > 30000) {
                s_bssidFloodStart = now;
                s_bssidSeenN      = 0;
                s_bssidFloodFired = false;
            }
            bool seen = false;
            for (uint8_t i = 0; i < s_bssidSeenN; i++) {
                if (memcmp((const void*)s_bssidSeen[i], addr2, 6) == 0) { seen = true; break; }
            }
            if (!seen && s_bssidSeenN < WG_BSSID_MAX) {
                memcpy((void*)s_bssidSeen[s_bssidSeenN++], addr2, 6);
                if (s_bssidSeenN >= 100 && !s_bssidFloodFired) {
                    s_bssidFloodFired = true;
                    enqueue(0xFE, addr2, addr1, "", 0, rssi);
                }
            }
            // Evil Twin: same SSID, different BSSID
            if (memcmp(addr2, (const void*)s_bssid, 6) != 0) {
                char ssid[33] = {};
                extractSSID(d, len, subtype, ssid);
                if (ssid[0] && strcmp(ssid, (const char*)s_ssid) == 0)
                    enqueue(8, addr2, addr1, ssid, 0, rssi);
            }
            return;
        }

        bool toUs    = memcmp(addr1, (const void*)s_bssid, 6) == 0;
        bool fromUs  = memcmp(addr2, (const void*)s_bssid, 6) == 0;
        bool bssidOk = memcmp(addr3, (const void*)s_bssid, 6) == 0;

        if      (subtype == 12 && (toUs || fromUs || bssidOk)) enqueue(12, addr2, addr1, "", 0, rssi);
        else if (subtype == 11 && (toUs || bssidOk))           enqueue(11, addr2, addr1, "", 0, rssi);
        else if (subtype ==  0 && (toUs || bssidOk))           enqueue(0,  addr2, addr1, "", 0, rssi);
        else if (subtype ==  4) {
            char ssid[33] = {};
            extractSSID(d, len, subtype, ssid);
            if (ssid[0] == '\0' || strcmp(ssid, (const char*)s_ssid) == 0)
                enqueue(4, addr2, addr1, ssid, 0, rssi);
        }

    } else if (ftype == 2 && len >= 28) {
        uint8_t tods   = d[1] & 0x01;
        uint8_t fromds = (d[1] >> 1) & 0x01;
        if (tods && fromds) return;
        int hdrLen = (subtype & 0x08) ? 26 : 24;
        if (len < (uint16_t)(hdrLen + 12)) return;

        const uint8_t* bssidPtr = tods ? (d + 4) : (fromds ? (d + 10) : (d + 16));
        if (memcmp(bssidPtr, (const void*)s_bssid, 6) != 0) return;

        const uint8_t* llc = d + hdrLen;
        if (llc[0] != 0xAA || llc[1] != 0xAA || llc[6] != 0x88 || llc[7] != 0x8E) return;
        const uint8_t* eapol = llc + 8;
        int eapolSpace = len - hdrLen - 8;
        if (eapolSpace < 8 || eapol[1] != 0x03) return;
        if (eapol[4] != 0x02 && eapol[4] != 0x01) return;

        uint8_t kiHi = eapol[5], kiLo = eapol[6];
        bool ack    = (kiLo & 0x80) != 0;
        bool mic    = (kiHi & 0x01) != 0;
        bool secure = (kiHi & 0x02) != 0;
        uint8_t msg = 0;
        if      ( ack && !mic)            msg = 1;
        else if (!ack &&  mic && !secure) msg = 2;
        else if ( ack &&  mic)            msg = 3;
        else if (!ack &&  mic &&  secure) msg = 4;
        if (!msg) return;

        const uint8_t* src = tods ? (d + 10) : (fromds ? (d + 16) : (d + 10));
        enqueue(0xFF, src, d + 4, "", msg, rssi);
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

WGuard::WGuard(DisplayManager& dm, WiFiFunctions& wf) : _dm(dm), _wf(wf) {}

// ── Argument parsing ──────────────────────────────────────────────────────────

void WGuard::start(char* args) {
    if (!args || !*args) {
        _dm.println("Usage: wg <index|bssid> [ch]");
        _dm.printCommandScreen();
        return;
    }
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    uint8_t bssid[6]; int channel = 6; char ssid[33] = {};

    if (!strchr(first, ':')) {
        int idx = atoi(first);
        if (!_wf.isScanDone()) { _dm.println("Run scanwifi first."); _dm.printCommandScreen(); return; }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) {
            _dm.println("Invalid index."); _dm.printCommandScreen(); return;
        }
        _wf.getNetworkSSID(idx, ssid);
    } else {
        if (sscanf(first, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]) != 6) {
            _dm.println("Bad BSSID."); _dm.printCommandScreen(); return;
        }
        if (second) { int ch = atoi(second); if (ch >= 1 && ch <= 13) channel = ch; }
    }
    run(bssid, channel, ssid);
}

// ── Threat detection ──────────────────────────────────────────────────────────

void WGuard::processFrame(const WgFrame& f) {
    uint32_t now = millis();

    switch (f.subtype) {
    case 12: {   // Deauth
        _cntDeauths++;
        WgCounter* ctr = findOrAdd(_deauthCtr, _deauthCtrN, f.src);
        if (ctr && rollCount(ctr, 10000, now) == 5) {
            char msg[44];
            snprintf(msg, sizeof(msg), "DEAUTH storm %02X:%02X:%02X...", f.src[0], f.src[1], f.src[2]);
            addEvent(2, msg);
            if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_ALERT);
        }
        if (now - _deauthBurstStart > 30000) { _deauthBurstStart = now; _deauthBurstCount = 0; }
        _deauthBurstCount++;
        break;
    }
    case 11: {   // Auth
        _cntAuths++;
        if (now - _authFloodStart > 10000) { _authFloodStart = now; _authMacN = 0; }
        bool seen = false;
        for (int i = 0; i < _authMacN; i++) if (memcmp(_authMacs[i], f.src, 6) == 0) { seen = true; break; }
        if (!seen && _authMacN < 32) memcpy(_authMacs[_authMacN++], f.src, 6);
        if (_authMacN == 32) {
            char msg[44]; snprintf(msg, sizeof(msg), "AUTH flood: 32+ unique MACs/10s");
            addEvent(1, msg);
            if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_WARNING);
            _authMacN = 33;  // prevent re-fire until window resets
        }
        break;
    }
    case 4: {    // Probe request
        _cntProbes++;
        if (now - _recentProberWindow > 30000) { _recentProberWindow = now; _recentProberN = 0; }
        bool seen = false;
        for (int i = 0; i < _recentProberN; i++) if (memcmp(_recentProbers[i], f.src, 6) == 0) { seen = true; break; }
        if (!seen && _recentProberN < WG_CTR_MAX) memcpy(_recentProbers[_recentProberN++], f.src, 6);

        WgCounter* ctr = findOrAdd(_probeCtr, _probeCtrN, f.src);
        if (ctr && rollCount(ctr, 5000, now) == 50) {
            char msg[44]; snprintf(msg, sizeof(msg), "PROBE storm %02X:%02X:%02X... 50/5s", f.src[0], f.src[1], f.src[2]);
            addEvent(1, msg);
            if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_WARNING);
        }
        break;
    }
    case 0: {    // Association request — rapid retry = PMKID harvest
        // Real PMKID attack: same MAC sends many assoc requests rapidly.
        // Normal reconnects: 1 assoc, possibly no prior probe — not suspicious.
        WgCounter* ctr = findOrAdd(_deauthCtr, _deauthCtrN, f.src);
        if (ctr && rollCount(ctr, 5000, now) == 5) {
            char msg[44]; snprintf(msg, sizeof(msg), "PMKID harvest? assoc x5 %02X:%02X:%02X", f.src[0], f.src[1], f.src[2]);
            addEvent(1, msg);
            if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_WARNING);
        }
        break;
    }
    case 8: {    // Evil Twin beacon — alert once per unique rogue BSSID
        for (int i = 0; i < _evilTwinN; i++)
            if (memcmp(_evilTwinSeen[i], f.src, 6) == 0) goto evil_done;
        if (_evilTwinN < 8) memcpy(_evilTwinSeen[_evilTwinN++], f.src, 6);
        {
            bool sameOui = (f.src[0] == _bssid[0] && f.src[1] == _bssid[1] && f.src[2] == _bssid[2]);
            char msg[44];
            if (sameOui) {
                // Same vendor — likely mesh/enterprise co-AP, not a real Evil Twin
                snprintf(msg, sizeof(msg), "CO-AP %02X:%02X:%02X:%02X:%02X:%02X (same OUI)",
                         f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                addEvent(0, msg);   // INFO only, no beep
            } else {
                snprintf(msg, sizeof(msg), "EVIL TWIN? %02X:%02X:%02X:%02X:%02X:%02X diff OUI",
                         f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                addEvent(1, msg);   // WARNING, not CRITICAL
                if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_WARNING);
            }
        }
        evil_done:;
        break;
    }
    case 0xFE:   // Beacon flood trigger from ISR
        addEvent(1, "BEACON FLOOD >50 APs/30s");
        NotificationManager::getInstance().notify(NOTIF_WARNING);
        break;
    case 0xFF: { // EAPOL
        _cntEapols++;
        if (_deauthBurstCount >= 3 && (now - _deauthBurstStart) < 30000) {
            char msg[44]; snprintf(msg, sizeof(msg), "HANDSHAKE harvest M%u", f.eapolMsg);
            addEvent(2, msg);
            if (!_bgMode) NotificationManager::getInstance().notify(NOTIF_ALERT);
            _deauthBurstCount = 0;
        }
        break;
    }
    }
}

void WGuard::addEvent(uint8_t sev, const char* msg) {
    WgEvent& e = _events[_evHead];
    e.ts  = millis();
    e.sev = sev;
    strncpy(e.msg, msg, sizeof(e.msg) - 1); e.msg[sizeof(e.msg) - 1] = '\0';
    _evHead = (_evHead + 1) % WG_EVENT_MAX;
    if (_evCount < WG_EVENT_MAX) _evCount++;
    if (sev > _maxSev) _maxSev = sev;
}

static void saveEventsToFile(const WgEvent* events, uint8_t evHead, uint8_t evCount, File& f) {
    for (uint8_t i = 0; i < evCount; i++) {
        uint8_t idx = (uint8_t)((evHead - evCount + i + WG_EVENT_MAX) % WG_EVENT_MAX);
        const WgEvent& e = events[idx];
        f.printf("%u,%s,%s\n",
                 e.ts / 1000,
                 e.sev == 2 ? "CRITICAL" : (e.sev == 1 ? "WARNING" : "INFO"),
                 e.msg);
    }
    f.flush();
}

// ── Counter helpers ───────────────────────────────────────────────────────────

WgCounter* WGuard::findOrAdd(WgCounter* table, uint8_t& cnt, const uint8_t* mac) {
    for (int i = 0; i < cnt; i++) if (memcmp(table[i].mac, mac, 6) == 0) return &table[i];
    if (cnt < WG_CTR_MAX) {
        WgCounter* c = &table[cnt++];
        memcpy(c->mac, mac, 6); c->count = 0; c->winStart = millis();
        return c;
    }
    WgCounter* oldest = &table[0];
    for (int i = 1; i < cnt; i++) if (table[i].winStart < oldest->winStart) oldest = &table[i];
    memcpy(oldest->mac, mac, 6); oldest->count = 0; oldest->winStart = millis();
    return oldest;
}

uint32_t WGuard::rollCount(WgCounter* ctr, uint32_t windowMs, uint32_t now) {
    if (now - ctr->winStart > windowMs) { ctr->count = 0; ctr->winStart = now; }
    return ++ctr->count;
}

// ── Background mode ───────────────────────────────────────────────────────────

void WGuard::beginBackground(char* args) {
    if (_bgMode) stopBackground();

    if (!args || !*args) {
        _dm.println("Usage: wg <index|bssid> [ch] bg");
        _dm.printCommandScreen();
        return;
    }
    char buf[64]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    uint8_t bssid[6]; int channel = 6; char ssid[33] = {};
    if (!strchr(first, ':')) {
        int idx = atoi(first);
        if (!_wf.isScanDone()) { _dm.println("Run scanwifi first."); _dm.printCommandScreen(); return; }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) { _dm.println("Invalid index."); _dm.printCommandScreen(); return; }
        _wf.getNetworkSSID(idx, ssid);
    } else {
        if (sscanf(first, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]) != 6) {
            _dm.println("Bad BSSID."); _dm.printCommandScreen(); return;
        }
        if (second) { int ch = atoi(second); if (ch >= 1 && ch <= 13) channel = ch; }
    }

    memcpy(_bssid, bssid, 6);
    strncpy(_ssid, ssid, 32); _ssid[32] = '\0';
    _channel = channel;
    _cntBeacons = _cntProbes = _cntAuths = _cntDeauths = _cntEapols = 0;
    _maxSev = 0; _evHead = _evCount = 0; _lastBgHead = 0;
    _deauthCtrN = _probeCtrN = 0;
    _authFloodStart = 0; _authMacN = 0;
    _deauthBurstStart = 0; _deauthBurstCount = 0;
    _recentProberN = 0; _recentProberWindow = millis();
    _evilTwinN = 0;
    _popupUntil = 0; _lastBgPoll = 0;
    s_head = s_tail = 0;
    s_isrBeacons = 0;
    s_bssidSeenN = 0; s_bssidFloodStart = millis(); s_bssidFloodFired = false;
    memcpy((void*)s_bssid, bssid, 6);
    strncpy((char*)s_ssid, ssid, 32); ((char*)s_ssid)[32] = '\0';

    WiFi.mode(WIFI_STA);
    delay(100);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);
    s_active = true;
    _bgMode  = true;
    _dm.setWGuardState(true, 0);

    _dm.setTextColor(TFT_GREEN);
    _dm.printText("WGUARD bg: ch");
    char chb[4]; snprintf(chb, sizeof(chb), "%d", channel); _dm.printText(chb);
    _dm.printText(" "); _dm.println(ssid[0] ? ssid : "<?>");
    _dm.setTextColor(0x4208); _dm.println("wg stop  to disable");
    _dm.setTextColor(TFT_WHITE);
    _dm.printCommandScreen();
}

void WGuard::stopBackground() {
    if (!_bgMode) return;
    s_active = false;
    _bgMode  = false;
    _popupUntil = 0;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_STA);
    _dm.setWGuardState(false, 0);
    _dm.setTextColor(TFT_RED); _dm.println("WGUARD stopped.");
    _dm.setTextColor(TFT_WHITE);
    _dm.printCommandScreen();
}

void WGuard::pollBackground() {
    if (!_bgMode) return;
    uint32_t now = millis();

    // Drain ring at most every 200 ms
    if (now - _lastBgPoll >= 200) {
        _lastBgPoll = now;
        while (s_tail != s_head) {
            WgFrame f;
            memcpy(&f, (const void*)&s_ring[s_tail], sizeof(WgFrame));
            s_tail = (s_tail + 1) % WG_RING_SIZE;
            processFrame(f);
        }
        _cntBeacons = s_isrBeacons;

        // New event since last poll? (compare head positions, not capped _evCount)
        if (_evHead != _lastBgHead) {
            _lastBgHead = _evHead;
            // Most recent event
            uint8_t idx = (uint8_t)((_evHead - 1 + WG_EVENT_MAX) % WG_EVENT_MAX);
            const WgEvent& e = _events[idx];

            // Update shield icon colour in status bar
            _dm.setWGuardState(true, _maxSev);

            // Sound — only for sev>0; INFO events (CO-AP etc.) are silent
            if (e.sev > 0)
                NotificationManager::getInstance().notify(e.sev >= 2 ? NOTIF_ALERT : NOTIF_WARNING);

            // Popup bar at bottom of screen (y=222, h=16) — only for sev>0
            if (e.sev > 0) {
                uint16_t bgColor = (e.sev >= 2) ? TFT_RED : 0x9400;
                _dm.fillRect(0, 222, 320, 16, bgColor);
                _dm.setCursor(4, 223);
                _dm.setTextColor(TFT_WHITE);
                _dm.printText("WGUARD: ");
                char trunc[30]; strncpy(trunc, e.msg, 29); trunc[29] = '\0';
                _dm.printText(trunc);
                _popupUntil = now + 4000;
            }
        }
    }

    // Expire popup
    if (_popupUntil && now >= _popupUntil) {
        _popupUntil = 0;
        _dm.printCommandScreen();
    }
}

// ── Shared interactive UI loop ────────────────────────────────────────────────
// Called by both run() and enterView(). WiFi/promiscuous already set up by caller.

void WGuard::runUI() {
    // ── Draw static layout ────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setDefaultTextSize();
    _dm.setCursor(4, outputY);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("WGUARD");
    _dm.setTextColor(0x7BEF);    _dm.printText("] ch");
    char chBuf[4]; snprintf(chBuf, sizeof(chBuf), "%d", _channel);
    _dm.setTextColor(TFT_WHITE); _dm.printText(chBuf); _dm.printText(" ");
    char ssidShort[18]; strncpy(ssidShort, _ssid[0] ? _ssid : "<?>", 17); ssidShort[17] = '\0';
    _dm.setTextColor(TFT_YELLOW); _dm.println(ssidShort);
    _dm.printSeparator();

    int32_t statusY = _dm.getCursorY();
    int32_t statsY  = statusY + LINE_HEIGHT;
    int32_t evSepY  = statsY  + LINE_HEIGHT;
    int32_t evY     = evSepY  + LINE_HEIGHT;

    _dm.setCursor(4, evSepY);
    _dm.setTextColor(0x4208);
    _dm.println("── Events ──────────────────────");

    _dm.setCursor(4, evY + LINE_HEIGHT * 5);
    _dm.setTextColor(0x4208);
    _dm.println("[q]quit  [s]save log");
    _dm.setTextColor(TFT_WHITE);

    uint32_t lastRefresh = 0;

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (k == 's' || k == 'S') {
            // Pause promiscuous for safe SD write (GDMA rule)
            s_active = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            if (sdCardManager.canAccessSD()) {
                sdCardManager.ensureDir("/logs");
                File f = SD.open("/logs/wguard.csv", FILE_APPEND);
                if (f) {
                    char hdr[64]; snprintf(hdr, sizeof(hdr), "# wguard %s ch%d\n", _ssid, _channel);
                    f.print(hdr); f.flush();
                    saveEventsToFile(_events, _evHead, _evCount, f);
                    f.close();
                    addEvent(0, "Log saved to SD");
                } else {
                    addEvent(0, "SD open failed");
                }
            } else {
                addEvent(0, "No SD card");
            }
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            esp_wifi_set_promiscuous(true);
            s_active = true;
        }

        // Drain ring
        while (s_tail != s_head) {
            WgFrame f;
            memcpy(&f, (const void*)&s_ring[s_tail], sizeof(WgFrame));
            s_tail = (s_tail + 1) % WG_RING_SIZE;
            processFrame(f);
        }
        _cntBeacons = s_isrBeacons;

        uint32_t now = millis();
        if (now - lastRefresh >= 500) {
            lastRefresh = now;

            // Status
            _dm.fillRect(4, statusY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, statusY);
            _dm.printText("STATUS: ");
            if      (_maxSev == 2) { _dm.setTextColor(TFT_RED);    _dm.println("!! CRITICAL"); }
            else if (_maxSev == 1) { _dm.setTextColor(TFT_YELLOW); _dm.println("WARNING"); }
            else                   { _dm.setTextColor(TFT_GREEN);  _dm.println("OK"); }
            _dm.setTextColor(TFT_WHITE);

            // Stats
            _dm.fillRect(4, statsY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, statsY);
            _dm.setTextColor(0x7BEF);
            char stat[48];
            snprintf(stat, sizeof(stat), "B:%-4u P:%-3u A:%-3u D:%-3u E:%-3u",
                     _cntBeacons, _cntProbes, _cntAuths, _cntDeauths, _cntEapols);
            _dm.println(stat);

            // Events (5 lines, most recent first)
            _dm.fillRect(4, evY, 312, LINE_HEIGHT * 5, TFT_BLACK);
            uint8_t show = _evCount < 5 ? _evCount : 5;
            for (uint8_t i = 0; i < show; i++) {
                uint8_t idx = (uint8_t)((_evHead - 1 - i + WG_EVENT_MAX) % WG_EVENT_MAX);
                const WgEvent& e = _events[idx];
                _dm.setCursor(4, evY + i * LINE_HEIGHT);
                uint32_t s = e.ts / 1000;
                char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%02u:%02u", (s / 60) % 60, s % 60);
                _dm.setTextColor(0x4208); _dm.printText(tbuf); _dm.printText(" ");
                if      (e.sev == 2) { _dm.setTextColor(TFT_RED);    _dm.printText("!! "); }
                else if (e.sev == 1) { _dm.setTextColor(TFT_YELLOW); _dm.printText("W  "); }
                else                  { _dm.setTextColor(0x7BEF);    _dm.printText("i  "); }
                _dm.setTextColor(TFT_WHITE);
                char trunc[28]; strncpy(trunc, e.msg, 27); trunc[27] = '\0';
                _dm.println(trunc);
            }
        }
    }
}

// ── Enter live view of running bg session ─────────────────────────────────────
// No WiFi setup/teardown — bg promiscuous stays running throughout.

void WGuard::enterView() {
    _bgMode = false;   // disable so processFrame notifies normally during live view
    runUI();
    _bgMode = true;    // restore bg mode
    _lastBgHead = _evHead;  // don't re-alert events we just saw
    _dm.setWGuardState(true, _maxSev);
    _dm.clearScreen();
    _dm.printFirstCommandScreen("");
}

// ── Fresh interactive session ─────────────────────────────────────────────────

void WGuard::run(const uint8_t* bssid, int channel, const char* ssid) {
    if (_bgMode) stopBackground();   // clean slate — stop bg if it was running

    memcpy(_bssid, bssid, 6);
    strncpy(_ssid, ssid, 32); _ssid[32] = '\0';
    _channel = channel;
    _cntBeacons = _cntProbes = _cntAuths = _cntDeauths = _cntEapols = 0;
    _maxSev = 0; _evHead = _evCount = 0;
    _deauthCtrN = _probeCtrN = 0;
    _authFloodStart = 0; _authMacN = 0;
    _deauthBurstStart = 0; _deauthBurstCount = 0;
    _recentProberN = 0; _recentProberWindow = millis();
    _evilTwinN = 0;
    _bgMode = false;   // explicitly — interactive mode, not bg
    s_head = s_tail = 0;
    s_isrBeacons = 0;
    s_bssidSeenN = 0; s_bssidFloodStart = millis(); s_bssidFloodFired = false;
    memcpy((void*)s_bssid, bssid, 6);
    strncpy((char*)s_ssid, ssid, 32); ((char*)s_ssid)[32] = '\0';

    WiFi.mode(WIFI_STA);
    delay(100);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);
    s_active = true;
    _dm.setWGuardState(true, 0);

    runUI();   // blocking

    // ── Cleanup ───────────────────────────────────────────────────────────────
    s_active = false;
    _bgMode  = false;
    _dm.setWGuardState(false, 0);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_STA);
    _dm.clearScreen();
    _dm.printFirstCommandScreen("");
}
