// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// netspy / ns — discover devices on a network with WiFi CLIENT ISOLATION
// enabled, where a normal ARP scan (`nd`) sees nothing because the AP blocks
// client-to-client unicast.
//
// Technique reference (NO code used — reimplemented from published research):
//   AirSnitch, Mathy Vanhoef et al., NDSS 2026.
//   https://github.com/vanhoefm/airsnitch  ·  https://papers.mathyvanhoef.com/ndss2026-airsnitch.pdf
//   AirSnitch is all-rights-reserved (no license) → techniques only, from the paper.
//
// ── HOW IT WORKS (HW-verified) ────────────────────────────────────────────────
// Client isolation only blocks client↔client UNICAST. Broadcast/multicast frames
// (ARP, DHCP, mDNS, SSDP, IPv6-ND) are sent by the AP to ALL associated clients,
// GTK-encrypted. Because the T-Deck is associated, its WiFi hardware ALREADY
// DECRYPTS those group frames for us — in promiscuous mode we receive them with
// the CCMP header still present but the payload in clear. So we just sniff group
// data frames from our BSSID and parse the plaintext at (hdrlen + CCMP 8) → every
// device that broadcasts shows up, with its real MAC + IP, despite isolation.
// (Verified: an ARP who-has from 00:ff:ea:ed:43:53 / 10.0.x decoded cleanly.)
//
// No software CCMP / GTK needed for this passive discovery. The GTK-extraction
// path (`ns gtk`, reads gWpaSm+0x174) is kept for a future Stage-2 INJECT, where
// esp_wifi_80211_tx does NOT auto-encrypt so we'd CCMP-encrypt ourselves.

#include "netspy.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include "lockscreen_manager.h"
#include "wifi_sd_guard.h"            // ScopedPromiscPause — GDMA-safe SD writes
#include "oui_lookup.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// Exported wpa_supplicant global (IDF 4.4.7 / arduino-esp32 2.0.17). GTK lives at
// +0x174 (len at +0x194). Framework-specific — platform is pinned in platformio.ini.
extern "C" { extern uint8_t gWpaSm[]; }
#define NETSPY_WPASM_LEN   0x34C
#define NETSPY_GTK_OFF     0x174
#define NETSPY_GTKLEN_OFF  0x194

// ── device table + capture ring ───────────────────────────────────────────────
#define NS_MAX_DEV   48
#define NS_RING      12
#define NS_PL_MAX    256
#define NS_HOW_ARP   0x01
#define NS_HOW_IP    0x02

struct NsDev {
    uint8_t     mac[6];
    uint32_t    ip;            // host order, 0 = unknown
    const char* vendor;
    const char* type;
    uint8_t     how;
    uint32_t    lastMs;
};
static NsDev s_dev[NS_MAX_DEV];
static int   s_devN;

struct NsRing { uint8_t mac[6]; uint16_t len; uint8_t pl[NS_PL_MAX]; };
static volatile NsRing   s_ring[NS_RING];
static volatile uint8_t  s_head, s_tail;
static uint8_t           s_bssid[6];

