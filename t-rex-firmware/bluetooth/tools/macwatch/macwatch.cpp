// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// macwatch / mw — WiFi probe + BLE MAC watchlist with proximity alert.
// "trackme-lite": reuses trackme's dual-radio loop (continuous NimBLE scan +
// time-gated WiFi probe sniff) but matches each sighting against a user
// watchlist instead of scoring/signatures. Foreground interactive watcher +
// BLE-only background mode.

#include "macwatch.h"
#include <NimBLEDevice.h>
#include "esp_wifi.h"
#include <WiFi.h>
#include <SD.h>
#include "display_manager.h"
#include "sdcard_manager.h"
#include "input_handling.h"
#include "notification_manager.h"
#include "powersave_manager.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include "oui_lookup.h"
#include "ble_ident.h"
#include "wguard.h"
#include "wifi_sd_guard.h"   // ScopedPromiscPause — GDMA-safe SD writes while wg bg holds promiscuous

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;
extern InputHandling  inputHandler;
extern WGuard         wGuard;

// ── tunables ──────────────────────────────────────────────────────────────────
#define MW_MAX               24
#define MW_BLE_RING          24
#define MW_PROBE_RING        16
#define MW_CAND_MAX          32
#define MW_VIS                7          // table rows per page (bmon-style)
#define MW_PRESENCE_TIMEOUT  180000UL    // 3 min — "left" only after this silence
#define MW_RADIO_WIFI        0x01
#define MW_RADIO_BT          0x02
#define MW_NEAR_2M           (-70)       // ~1-2 m calibration ref
#define MW_NEAR_ROOM         (-85)       // ~5-7 m
#define MW_CAND_NEAR         (-60)       // add-mode "near only" cutoff (~arm's length)

// ── watchlist ─────────────────────────────────────────────────────────────────
struct MwEntry {
    uint8_t  mac[6];
    uint8_t  prefixLen;     // 3 (OUI prefix, WiFi) or 6 (full)
    char     name[24];      // user-given person/device name
    uint8_t  radio;         // MW_RADIO_WIFI | MW_RADIO_BT
    int8_t   nearRssi;      // proximity gate: 0 = any range
    bool     present;
    bool     everSeen;
    bool     lastWiFi;      // radio of the last sighting (for display/log)
    uint32_t lastSeenMs;
    float    rssiSmoothed;
    int8_t   lastRssi;
};
static MwEntry s_watch[MW_MAX];
static int     s_watchCount = 0;
static int     s_sel        = 0;
static bool    s_loaded     = false;   // watchlist read from SD once; s_watch is then resident
                                       // (keeps presence state across mw / mw bg re-entry)

// ── BLE scan ring (NimBLE host task → main task) ──────────────────────────────
struct MwBle { uint8_t mac[6]; int8_t rssi; char name[20]; uint16_t companyId; };
static volatile MwBle   s_bleRing[MW_BLE_RING];
static volatile uint8_t s_bleHead = 0, s_bleTail = 0;

class MwBleCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        uint8_t next = (s_bleHead + 1) % MW_BLE_RING;
        if (next == s_bleTail) return;
        volatile MwBle& e = s_bleRing[s_bleHead];
        std::string addr = dev->getAddress().toString();
        sscanf(addr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               (unsigned char*)&e.mac[0], (unsigned char*)&e.mac[1],
               (unsigned char*)&e.mac[2], (unsigned char*)&e.mac[3],
               (unsigned char*)&e.mac[4], (unsigned char*)&e.mac[5]);
        e.rssi = (int8_t)dev->getRSSI();
        ((char*)e.name)[0] = '\0';
        std::string nm = dev->getName();
        if (!nm.empty()) {
            size_t n = min(nm.size(), (size_t)19);
            memcpy((void*)e.name, nm.data(), n);
            ((char*)e.name)[n] = '\0';
        }
        // 16-bit company ID from manufacturer data (names the vendor even for rnd MACs)
        e.companyId = 0;
        if (dev->haveManufacturerData()) {
            std::string m = dev->getManufacturerData();
            if (m.size() >= 2) e.companyId = (uint8_t)m[0] | ((uint16_t)(uint8_t)m[1] << 8);
        }
        s_bleHead = next;
    }
};
static MwBleCb s_bleCb;

// ── WiFi probe ring (promiscuous ISR → main task) ─────────────────────────────
struct MwProbe { uint8_t mac[6]; int8_t rssi; };
static volatile MwProbe s_pring[MW_PROBE_RING];
static volatile uint8_t s_pHead = 0, s_pTail = 0;

static void IRAM_ATTR mwWifiCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (pkt->rx_ctrl.sig_len < 24) return;
    const uint8_t* d = pkt->payload;
    if (((d[0] >> 4) & 0x0F) != 4) return;          // probe requests only
    uint8_t next = (s_pHead + 1) % MW_PROBE_RING;
    if (next == s_pTail) return;
    MwProbe& s = (MwProbe&)s_pring[s_pHead];
    memcpy(s.mac, d + 10, 6);                         // addr2 = source STA
    s.rssi = pkt->rx_ctrl.rssi;
    s_pHead = next;
}

// ── shared runtime state ──────────────────────────────────────────────────────
static const uint8_t MW_CHANS[] = { 1, 6, 11 };
static uint8_t  s_chIdx       = 0;
static char     s_pendingKey  = 0;
static bool     s_logOn       = false;

// background
static bool     s_bgActive    = false;
static uint32_t s_bgLastPoll  = 0;
static uint32_t s_bgPopupUntil = 0;

// foreground popup
static uint32_t s_popupUntil  = 0;

// locked-arrival latch (shared fg/bg; rendered by whichever loop is active)
static bool     s_pendPopup   = false;
static int      s_pendIdx     = 0;

// transient notice (LEFT / toggles)
static uint32_t s_noticeUntil = 0;
static char     s_noticeText[40] = "";
static uint16_t s_noticeCol   = 0xFFFF;

// foreground redraw gating + remove-confirm
static bool     s_dirty       = false;   // a presence transition happened → repaint
static uint32_t s_rmConfirmUntil = 0;     // armed window for a second [r] to confirm

// bring-up diagnostic — proves both radios are feeding the matcher in one session
static uint32_t s_diagBle     = 0;        // BLE ring entries drained
static uint32_t s_diagWifi    = 0;        // WiFi probe frames drained

// deferred event log (flushed only when promiscuous is OFF — GDMA rule)
struct MwEvt { char kind; char name[24]; char mac[18]; int8_t rssi; bool wifi; };
static MwEvt    s_evtQ[8];
static uint8_t  s_evtCount = 0;

// add-mode candidates
struct MwCand {
    uint8_t mac[6]; int8_t rssi; bool isWiFi; char name[20];
    const char* vendor; const char* type; uint16_t companyId;
};
static MwCand s_cand[MW_CAND_MAX];
static int    s_candCount = 0;

// ── helpers ───────────────────────────────────────────────────────────────────
static void mwMacStr(const uint8_t* m, uint8_t len, char* out) {
    if (len > 6) len = 6;
    int p = 0;
    for (uint8_t i = 0; i < len; i++) {
        p += sprintf(out + p, "%02X", m[i]);
        if (i + 1 < len) out[p++] = ':';
    }
    out[p] = '\0';
}

static int mwMatch(const uint8_t* mac, bool isWiFi) {
    uint8_t want = isWiFi ? MW_RADIO_WIFI : MW_RADIO_BT;
    for (int i = 0; i < s_watchCount; i++) {
        if (!(s_watch[i].radio & want)) continue;
        if (memcmp(s_watch[i].mac, mac, s_watch[i].prefixLen) == 0) return i;
    }
    return -1;
}

// Update presence for a matched entry; returns true on an ARRIVE transition.
static bool mwSighting(int idx, int8_t rssi, bool isWiFi) {
    MwEntry& e = s_watch[idx];
    if (!e.everSeen) { e.rssiSmoothed = (float)rssi; e.everSeen = true; }
    else             e.rssiSmoothed = e.rssiSmoothed * 0.7f + (float)rssi * 0.3f;
    e.lastRssi = rssi;
    e.lastWiFi = isWiFi;
    bool near = (e.nearRssi == 0) || (e.rssiSmoothed >= (float)e.nearRssi);
    if (!near) return false;                 // detectable but out of "near" range
    e.lastSeenMs = millis();
    if (!e.present) { e.present = true; return true; }
    return false;
}

static void mwSetNotice(const char* txt, uint16_t col) {
    strncpy(s_noticeText, txt, sizeof(s_noticeText) - 1);
    s_noticeText[sizeof(s_noticeText) - 1] = '\0';
    s_noticeCol   = col;
    s_noticeUntil = millis() + 2500;
}

static void mwQueueEvt(char kind, const MwEntry& e) {
    if (!s_logOn || s_evtCount >= 8) return;
    MwEvt& q = s_evtQ[s_evtCount++];
    q.kind = kind;
    strncpy(q.name, e.name, 23); q.name[23] = '\0';
    mwMacStr(e.mac, e.prefixLen, q.mac);
    q.rssi = (int8_t)e.rssiSmoothed;
    q.wifi = e.lastWiFi;
}

// Flush queued events. macwatch itself never runs promiscuous in bg, BUT wguard's
// background IDS (`wg bg`) keeps promiscuous ON the whole time — and pollMacwatchBg()
// runs right after wGuard.pollBackground() in the same getKeyboardInput() tick. Writing
// SD with that promiscuous DMA live corrupts FatFS (the ~1h "T-Deck rebooted" crash with
// gps+wg bg+mw bg). ScopedPromiscPause reads the *current* promiscuous state and pauses
// whoever owns it (wguard) for the write, restoring it after — a no-op when promiscuous
// is already off, so it's correct in every path (fg sniff, bg, standalone).
static void mwFlushEvts() {
    if (s_evtCount == 0) return;
    if (!sdCardManager.isReady()) { s_evtCount = 0; return; }
    ScopedPromiscPause _;          // pause wguard's (or any) promiscuous for the SD write
    sdCardManager.ensureDir(SD_DIR_MACWATCH);
    for (uint8_t i = 0; i < s_evtCount; i++) {
        MwEvt& q = s_evtQ[i];
        char ts[22] = "";
        ClockManager::instance().getTimestamp(ts, sizeof(ts));
        char line[96];   // columns: time,event,name,mac,radio,rssi
        snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%d",
                 ts[0] ? ts : "@", q.kind == 'A' ? "ARRIVE" : "LEAVE",
                 q.name, q.mac, q.wifi ? "WIFI" : "BT", (int)q.rssi);
        sdCardManager.appendLine(SD_LOG_MACWATCH_EVT, String(line));
    }
    s_evtCount = 0;
}

// ── popups / alert ────────────────────────────────────────────────────────────
static void mwCenterPopup(const MwEntry& e) {
    int w = 240, h = 54, x = (SCREEN_WIDTH - w) / 2, y = 90;
    displayManager.fillRect(x - 2, y - 2, w + 4, h + 4, TFT_GREEN);   // border
    displayManager.fillRect(x, y, w, h, 0x000C);                      // dark navy fill
    displayManager.setCursor(x + 10, y + 8);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.printText(">> ARRIVED <<");
    displayManager.setCursor(x + 10, y + 24);
    displayManager.setTextColor(TFT_WHITE);
    char nm[24]; snprintf(nm, sizeof(nm), "%.22s", e.name);
    displayManager.printText(nm);
    char sub[32];
    snprintf(sub, sizeof(sub), "%d dBm   %s", (int)e.rssiSmoothed, e.lastWiFi ? "WiFi" : "BLE");
    displayManager.setCursor(x + 10, y + 38);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.printText(sub);
    s_popupUntil = millis() + 4000;
}

static void mwBgPopup(const MwEntry& e) {
    displayManager.fillRect(0, 222, SCREEN_WIDTH, 16, 0x0240);   // dark green bar
    displayManager.setCursor(4, 223);
    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("[MW] ");
    displayManager.setTextColor(TFT_WHITE);
    char p[44];
    snprintf(p, sizeof(p), "%.18s  %d dBm %s", e.name, (int)e.rssiSmoothed,
             e.lastWiFi ? "W" : "B");
    displayManager.printText(p);
    s_bgPopupUntil = millis() + 4000;
}