// ── header ─────────────────────────────────────────────────────────────────────
static void netspyHeader(const char* tag) {
    auto& dm = displayManager;
    dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
    dm.setCursor(6, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("NET");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("SPY");
    dm.setTextColor(0x7BEF);     dm.printText("]  "); dm.println(tag);
    dm.printSeparator();
}

// ── ns gtk — show the live group key (Stage-2 groundwork) ─────────────────────
static void netspyShowGtk() {
    auto& dm = displayManager;
    netspyHeader("GTK (group key)");
    uint32_t len = *(const uint32_t*)(gWpaSm + NETSPY_GTKLEN_OFF);
    if (len != 16 && len != 32) {
        dm.setTextColor(TFT_RED);
        char b[40]; snprintf(b, sizeof(b), "GTK len=%lu (want 16/32)", (unsigned long)len);
        dm.println(b);
        dm.setTextColor(0x7BEF); dm.println("Offset drift? run 'ns dump'.");
        dm.printCommandScreen(); return;
    }
    const uint8_t* gtk = gWpaSm + NETSPY_GTK_OFF;
    dm.setTextColor(TFT_GREEN); dm.println("GTK from gWpaSm:");
    dm.setTextColor(TFT_WHITE);
    char row[40]; int n = 0;
    for (uint32_t i = 0; i < len; i++) {
        n += snprintf(row + n, sizeof(row) - n, "%02X ", gtk[i]);
        if ((i & 7) == 7) { dm.setCursor(10, dm.getCursorY()); dm.println(row); n = 0; }
    }
    if (n) { dm.setCursor(10, dm.getCursorY()); dm.println(row); }
    char info[40]; snprintf(info, sizeof(info), "len %lu B  @ +0x%X", (unsigned long)len, NETSPY_GTK_OFF);
    dm.setTextColor(0x7BEF); dm.println(info);
    dm.printCommandScreen();
}

// ── ns dump — full gWpaSm hex to SD (re-find offset if toolchain changes) ─────
static void netspyDump() {
    auto& dm = displayManager;
    netspyHeader("gWpaSm dump");
    if (!sdCardManager.isReady()) { dm.setTextColor(TFT_RED); dm.println("No SD."); dm.printCommandScreen(); return; }
    sdCardManager.ensureDir(SD_DIR_NETSPY);
    File f = SD.open(SD_DIR_NETSPY "/gwpasm.txt", FILE_WRITE);
    if (!f) { dm.setTextColor(TFT_RED); dm.println("SD open failed."); dm.printCommandScreen(); return; }
    const uint8_t* p = gWpaSm;
    char line[80];
    for (int i = 0; i < NETSPY_WPASM_LEN; i += 16) {
        int n = snprintf(line, sizeof(line), "%03x: ", i);
        for (int j = 0; j < 16 && (i + j) < NETSPY_WPASM_LEN; j++)
            n += snprintf(line + n, sizeof(line) - n, "%02x ", p[i + j]);
        line[n] = '\0'; f.println(line);
    }
    f.close();
    dm.setTextColor(TFT_GREEN); dm.println("Dumped -> " SD_DIR_NETSPY "/gwpasm.txt");
    dm.printCommandScreen();
}

// ── device table helpers ───────────────────────────────────────────────────────
static void nsAddDev(const uint8_t* mac, uint32_t ip, uint8_t how) {
    if (mac[0] & 0x01) return;                       // skip group/multicast MAC
    bool z = true; for (int i = 0; i < 6; i++) if (mac[i]) { z = false; break; }
    if (z) return;                                   // skip all-zero
    for (int i = 0; i < s_devN; i++) {
        if (!memcmp(s_dev[i].mac, mac, 6)) {
            if (ip) s_dev[i].ip = ip;
            s_dev[i].how |= how; s_dev[i].lastMs = millis();
            return;
        }
    }
    if (s_devN >= NS_MAX_DEV) return;
    NsDev& d = s_dev[s_devN++];
    memcpy(d.mac, mac, 6); d.ip = ip; d.how = how; d.lastMs = millis();
    OuiInfo oi = ouiLookup(mac); d.vendor = oi.vendor; d.type = oi.type;
}

// Parse one decrypted payload (LLC/SNAP) → device(s).
static void nsParse(const uint8_t* mac, const uint8_t* pl, int pll) {
    if (pll < 8) return;
    if (!(pl[0] == 0xAA && pl[1] == 0xAA && pl[2] == 0x03)) return;  // need LLC/SNAP
    uint16_t eth = (pl[6] << 8) | pl[7];
    const uint8_t* p = pl + 8; int n = pll - 8;
    if (eth == 0x0806 && n >= 28) {                  // ARP — sender MAC + IP
        uint32_t sip = ((uint32_t)p[14] << 24) | ((uint32_t)p[15] << 16) |
                       ((uint32_t)p[16] << 8) | p[17];
        nsAddDev(p + 8, sip, NS_HOW_ARP);
    } else if (eth == 0x0800 && n >= 20) {           // IPv4 — L2 src MAC + IP src
        uint32_t sip = ((uint32_t)p[12] << 24) | ((uint32_t)p[13] << 16) |
                       ((uint32_t)p[14] << 8) | p[15];
        nsAddDev(mac, sip, NS_HOW_IP);
    }
}

// ── promiscuous capture (group data frames from our BSS) ──────────────────────
static void nsCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (t != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    int len = pk->rx_ctrl.sig_len;
    if (len < 40 || len > 1600) return;
    const uint8_t* f = pk->payload;
    uint16_t fc = f[0] | (f[1] << 8);
    if (((fc >> 2) & 3) != 2)  return;               // data
    if (!(f[4] & 0x01))        return;               // A1 group
    int toDS = (fc >> 8) & 1, fromDS = (fc >> 9) & 1;
    if (!(fromDS && !toDS))    return;               // AP -> clients (A2 = BSSID)
    if (memcmp(f + 10, s_bssid, 6) != 0) return;     // our BSS
    int subtype = (fc >> 4) & 0xf; bool qos = subtype & 0x08;
    int hdrlen = 24 + (qos ? 2 : 0);                 // no A4 (we required !toDS)
    int prot = (fc >> 14) & 1;
    int payoff = hdrlen + (prot ? 8 : 0);            // CCMP header present if Protected
    if (len <= payoff + 8) return;
    int pll = len - payoff; if (pll > NS_PL_MAX) pll = NS_PL_MAX;
    uint8_t nx = (uint8_t)((s_head + 1) % NS_RING);
    if (nx == s_tail) return;                        // ring full → drop
    memcpy((void*)s_ring[s_head].mac, f + 16, 6);    // A3 = original sender
    s_ring[s_head].len = (uint16_t)pll;
    memcpy((void*)s_ring[s_head].pl, f + payoff, pll);
    s_head = nx;
}

// ── SD save ───────────────────────────────────────────────────────────────────
static void nsSave() {
    auto& dm = displayManager;
    if (!sdCardManager.isReady()) return;
    char path[40]; uint16_t seq = 1;
    {
        ScopedPromiscPause _;                        // GDMA: pause sniff for SD I/O
        sdCardManager.ensureDir(SD_DIR_NETSPY);
        while (seq <= 999) {
            snprintf(path, sizeof(path), SD_DIR_NETSPY "/%03u.csv", seq);
            if (!SD.exists(path)) break;
            seq++;
        }
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            char ts[24]; ClockManager::instance().getTimestamp(ts, sizeof(ts));
            if (!ts[0]) snprintf(ts, sizeof(ts), "@%lums", (unsigned long)millis());
            f.println("time,mac,ip,vendor,type,how");
            for (int i = 0; i < s_devN; i++) {
                NsDev& d = s_dev[i];
                char line[100];
                snprintf(line, sizeof(line),
                         "%s,%02x:%02x:%02x:%02x:%02x:%02x,%u.%u.%u.%u,%s,%s,%s%s",
                         ts, d.mac[0],d.mac[1],d.mac[2],d.mac[3],d.mac[4],d.mac[5],
                         (d.ip>>24)&0xff,(d.ip>>16)&0xff,(d.ip>>8)&0xff,d.ip&0xff,
                         d.vendor ? d.vendor : "?", d.type ? d.type : "?",
                         (d.how & NS_HOW_ARP) ? "A" : "", (d.how & NS_HOW_IP) ? "I" : "");
                f.println(line);
            }
            f.close();
        }
    }
    char b[40]; snprintf(b, sizeof(b), "Saved %d -> %s", s_devN, path);
    dm.setTextColor(0x6FE8); dm.setCursor(6, 226); dm.printText(b);
}