static void mwOnArrive(int idx, bool bg) {
    MwEntry& e = s_watch[idx];
    s_dirty = true;
    NotificationManager::getInstance().notify(NOTIF_ALERT);   // also fires wake callback
    PowerSaveManager::getInstance().forceWake();              // ensure screen on
    mwQueueEvt('A', e);
    if (displayManager.isBlocked()) {                         // locked: beep only, latch popup
        s_pendPopup = true; s_pendIdx = idx;
        return;
    }
    if (bg) mwBgPopup(e);
    else    mwCenterPopup(e);
}

static void mwExpire() {
    uint32_t now = millis();
    for (int i = 0; i < s_watchCount; i++) {
        MwEntry& e = s_watch[i];
        if (e.present && (now - e.lastSeenMs > MW_PRESENCE_TIMEOUT)) {
            e.present = false;
            s_dirty = true;
            mwQueueEvt('L', e);
            char n[40]; snprintf(n, sizeof(n), "LEFT: %.30s", e.name);
            mwSetNotice(n, TFT_ORANGE);
        }
    }
}

// ── BLE scan control ──────────────────────────────────────────────────────────
static void mwBleStart() {
    NimBLEDevice::init("");                 // idempotent — no-op if already up
    s_bleHead = s_bleTail = 0;
    NimBLEScan* p = NimBLEDevice::getScan();
    p->setScanCallbacks(nullptr);           // clear stale cb from another BLE cmd
    p->setScanCallbacks(&s_bleCb, true);    // report duplicates → repeat sightings
    p->setActiveScan(false);                // passive — presence only, lower power
    p->setInterval(160);
    p->setWindow(144);
    p->start(0, false);                     // continuous, non-blocking
    displayManager.setBtActive(true);
}

static void mwBleStop() {
    NimBLEScan* p = NimBLEDevice::getScan();
    p->stop();
    p->setScanCallbacks(nullptr);
    p->clearResults();
    displayManager.setBtActive(false);
}

// ── WiFi probe sniff window (foreground only) ─────────────────────────────────
static void mwCandAdd(const uint8_t* mac, int8_t rssi, bool isWiFi, const char* name,
                      uint16_t companyId);

// collect=false → match watchlist + alert; collect=true → gather add-mode cands.
// huntMac set → only update *huntSm/*huntSeen for that MAC (no matching/alerts).
static void mwWifiSniff(uint32_t durMs, bool collect,
                        const uint8_t* huntMac = nullptr,
                        float* huntSm = nullptr, uint32_t* huntSeen = nullptr) {
    if (wGuard.isBackground()) return;       // don't steal wguard's promiscuous cb
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false);
        delay(50);
    }
    wifi_promiscuous_filter_t flt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(mwWifiCb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(MW_CHANS[s_chIdx], WIFI_SECOND_CHAN_NONE);
    s_chIdx = (s_chIdx + 1) % (sizeof(MW_CHANS));

    uint32_t t0 = millis();
    while (millis() - t0 < durMs) {
        while (s_pTail != s_pHead) {
            MwProbe pe;
            memcpy(&pe, (const void*)&s_pring[s_pTail], sizeof(pe));
            s_pTail = (s_pTail + 1) % MW_PROBE_RING;
            s_diagWifi++;
            if (huntMac) {
                if (memcmp(pe.mac, huntMac, 6) == 0 && huntSm && huntSeen) {
                    *huntSm = (*huntSm) * 0.6f + (float)pe.rssi * 0.4f;
                    *huntSeen = millis();
                }
            } else if (collect) {
                mwCandAdd(pe.mac, pe.rssi, true, "", 0);   // WiFi probe: no company ID
            } else {
                int idx = mwMatch(pe.mac, true);
                if (idx >= 0 && mwSighting(idx, pe.rssi, true)) mwOnArrive(idx, false);
            }
        }
        char k = inputHandler.getKeyboardInput();
        if (k) { s_pendingKey = k; break; }
        delay(10);
    }
    esp_wifi_set_promiscuous(false);
}

// ── watchlist load / save ─────────────────────────────────────────────────────
static void mwLoadWatchlist() {
    s_watchCount = 0;
    if (!sdCardManager.isReady() || !SD.exists(SD_LOG_MACWATCH_LIST)) return;
    ScopedPromiscPause _;          // startMacwatchBg()/mwEnsureLoaded() may run while wg bg promiscuous is live
    File f = SD.open(SD_LOG_MACWATCH_LIST, FILE_READ);
    if (!f) return;
    while (f.available() && s_watchCount < MW_MAX) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() < 2 || line[0] == '#') continue;
        String col[4]; int nc = 0, start = 0;
        for (int i = 0; i <= (int)line.length() && nc < 4; i++) {
            if (i == (int)line.length() || line[i] == ',') {
                col[nc++] = line.substring(start, i); start = i + 1;
            }
        }
        for (int i = 0; i < nc; i++) col[i].trim();
        if (nc < 1 || col[0].length() < 2) continue;
        uint8_t mac[6] = {0};
        int got = sscanf(col[0].c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                         &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        if (got != 6 && got != 3) continue;
        MwEntry& e = s_watch[s_watchCount];
        memset(&e, 0, sizeof(e));
        memcpy(e.mac, mac, 6);
        e.prefixLen = (got == 3) ? 3 : 6;
        if (nc >= 2 && col[1].length()) { strncpy(e.name, col[1].c_str(), 23); e.name[23] = '\0'; }
        else snprintf(e.name, sizeof(e.name), "%s", col[0].c_str());
        e.radio = MW_RADIO_WIFI | MW_RADIO_BT;
        if (nc >= 3) {
            if      (col[2].equalsIgnoreCase("WIFI")) e.radio = MW_RADIO_WIFI;
            else if (col[2].equalsIgnoreCase("BT"))   e.radio = MW_RADIO_BT;
        }
        e.nearRssi = (nc >= 4) ? (int8_t)col[3].toInt() : 0;
        s_watchCount++;
    }
    f.close();
}

// Load from SD only the first time; afterwards s_watch[] stays resident so that
// presence/last-seen survive command re-entry (no spurious re-alerts). Foreground
// edits mutate s_watch + persist to SD, so a reload is never needed.
static void mwEnsureLoaded() {
    if (s_loaded) return;
    mwLoadWatchlist();
    s_loaded = true;
}

static bool mwSaveWatchlist() {
    if (!sdCardManager.isReady()) return false;
    ScopedPromiscPause _;          // [a]/[r] may save while wg bg holds promiscuous (or fg sniff returned early)
    sdCardManager.ensureDir(SD_DIR_MACWATCH);
    File f = SD.open(SD_LOG_MACWATCH_LIST, FILE_WRITE);   // truncate + rewrite
    if (!f) return false;
    f.println("# macwatch: mac_or_prefix,name,radio(WIFI|BT|BOTH),nearRssi(0=any)");
    for (int i = 0; i < s_watchCount; i++) {
        MwEntry& e = s_watch[i];
        char macbuf[18]; mwMacStr(e.mac, e.prefixLen, macbuf);
        const char* r = (e.radio == MW_RADIO_WIFI) ? "WIFI"
                      : (e.radio == MW_RADIO_BT)   ? "BT" : "BOTH";
        f.printf("%s,%s,%s,%d\n", macbuf, e.name, r, (int)e.nearRssi);
    }
    f.close();
    return true;
}

static int mwAddOrUpdate(const uint8_t* mac, uint8_t prefixLen, const char* name,
                         uint8_t radio, int8_t nearRssi) {
    for (int i = 0; i < s_watchCount; i++) {
        if (s_watch[i].prefixLen == prefixLen &&
            memcmp(s_watch[i].mac, mac, prefixLen) == 0) {
            strncpy(s_watch[i].name, name, 23); s_watch[i].name[23] = '\0';
            s_watch[i].radio = radio; s_watch[i].nearRssi = nearRssi;
            return i;
        }
    }
    if (s_watchCount >= MW_MAX) return -1;
    MwEntry& e = s_watch[s_watchCount];
    memset(&e, 0, sizeof(e));
    memcpy(e.mac, mac, 6);
    e.prefixLen = prefixLen;
    strncpy(e.name, name, 23); e.name[23] = '\0';
    e.radio = radio; e.nearRssi = nearRssi;
    return s_watchCount++;
}

// ── add-mode candidate collection ─────────────────────────────────────────────
static void mwCandAdd(const uint8_t* mac, int8_t rssi, bool isWiFi, const char* name,
                      uint16_t companyId) {
    for (int i = 0; i < s_candCount; i++) {
        if (memcmp(s_cand[i].mac, mac, 6) == 0) {
            s_cand[i].rssi = rssi;
            if (name && name[0] && !s_cand[i].name[0]) {
                strncpy(s_cand[i].name, name, 19); s_cand[i].name[19] = '\0';
            }
            if (companyId && !s_cand[i].companyId) s_cand[i].companyId = companyId;
            return;
        }
    }
    if (s_candCount >= MW_CAND_MAX) return;
    MwCand& c = s_cand[s_candCount++];
    memcpy(c.mac, mac, 6);
    c.rssi = rssi; c.isWiFi = isWiFi; c.name[0] = '\0'; c.companyId = companyId;
    if (name && name[0]) { strncpy(c.name, name, 19); c.name[19] = '\0'; }
    OuiInfo oi = ouiLookup(mac);
    c.vendor = oi.vendor; c.type = oi.type;
}

// Strongest signal first — the device in the user's hand floats to the top.
static void mwSortCandByRssi() {
    for (int i = 1; i < s_candCount; i++) {
        MwCand key = s_cand[i];
        int j = i - 1;
        while (j >= 0 && s_cand[j].rssi < key.rssi) { s_cand[j + 1] = s_cand[j]; j--; }
        s_cand[j + 1] = key;
    }
}

// Drop everything weaker than thr → only devices held close to the T-Deck remain.
static void mwFilterNear(int8_t thr) {
    int w = 0;
    for (int i = 0; i < s_candCount; i++)
        if (s_cand[i].rssi >= thr) s_cand[w++] = s_cand[i];
    s_candCount = w;
}