// ── discovery UI ───────────────────────────────────────────────────────────────
#define NS_ROWS 9
static void nsDraw(int page) {
    auto& dm = displayManager;
    netspyHeader("client-isolation recon");
    dm.setTextColor(0x7BEF);
    dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("IP              VENDOR/TYPE   H");
    int total = (s_devN + NS_ROWS - 1) / NS_ROWS; if (total < 1) total = 1;
    if (page >= total) page = total - 1;
    for (int r = 0; r < NS_ROWS; r++) {
        int idx = page * NS_ROWS + r;
        int y = outputY + LINE_HEIGHT * (3 + r);
        if (idx >= s_devN) continue;
        NsDev& d = s_dev[idx];
        char ipb[16];
        snprintf(ipb, sizeof(ipb), "%u.%u.%u.%u",
                 (unsigned)((d.ip>>24)&0xff), (unsigned)((d.ip>>16)&0xff),
                 (unsigned)((d.ip>>8)&0xff),  (unsigned)(d.ip&0xff));
        char line[56];
        snprintf(line, sizeof(line), "%-15s %-13s %s%s",
                 ipb, d.vendor ? d.vendor : "?",
                 (d.how & NS_HOW_ARP) ? "A" : " ", (d.how & NS_HOW_IP) ? "I" : "");
        dm.setTextColor(TFT_WHITE);
        dm.setCursor(6, y); dm.println(line);
    }
    char foot[48];
    snprintf(foot, sizeof(foot), "dev:%d  pg %d/%d  s=save c=clr q=quit",
             s_devN, page + 1, total);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText(foot);
}

static void netspyDiscover() {
    auto& dm = displayManager;
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { netspyHeader("recon"); dm.setTextColor(TFT_RED); dm.println("No BSSID (not associated)."); dm.printCommandScreen(); return; }
    memcpy(s_bssid, bm, 6);
    s_devN = 0; s_head = s_tail = 0;

    nsDraw(0);

    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(nsCb);
    esp_wifi_set_promiscuous(true);

    int page = 0; uint32_t lastDraw = 0; bool run = true;
    while (run) {
        // drain capture ring → parse
        while (s_tail != s_head) {
            NsRing e;
            memcpy(&e, (const void*)&s_ring[s_tail], sizeof(e));
            s_tail = (uint8_t)((s_tail + 1) % NS_RING);
            nsParse(e.mac, e.pl, e.len);
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { run = false; break; }
        else if (k == 'c' || k == 'C') { s_devN = 0; lastDraw = 0; }
        else if (k == 's' || k == 'S') { nsSave(); vTaskDelay(pdMS_TO_TICKS(1200)); lastDraw = 0; }
        else if (k == 'l' || k == 'L') { page++; lastDraw = 0; }
        else if (k == 'a' || k == 'A') { if (page > 0) page--; lastDraw = 0; }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 1000) { nsDraw(page); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    dm.clearScreen();
    dm.printCommandScreen();
}

// ── entry ──────────────────────────────────────────────────────────────────────
void runNetspy(char* args) {
    auto& dm = displayManager;
    if (WiFi.status() != WL_CONNECTED) {
        netspyHeader("recon");
        dm.setTextColor(TFT_RED);   dm.println("Connect first:  cw <ssid>");
        dm.setTextColor(0x7BEF);    dm.println("(needs to be on the target net)");
        dm.printCommandScreen();
        return;
    }
    if (args && (args[0] == 'g' || args[0] == 'G')) { netspyShowGtk(); return; }
    if (args && (args[0] == 'd' || args[0] == 'D')) { netspyDump();    return; }
    netspyDiscover();
}