// ── inline text prompt (lock-aware) ───────────────────────────────────────────
// T-Deck keyboard has no Esc — Enter saves, trackball CLICK cancels.
// Returns false if cancelled. Buffer may be left empty (caller falls back).
static bool mwPromptText(const char* title, char* out, size_t cap) {
    size_t len = 0; out[0] = '\0';
    bool redraw = true;
    while (true) {
        if (redraw && !displayManager.isBlocked()) {
            displayManager.clearScreen();
            displayManager.updateStatusBar();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.printText(title);
            displayManager.setCursor(4, outputY + LINE_HEIGHT * 2);
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printText("> ");
            displayManager.printText(out);
            displayManager.printText("_");
            displayManager.setCursor(4, outputY + LINE_HEIGHT * 4);
            displayManager.setTextColor(TFT_DARKGREY);
            displayManager.printText("Enter = save    trackball click = cancel");
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_CLICK) return false;                     // cancel (no Esc key)
        char k = inputHandler.getKeyboardInput();
        if (k) {
            if (k == '\r' || k == '\n') return true;
            if (k == '\b') { if (len) { out[--len] = '\0'; redraw = true; } }
            else if (k >= 32 && k < 127 && len < cap - 1) {
                out[len++] = k; out[len] = '\0'; redraw = true;
            }
        }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Single-key choice prompt. Returns the lowercased key pressed.
static char mwPromptChoice(const char* l1, const char* l2, const char* l3) {
    bool redraw = true;
    while (true) {
        if (redraw && !displayManager.isBlocked()) {
            displayManager.clearScreen();
            displayManager.updateStatusBar();
            displayManager.setDefaultTextSize();
            int y = outputY;
            displayManager.setTextColor(TFT_CYAN);
            displayManager.setCursor(4, y); displayManager.printText(l1); y += LINE_HEIGHT * 2;
            displayManager.setTextColor(TFT_WHITE);
            displayManager.setCursor(4, y); displayManager.printText(l2); y += LINE_HEIGHT;
            if (l3) { displayManager.setCursor(4, y); displayManager.printText(l3); }
            redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        if (k) return (k >= 'A' && k <= 'Z') ? (k + 32) : k;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── shared table layout (bmon-style) ──────────────────────────────────────────
#define MW_RY(n)   (outputY + (n) * LINE_HEIGHT)
// candidate-picker columns
#define CC_SEL     4
#define CC_NAME   16
#define CC_RSSI  112
#define CC_RAD   142
#define CC_MAC   166

static uint16_t mwRssiColor(int8_t r) {
    if (r >= -60) return TFT_GREEN;
    if (r >= -78) return TFT_YELLOW;
    return TFT_ORANGE;
}

// Device picker — proper table: header / columns / separator / 7 highlighted rows.
// Selection is by trackball (sel = page-local index 0..PER-1).
static void mwDrawCandTable(int page, int sel) {
    if (displayManager.isBlocked()) return;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    int total  = max(1, (s_candCount + MW_VIS - 1) / MW_VIS);
    int start  = page * MW_VIS;
    int end    = min(start + MW_VIS, s_candCount);

    // row 0 — header
    dm.setCursor(CC_SEL, MW_RY(0));
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("MW");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("ADD");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    char hbuf[24]; snprintf(hbuf, sizeof(hbuf), "%d found  %d/%d", s_candCount, page + 1, total);
    dm.setTextColor(TFT_WHITE);  dm.printText(hbuf);

    // row 1 — column headers
    dm.setTextColor(0x7BEF);
    dm.setCursor(CC_NAME, MW_RY(1)); dm.printText("NAME / VENDOR");
    dm.setCursor(CC_RSSI, MW_RY(1)); dm.printText("RSSI");
    dm.setCursor(CC_RAD,  MW_RY(1)); dm.printText("R");
    dm.setCursor(CC_MAC,  MW_RY(1)); dm.printText("MAC");

    // row 2 — separator
    dm.setCursor(CC_SEL, MW_RY(2)); dm.printSeparator();

    // rows 3-9 — devices
    for (int si = start; si < end; si++) {
        int16_t ry = MW_RY(3 + (si - start));
        MwCand& c = s_cand[si];
        bool seld = ((si - start) == sel);
        if (seld) dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);

        dm.setCursor(CC_SEL, ry);
        dm.setTextColor(TFT_CYAN); dm.printText(seld ? ">" : " ");

        const char* co  = bleCompanyName(c.companyId);
        const char* lbl = c.name[0] ? c.name : (co ? co : (c.vendor ? c.vendor : "(unknown)"));
        char nm[17]; snprintf(nm, sizeof(nm), "%-15.15s", lbl);
        dm.setCursor(CC_NAME, ry);
        dm.setTextColor(seld ? TFT_WHITE : 0xC618); dm.printText(nm);

        char rs[6]; snprintf(rs, sizeof(rs), "%4d", c.rssi);
        dm.setCursor(CC_RSSI, ry);
        dm.setTextColor(mwRssiColor(c.rssi)); dm.printText(rs);

        dm.setCursor(CC_RAD, ry);
        dm.setTextColor(c.isWiFi ? TFT_CYAN : 0x9CF3);
        dm.printText(c.isWiFi ? "Wi" : "BLE");

        char macbuf[18]; mwMacStr(c.mac, 6, macbuf);
        dm.setCursor(CC_MAC, ry);
        dm.setTextColor(seld ? TFT_YELLOW : 0x7BEF); dm.printText(macbuf);
    }

    // row 10 — separator + footer
    dm.setCursor(CC_SEL, MW_RY(10)); dm.printSeparator();
    dm.setCursor(CC_SEL, MW_RY(11));
    dm.setTextColor(TFT_DARKGREY);
    dm.printText("click=pick [h]hunt [n]near [u]rescan [q]cancel");
}

// One blocking scan burst → fresh candidate set, sorted strongest-first.
// Blocking is fine: it only runs on an explicit action (open add / press [u]).
static void mwGatherCandidates(uint32_t durMs) {
    s_candCount = 0;
    if (!displayManager.isBlocked()) {
        displayManager.clearScreen();
        displayManager.updateStatusBar();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.printText("Scanning nearby devices...");
        displayManager.setCursor(4, outputY + LINE_HEIGHT * 2);
        displayManager.setTextColor(TFT_DARKGREY);
        displayManager.printText("hold the device you want close by");
    }
    uint32_t t0 = millis();
    while (millis() - t0 < durMs) {
        while (s_bleTail != s_bleHead) {
            MwBle e; memcpy(&e, (const void*)&s_bleRing[s_bleTail], sizeof(e));
            s_bleTail = (s_bleTail + 1) % MW_BLE_RING;
            mwCandAdd(e.mac, e.rssi, false, e.name, e.companyId);
        }
        mwWifiSniff(250, true);                 // gather WiFi probers
        s_pendingKey = 0;                        // swallow keys pressed during gather
    }
    mwSortCandByRssi();
}

// ── proximity "hunt" meter (silent) — confirm which device is yours ───────────
// Live RSSI bar that grows as the device gets closer; move it to find the spike.
static void mwDrawHunt(const char* label, float sm, uint32_t ageMs) {
    if (displayManager.isBlocked()) return;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    // header
    dm.setCursor(CC_SEL, MW_RY(0));
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("MW");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("HUNT");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    char hb[22]; snprintf(hb, sizeof(hb), "%.16s", label);
    dm.setTextColor(TFT_WHITE);  dm.printText(hb);

    bool stale = ageMs > 4000;
    int  rssi  = (int)sm;
    uint16_t col = stale       ? 0x7BEF
                 : (rssi >= -45) ? TFT_GREEN
                 : (rssi >= -60) ? 0x2FE0      // light green
                 : (rssi >= -75) ? TFT_YELLOW
                 :                 TFT_ORANGE;

    // numeric RSSI + freshness
    dm.setCursor(CC_SEL, MW_RY(2));
    dm.setTextColor(0x7BEF); dm.printText("signal: ");
    dm.setTextColor(col);
    char rb[28];
    if (stale) snprintf(rb, sizeof(rb), "-- dBm   (no signal)");
    else       snprintf(rb, sizeof(rb), "%d dBm   %lus ago", rssi,
                        (unsigned long)(ageMs / 1000));
    dm.printText(rb);

    // big bar — the "metal detector": longer = closer
    int bx = 16, by = MW_RY(4), bw = 288, bh = 26;
    dm.fillRect(bx - 1, by - 1, bw + 2, bh + 2, 0x7BEF);   // frame
    dm.fillRect(bx, by, bw, bh, TFT_BLACK);
    if (!stale) {
        int r = rssi; if (r < -90) r = -90; if (r > -35) r = -35;
        int fw = (int)((float)(r + 90) / 55.0f * bw);
        if (fw > 0) dm.fillRect(bx, by, fw, bh, col);
    }

    // proximity word
    const char* word = stale       ? "no signal - move around to reacquire"
                     : (rssi >= -45) ? "VERY CLOSE - this is the one"
                     : (rssi >= -60) ? "CLOSE"
                     : (rssi >= -75) ? "NEAR - move it closer"
                     :                 "FAR - bar grows as you get warmer";
    dm.setCursor(bx, by + bh + 10);
    dm.setTextColor(col);
    dm.printText(word);

    // footer
    dm.setCursor(CC_SEL, MW_RY(12));
    dm.setTextColor(TFT_DARKGREY);
    dm.printText("move the device   click = this is it   [q] back");
}

// Returns true if the user confirms this candidate (→ proceed to name it).
static bool mwHuntDevice(int candIdx) {
    uint8_t  mac[6]; memcpy(mac, s_cand[candIdx].mac, 6);
    bool     wifi = s_cand[candIdx].isWiFi;
    char     label[20];
    const char* l = s_cand[candIdx].name[0] ? s_cand[candIdx].name
                  : (s_cand[candIdx].vendor ? s_cand[candIdx].vendor : "device");
    strncpy(label, l, 19); label[19] = '\0';

    float    sm = (float)s_cand[candIdx].rssi;
    uint32_t lastSeen = millis(), lastDraw = 0;
    bool     redraw = true;
    while (true) {
        // BLE: read straight from the continuous scan ring
        while (s_bleTail != s_bleHead) {
            MwBle e; memcpy(&e, (const void*)&s_bleRing[s_bleTail], sizeof(e));
            s_bleTail = (s_bleTail + 1) % MW_BLE_RING;
            if (memcmp(e.mac, mac, 6) == 0) { sm = sm * 0.6f + (float)e.rssi * 0.4f; lastSeen = millis(); }
        }
        // WiFi: short sniff window that only tracks this MAC
        if (wifi) mwWifiSniff(250, false, mac, &sm, &lastSeen);

        uint32_t now = millis();
        if (redraw || now - lastDraw >= 200) { mwDrawHunt(label, sm, now - lastSeen); lastDraw = now; redraw = false; }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') return false;
        if (tb == TBALL_CLICK || k == '\r' || k == '\n') return true;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

// Full add flow: scan → pick (STABLE list, manual [u] refresh) → name → prefix →
// proximity → save.  No Esc key on the T-Deck — cancel is the trackball click / [q].
static void mwAddMode() {
    mwGatherCandidates(3000);                    // initial scan
    if (s_candCount == 0) { mwSetNotice("Nothing found - press [a] to retry", TFT_ORANGE); return; }

    int page = 0, sel = 0; bool picked = false; bool redraw = true;
    while (true) {
        int pageCount  = min(MW_VIS, max(0, s_candCount - page * MW_VIS));
        int totalPages = max(1, (s_candCount + MW_VIS - 1) / MW_VIS);
        if (sel >= pageCount && pageCount > 0) sel = pageCount - 1;
        if (sel < 0) sel = 0;

        if (redraw) { mwDrawCandTable(page, sel); redraw = false; }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') return;                            // cancel
        if (tb == TBALL_CLICK || k == '\r' || k == '\n') { picked = true; break; }   // pick
        if (k == 'u' || k == 'U') { mwGatherCandidates(3000); sel = 0; page = 0; redraw = true; continue; }
        if (k == 'n' || k == 'N') {                                  // keep only close devices
            mwFilterNear(MW_CAND_NEAR);
            if (s_candCount == 0) mwGatherCandidates(3000);          // nothing close → rescan
            sel = 0; page = 0; redraw = true; continue;
        }
        if (k == 'h' || k == 'H') {                                  // hunt meter on selection
            if (s_candCount > 0 && mwHuntDevice(page * MW_VIS + sel)) { picked = true; break; }
            redraw = true; continue;
        }
        if (tb == TBALL_DOWN) {
            if (sel < pageCount - 1)            { sel++; redraw = true; }
            else if (page < totalPages - 1)     { page++; sel = 0; redraw = true; }
        } else if (tb == TBALL_UP) {
            if (sel > 0)                        { sel--; redraw = true; }
            else if (page > 0)                  { page--; sel = MW_VIS - 1; redraw = true; }
        }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    MwCand chosen = s_cand[page * MW_VIS + sel];

    // ── name ─────────────────────────────────────────────────────────────────────
    char name[24];
    if (!mwPromptText("Name this device:", name, sizeof(name))) return;   // click cancels
    if (!name[0]) {
        if (chosen.vendor) { strncpy(name, chosen.vendor, 23); name[23] = '\0'; }
        else mwMacStr(chosen.mac, 6, name);
    }

    // ── prefix (WiFi only) ────────────────────────────────────────────────────────
    uint8_t prefixLen = 6;
    if (chosen.isWiFi) {
        char c = mwPromptChoice("Match scope:",
                                "[f] full MAC (this exact device)",
                                "[v] vendor prefix (any of this brand)");
        if (c == 'v') prefixLen = 3;
    }

    // ── proximity gate ──────────────────────────────────────────────────────────
    int8_t nearRssi = 0;
    {
        char c = mwPromptChoice("Alert range:",
                                "[1] any range   [2] near ~2m",
                                "[3] room ~6m");
        if      (c == '2') nearRssi = MW_NEAR_2M;
        else if (c == '3') nearRssi = MW_NEAR_ROOM;
    }

    uint8_t radio = chosen.isWiFi ? MW_RADIO_WIFI : MW_RADIO_BT;
    int idx = mwAddOrUpdate(chosen.mac, prefixLen, name, radio, nearRssi);
    if (idx < 0) { mwSetNotice("Watchlist full", TFT_RED); return; }

    if (mwSaveWatchlist()) mwSetNotice("Saved", TFT_GREEN);   // self-guarded (ScopedPromiscPause)
    else                   mwSetNotice("Added (no SD - session only)", TFT_YELLOW);
}

// ── foreground watch screen (bmon-style table) ────────────────────────────────
// watch-list columns
#define CW_SEL     4    // ">" selected marker
#define CW_DOT    14    // "*" present / "." absent
#define CW_NAME   26
#define CW_RSSI  138
#define CW_RAD   172
#define CW_AGE   200

static void mwDrawWatch() {
    if (displayManager.isBlocked()) return;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    int present = 0;
    for (int i = 0; i < s_watchCount; i++) if (s_watch[i].present) present++;

    int page  = (MW_VIS > 0) ? s_sel / MW_VIS : 0;
    int start = page * MW_VIS;
    int end   = min(start + MW_VIS, s_watchCount);
    int total = max(1, (s_watchCount + MW_VIS - 1) / MW_VIS);

    // row 0 — header
    dm.setCursor(CW_SEL, MW_RY(0));
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("MW");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("WATCH");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    char hbuf[28];
    snprintf(hbuf, sizeof(hbuf), "%d near / %d  %d/%d", present, s_watchCount, page + 1, total);
    dm.setTextColor(TFT_WHITE);  dm.printText(hbuf);
    if (s_logOn) { dm.setTextColor(TFT_CYAN); dm.printText("  [LOG]"); }

    // row 1 — column headers
    dm.setTextColor(0x7BEF);
    dm.setCursor(CW_NAME, MW_RY(1)); dm.printText("NAME");
    dm.setCursor(CW_RSSI, MW_RY(1)); dm.printText("RSSI");
    dm.setCursor(CW_RAD,  MW_RY(1)); dm.printText("R");
    dm.setCursor(CW_AGE,  MW_RY(1)); dm.printText("AGE");

    // row 2 — separator
    dm.setCursor(CW_SEL, MW_RY(2)); dm.printSeparator();

    if (s_watchCount == 0) {
        dm.setCursor(CW_SEL, MW_RY(3));
        dm.setTextColor(0x7BEF);
        dm.printText("Empty - press [a] to add a device.");
    } else {
        uint32_t now = millis();
        for (int i = start; i < end; i++) {
            int16_t ry = MW_RY(3 + (i - start));
            MwEntry& e = s_watch[i];
            bool seld = (i == s_sel);
            if (seld) dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
            uint16_t base = e.present ? TFT_GREEN : 0x7BEF;

            dm.setCursor(CW_SEL, ry);
            dm.setTextColor(TFT_CYAN); dm.printText(seld ? ">" : " ");
            dm.setCursor(CW_DOT, ry);
            dm.setTextColor(base);     dm.printText(e.present ? "*" : ".");

            char nm[17]; snprintf(nm, sizeof(nm), "%-16.16s", e.name);
            dm.setCursor(CW_NAME, ry);
            dm.setTextColor(e.present ? TFT_WHITE : base); dm.printText(nm);

            dm.setCursor(CW_RSSI, ry);
            if (e.everSeen) {
                char rs[6]; snprintf(rs, sizeof(rs), "%4d", (int)e.rssiSmoothed);
                dm.setTextColor(e.present ? mwRssiColor((int8_t)e.rssiSmoothed) : base);
                dm.printText(rs);
            } else { dm.setTextColor(base); dm.printText("  --"); }

            const char* r = (e.radio == MW_RADIO_WIFI) ? "W" : (e.radio == MW_RADIO_BT) ? "B" : "WB";
            char rad[6]; snprintf(rad, sizeof(rad), "%s%s", r, e.nearRssi ? "~" : "");
            dm.setCursor(CW_RAD, ry);
            dm.setTextColor(base); dm.printText(rad);

            dm.setCursor(CW_AGE, ry);
            dm.setTextColor(base);
            if (e.present) {
                char ag[10]; snprintf(ag, sizeof(ag), "%lus",
                                      (unsigned long)((now - e.lastSeenMs) / 1000));
                dm.printText(ag);
            } else dm.printText("-");
        }
    }

    // row 10 — separator
    dm.setCursor(CW_SEL, MW_RY(10)); dm.printSeparator();

    // row 11 — transient notice
    if (s_noticeUntil && millis() < s_noticeUntil) {
        dm.setCursor(CW_SEL, MW_RY(11));
        dm.setTextColor(s_noticeCol);
        dm.printText(s_noticeText);
    }
    // row 12 — footer keys
    dm.setCursor(CW_SEL, MW_RY(12));
    dm.setTextColor(TFT_DARKGREY);
    dm.printText("[a]add [r]del [s]log [c]clr ~=near q=quit");

    // row 13 — bring-up diagnostic (both radios feeding the matcher?)
    dm.setCursor(CW_SEL, MW_RY(13));
    dm.setTextColor(0x528A);
    char diag[40];
    snprintf(diag, sizeof(diag), "diag ble:%lu wifi:%lu",
             (unsigned long)s_diagBle, (unsigned long)s_diagWifi);
    dm.printText(diag);
}

// ── foreground entry / main loop ──────────────────────────────────────────────
static void mwRunInteractive(bool addFirst) {
    bool wasBg = isMacwatchBgActive();   // resume bg on exit if it was running
    stopMacwatchBg();                    // release singleton BLE scan from bg
    mwEnsureLoaded();
    s_sel = 0; s_logOn = false; s_popupUntil = 0; s_noticeUntil = 0; s_evtCount = 0;
    s_pendPopup = false; s_diagBle = 0; s_diagWifi = 0; s_rmConfirmUntil = 0;
    mwBleStart();

    if (addFirst || s_watchCount == 0) mwAddMode();

    bool needDraw = true;
    uint32_t lastTick = 0;

    while (true) {
        uint32_t now = millis();

        // fast: drain BLE sightings
        while (s_bleTail != s_bleHead) {
            MwBle e; memcpy(&e, (const void*)&s_bleRing[s_bleTail], sizeof(e));
            s_bleTail = (s_bleTail + 1) % MW_BLE_RING;
            s_diagBle++;
            int idx = mwMatch(e.mac, false);
            if (idx >= 0 && mwSighting(idx, e.rssi, false)) mwOnArrive(idx, false);
        }

        // slow ~1 s: WiFi sniff (channel-hop) + expiry + event flush
        if (now - lastTick >= 1000) {
            lastTick = now;
            // Only sniff WiFi if something on the list is watched on WiFi —
            // a BLE-only list shouldn't disturb the radio / a live connection.
            bool needWifi = false;
            for (int i = 0; i < s_watchCount; i++)
                if (s_watch[i].radio & MW_RADIO_WIFI) { needWifi = true; break; }
            if (needWifi) mwWifiSniff(400, false);   // matches + alerts; promiscuous OFF on return
            mwExpire();
            mwFlushEvts();             // safe: promiscuous off
            // Repaint only when something is present (ages tick up) or a
            // transition happened — screen stays still when nothing's around.
            bool anyPresent = false;
            for (int i = 0; i < s_watchCount; i++) if (s_watch[i].present) { anyPresent = true; break; }
            if ((s_dirty || anyPresent) && !s_popupUntil) needDraw = true;
            s_dirty = false;
        }

        if (s_popupUntil && now >= s_popupUntil) { s_popupUntil = 0; needDraw = true; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) needDraw = true;
        if (!displayManager.isBlocked() && s_pendPopup) {
            mwCenterPopup(s_watch[s_pendIdx]); s_pendPopup = false;
        }

        char k = s_pendingKey ? s_pendingKey : inputHandler.getKeyboardInput();
        s_pendingKey = 0;
        TrackballEvent tb = inputHandler.getTrackballEvent();

        // any input other than a repeated [r] cancels a pending delete-confirm
        if ((k && k != 'r' && k != 'R') || tb == TBALL_UP || tb == TBALL_DOWN)
            s_rmConfirmUntil = 0;

        if (k == 'q' || k == 'Q') break;
        if (k == 'a' || k == 'A') { mwAddMode(); needDraw = true; }
        else if (k == 'r' || k == 'R') {
            if (s_watchCount > 0) {
                if (s_rmConfirmUntil && now < s_rmConfirmUntil) {     // confirmed
                    for (int i = s_sel; i < s_watchCount - 1; i++) s_watch[i] = s_watch[i + 1];
                    s_watchCount--;
                    if (s_sel >= s_watchCount) s_sel = max(0, s_watchCount - 1);
                    mwSaveWatchlist();
                    mwSetNotice("Deleted", TFT_YELLOW);
                    s_rmConfirmUntil = 0;
                } else {                                               // arm confirm
                    s_rmConfirmUntil = now + 3000;
                    char m[40]; snprintf(m, sizeof(m), "Delete '%.18s'? press [r]", s_watch[s_sel].name);
                    mwSetNotice(m, TFT_ORANGE);
                }
            }
            needDraw = true;
        }
        else if (k == 's' || k == 'S') {
            s_logOn = !s_logOn;
            if (s_logOn) sdCardManager.ensureDir(SD_DIR_MACWATCH);
            mwSetNotice(s_logOn ? "Event log ON" : "Event log OFF", TFT_CYAN);
            needDraw = true;
        }
        else if (k == 'c' || k == 'C') {
            for (int i = 0; i < s_watchCount; i++) {
                s_watch[i].present = false; s_watch[i].everSeen = false;
                s_watch[i].lastSeenMs = 0;
            }
            mwSetNotice("Presence cleared", TFT_CYAN);
            needDraw = true;
        }
        if (tb == TBALL_DOWN && s_sel < s_watchCount - 1) { s_sel++; needDraw = true; }
        if (tb == TBALL_UP   && s_sel > 0)                { s_sel--; needDraw = true; }

        if (s_noticeUntil && now >= s_noticeUntil) { s_noticeUntil = 0; needDraw = true; }

        if (needDraw && !displayManager.isBlocked() && !s_popupUntil) {
            mwDrawWatch();
            needDraw = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    mwBleStop();
    // Clean up only macwatch's OWN promiscuous. If wguard bg owns it, leave it ON —
    // mwWifiSniff() bailed early in that case and never enabled it, so disabling here
    // would silently stop wguard's IDS sniffing.
    if (!wGuard.isBackground()) esp_wifi_set_promiscuous(false);
    s_logOn = false;
    if (wasBg) startMacwatchBg();          // resume the background watcher we paused
    else       displayManager.printCommandScreen();
}

// ── public: foreground dispatch ───────────────────────────────────────────────
void runMacwatch(char* args) {
    if (args && (strncmp(args, "bg", 2) == 0)) {
        if (isMacwatchBgActive()) {                 // idempotent — don't stop/restart
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("macwatch already running in background");
            displayManager.printCommandScreen();
        } else {
            startMacwatchBg();                       // prints its own status + screen
        }
        return;
    }
    if (args && (strncmp(args, "stop", 4) == 0)) {
        if (isMacwatchBgActive()) {
            stopMacwatchBg();
            displayManager.println("macwatch background stopped");
        } else {
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("macwatch not running in background");
        }
        displayManager.printCommandScreen();
        return;
    }
    bool addFirst = args && (strncmp(args, "add", 3) == 0);
    mwRunInteractive(addFirst);
}

// ── public: background mode ───────────────────────────────────────────────────
void startMacwatchBg() {
    if (s_bgActive) stopMacwatchBg();
    mwEnsureLoaded();
    s_logOn = sdCardManager.isReady();      // bg logs events when SD present
    s_evtCount = 0; s_pendPopup = false; s_bgPopupUntil = 0; s_bgLastPoll = 0;
    mwBleStart();
    s_bgActive = true;
    displayManager.setMwActive(true);

    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("[MW] BG  ");
    char m[40];
    snprintf(m, sizeof(m), "%d watched (BLE)", s_watchCount);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText(m);
    displayManager.setTextColor(0x4208);
    displayManager.println("   [mw stop]");
    displayManager.printCommandScreen();
}

void stopMacwatchBg() {
    if (!s_bgActive) return;
    s_bgActive = false;
    mwBleStop();
    displayManager.setMwActive(false);
}

bool isMacwatchBgActive() { return s_bgActive; }

void pollMacwatchBg() {
    if (!s_bgActive) return;
    uint32_t now = millis();
    if (now - s_bgLastPoll >= 200) {
        s_bgLastPoll = now;
        while (s_bleTail != s_bleHead) {
            MwBle e; memcpy(&e, (const void*)&s_bleRing[s_bleTail], sizeof(e));
            s_bleTail = (s_bleTail + 1) % MW_BLE_RING;
            int idx = mwMatch(e.mac, false);
            if (idx >= 0 && mwSighting(idx, e.rssi, false)) mwOnArrive(idx, true);
        }
        mwExpire();
        mwFlushEvts();             // bg never runs promiscuous → always GDMA-safe
        if (!displayManager.isBlocked() && s_pendPopup) {
            mwBgPopup(s_watch[s_pendIdx]); s_pendPopup = false;
        }
    }
    if (s_bgPopupUntil && now >= s_bgPopupUntil) {
        s_bgPopupUntil = 0;
        if (!displayManager.isBlocked()) displayManager.printCommandScreen();
    }
}
