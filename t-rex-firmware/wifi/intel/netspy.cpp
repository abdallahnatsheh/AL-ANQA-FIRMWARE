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
// (Verified: an ARP who-has from xx:xx:xx:xx:xx:xx / 10.0.x decoded cleanly.)
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
#include "network_scanner.h"        // launch ps/pg directly on a discovered device

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;
extern NetworkScanner networkScanner;

// Exported wpa_supplicant global (IDF 4.4.7 / arduino-esp32 2.0.17). GTK lives at
// +0x174 (len at +0x194). Framework-specific — platform is pinned in platformio.ini.
extern "C" { extern uint8_t gWpaSm[]; }
#define NETSPY_WPASM_LEN   0x34C
#define NETSPY_GTK_OFF     0x174
#define NETSPY_GTKLEN_OFF  0x194

// ── device table + capture ring ───────────────────────────────────────────────
#define NS_MAX_DEV   48
#define NS_RING      12
// DHCP options sit past the BOOTP sname(64)+file(128) legacy fields → the magic
// cookie is at offset 236, options at 240. To reach a hostname (opt 12) we must
// capture LLC(8)+IP(20)+UDP(8)+DHCP(~360) ≈ 400 bytes of payload, not 256.
#define NS_PL_MAX    400
#define NS_HOW_ARP   0x01
#define NS_HOW_IP    0x02
#define NS_HOW_DHCP  0x04
#define NS_HOW_MDNS  0x08
#define NS_HOW_SSDP  0x10

// Service-type bits (mDNS PTR/SRV record names + SSDP URN) — shown in [i] detail.
#define NS_SVC_AIRPLAY 0x0001
#define NS_SVC_CAST    0x0002
#define NS_SVC_APPLE   0x0004
#define NS_SVC_PRINT   0x0008
#define NS_SVC_SSH     0x0010
#define NS_SVC_SMB     0x0020
#define NS_SVC_HOMEKIT 0x0040
#define NS_SVC_SPOTIFY 0x0080
#define NS_SVC_AMAZON  0x0100
#define NS_SVC_HTTP    0x0200
#define NS_SVC_DLNA    0x0400

struct NsDev {
    uint8_t     mac[6];
    uint32_t    ip;            // host order, 0 = unknown
    char        name[24];      // DHCP/mDNS hostname or SSDP product, "" = unknown
    const char* vendor;
    const char* type;
    uint16_t    svc;           // NS_SVC_* bitmask
    uint8_t     how;
    uint32_t    lastMs;
};
static NsDev s_dev[NS_MAX_DEV];
static int   s_devN;

struct NsRing { uint8_t mac[6]; uint16_t len; uint8_t pl[NS_PL_MAX]; };
static volatile NsRing   s_ring[NS_RING];
static volatile uint8_t  s_head, s_tail;
static uint8_t           s_bssid[6];

// Exposed so portscan/ping can target the netspy list (ps ns3 / pg ns0). The
// table persists after `ns` exits (s_devN isn't cleared), like the ARP cache.
int      netspyDeviceCount()      { return s_devN; }
uint32_t netspyDeviceIp(int idx)  { return (idx >= 0 && idx < s_devN) ? s_dev[idx].ip : 0; }

// isoscan targeting: victim MAC + display name from the same persistent table.
bool netspyDeviceMac(int idx, uint8_t out[6]) {
    if (idx < 0 || idx >= s_devN) return false;
    memcpy(out, s_dev[idx].mac, 6);
    return true;
}
const char* netspyDeviceName(int idx) {
    if (idx < 0 || idx >= s_devN) return nullptr;
    if (s_dev[idx].name[0]) return s_dev[idx].name;
    return s_dev[idx].vendor ? s_dev[idx].vendor : "?";
}
bool netspyGetGtk(uint8_t out[32], int* lenOut) {
    uint32_t len = *(const uint32_t*)(gWpaSm + NETSPY_GTKLEN_OFF);
    if (lenOut) *lenOut = (int)len;
    if (len != 16 && len != 32) return false;         // offset drift / not assoc'd
    memcpy(out, gWpaSm + NETSPY_GTK_OFF, len);
    return true;
}

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
// strongName=true (DHCP/mDNS hostname) overwrites; false (SSDP product) fills
// the name only when still empty, so a model string can't clobber a real host.
static void nsAddDev(const uint8_t* mac, uint32_t ip, uint8_t how,
                     const char* name = nullptr, bool strongName = true) {
    if (mac[0] & 0x01) return;                       // skip group/multicast MAC
    bool z = true; for (int i = 0; i < 6; i++) if (mac[i]) { z = false; break; }
    if (z) return;                                   // skip all-zero
    for (int i = 0; i < s_devN; i++) {
        if (!memcmp(s_dev[i].mac, mac, 6)) {
            if (ip) s_dev[i].ip = ip;
            if (name && name[0] && (strongName || !s_dev[i].name[0])) {
                strncpy(s_dev[i].name, name, sizeof(s_dev[i].name) - 1);
                s_dev[i].name[sizeof(s_dev[i].name) - 1] = '\0';
            }
            s_dev[i].how |= how; s_dev[i].lastMs = millis();
            return;
        }
    }
    if (s_devN >= NS_MAX_DEV) return;
    NsDev& d = s_dev[s_devN++];
    memcpy(d.mac, mac, 6); d.ip = ip; d.how = how; d.lastMs = millis();
    d.svc = 0;
    d.name[0] = '\0';
    if (name && name[0]) {
        strncpy(d.name, name, sizeof(d.name) - 1);
        d.name[sizeof(d.name) - 1] = '\0';
    }
    OuiInfo oi = ouiLookup(mac); d.vendor = oi.vendor; d.type = oi.type;
}

// OR service bits into an existing device (created already via its IP/name frame).
static void nsOrSvc(const uint8_t* mac, uint16_t bits) {
    if (!bits) return;
    for (int i = 0; i < s_devN; i++)
        if (!memcmp(s_dev[i].mac, mac, 6)) { s_dev[i].svc |= bits; return; }
}

// Map an mDNS record name (e.g. "_airplay._tcp.local") to service bits.
static uint16_t nsSvcFromName(const char* nm) {
    uint16_t b = 0;
    if (strstr(nm, "_airplay") || strstr(nm, "_raop"))                              b |= NS_SVC_AIRPLAY;
    if (strstr(nm, "_googlecast"))                                                  b |= NS_SVC_CAST;
    if (strstr(nm, "_companion-link") || strstr(nm, "_airdrop") || strstr(nm, "_apple-mobdev")) b |= NS_SVC_APPLE;
    if (strstr(nm, "_ipp") || strstr(nm, "_printer") || strstr(nm, "_pdl-datastream")) b |= NS_SVC_PRINT;
    if (strstr(nm, "_ssh") || strstr(nm, "_sftp"))                                  b |= NS_SVC_SSH;
    if (strstr(nm, "_smb"))                                                         b |= NS_SVC_SMB;
    if (strstr(nm, "_hap"))                                                         b |= NS_SVC_HOMEKIT;
    if (strstr(nm, "_spotify-connect"))                                             b |= NS_SVC_SPOTIFY;
    if (strstr(nm, "_amzn") || strstr(nm, "_alexa"))                                b |= NS_SVC_AMAZON;
    if (strstr(nm, "_http"))                                                        b |= NS_SVC_HTTP;
    return b;
}

// Short display tag for one service bit.
static const char* nsSvcTag(uint16_t bit) {
    switch (bit) {
        case NS_SVC_AIRPLAY: return "AirPlay";
        case NS_SVC_CAST:    return "Cast";
        case NS_SVC_APPLE:   return "Apple";
        case NS_SVC_PRINT:   return "Printer";
        case NS_SVC_SSH:     return "SSH";
        case NS_SVC_SMB:     return "SMB";
        case NS_SVC_HOMEKIT: return "HomeKit";
        case NS_SVC_SPOTIFY: return "Spotify";
        case NS_SVC_AMAZON:  return "Alexa";
        case NS_SVC_HTTP:    return "HTTP";
        case NS_SVC_DLNA:    return "DLNA";
    }
    return "";
}

// Case-sensitive substring search in a non-terminated byte buffer (SSDP URN scan).
static bool nsRawHas(const uint8_t* t, int len, const char* s) {
    int sl = (int)strlen(s);
    for (int i = 0; i + sl <= len; i++) {
        int j = 0; for (; j < sl; j++) if (t[i + j] != (uint8_t)s[j]) break;
        if (j == sl) return true;
    }
    return false;
}

static const uint16_t NS_SVC_BITS[] = {
    NS_SVC_AIRPLAY, NS_SVC_CAST, NS_SVC_APPLE, NS_SVC_PRINT, NS_SVC_SSH, NS_SVC_SMB,
    NS_SVC_HOMEKIT, NS_SVC_SPOTIFY, NS_SVC_AMAZON, NS_SVC_HTTP, NS_SVC_DLNA };

// Build a space-separated service tag list (e.g. "AirPlay HomeKit") into b[n].
static void nsSvcStr(uint16_t svc, char* b, int n) {
    b[0] = '\0';
    for (unsigned i = 0; i < sizeof(NS_SVC_BITS) / sizeof(NS_SVC_BITS[0]); i++)
        if (svc & NS_SVC_BITS[i]) {
            if (b[0]) strncat(b, " ", n - strlen(b) - 1);
            strncat(b, nsSvcTag(NS_SVC_BITS[i]), n - strlen(b) - 1);
        }
}

// Stage 1b — parse a DHCP/BOOTP payload (UDP 67/68) → client MAC (chaddr),
// assigned/requested IP, and hostname (option 12). Richest single source: gives
// name + MAC + IP atomically, even for devices that otherwise stay silent.
static void nsParseDHCP(const uint8_t* d, int dlen) {
    if (dlen < 240) return;                          // need fixed BOOTP + cookie
    if (!(d[236] == 0x63 && d[237] == 0x82 &&        // DHCP magic cookie
          d[238] == 0x53 && d[239] == 0x63)) return;
    const uint8_t* chaddr = d + 28;                  // client hw addr (real MAC)
    uint32_t yiaddr = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) |
                      ((uint32_t)d[18] << 8) | d[19];   // assigned (OFFER/ACK)
    uint32_t ciaddr = ((uint32_t)d[12] << 24) | ((uint32_t)d[13] << 16) |
                      ((uint32_t)d[14] << 8) | d[15];   // current (renew)
    uint32_t reqip = 0;
    char host[24]; host[0] = '\0';
    int i = 240;                                     // option TLVs
    while (i + 1 < dlen) {
        uint8_t opt = d[i];
        if (opt == 0xFF) break;                      // end
        if (opt == 0x00) { i++; continue; }          // pad
        if (i + 2 > dlen) break;
        uint8_t l = d[i + 1];
        if (i + 2 + l > dlen) break;
        const uint8_t* v = d + i + 2;
        if (opt == 12 && l > 0) {                     // hostname
            int cl = l < (int)sizeof(host) - 1 ? l : (int)sizeof(host) - 1;
            int k = 0; for (; k < cl; k++) { char c = v[k]; host[k] = (c >= 32 && c < 127) ? c : '?'; }
            host[k] = '\0';
        } else if (opt == 50 && l == 4) {             // requested IP
            reqip = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
        }
        i += 2 + l;
    }
    uint32_t ip = yiaddr ? yiaddr : (ciaddr ? ciaddr : reqip);
    nsAddDev(chaddr, ip, NS_HOW_DHCP, host[0] ? host : nullptr);
}

// Read a DNS name from packet base b[blen] starting at offset off → dotted name
// in out[outsz]. Handles label compression (0xC0 pointers) with a jump guard so
// a crafted packet can't loop or over-read. Returns the offset to continue the
// record stream (inline position past the name / past the 2 pointer bytes), or
// -1 on a malformed name.
static int dnsName(const uint8_t* b, int blen, int off, char* out, int outsz) {
    int o = off, outn = 0, jumps = 0, ret = -1;
    while (o >= 0 && o < blen) {
        uint8_t len = b[o];
        if (len == 0) { o++; if (ret < 0) ret = o; break; }
        if ((len & 0xC0) == 0xC0) {                  // compression pointer
            if (o + 1 >= blen) { out[0] = '\0'; return -1; }
            if (ret < 0) ret = o + 2;                // record resumes after 2 bytes
            if (++jumps > 6) break;                  // loop guard
            o = ((len & 0x3F) << 8) | b[o + 1];
            continue;
        }
        if (len & 0xC0) { out[0] = '\0'; return -1; } // reserved label type
        o++;
        if (o + len > blen) { out[0] = '\0'; return -1; }
        for (int k = 0; k < len; k++) {
            if (outn < outsz - 1) { char c = b[o + k]; out[outn++] = (c >= 32 && c < 127) ? c : '?'; }
        }
        if (outn < outsz - 1) out[outn++] = '.';
        o += len;
    }
    if (outn > 0 && out[outn - 1] == '.') outn--;     // strip trailing dot
    out[outn] = '\0';
    return ret;
}

// Stage 1b — parse an mDNS response (UDP 5353). Devices announce their own
// `<host>.local` far more often than they DHCP, so this fills names on a quiet
// network. We take the hostname from A/AAAA answer records (whose owner is the
// sending device) and attach it to the frame's L2 source MAC. Service-type
// (PTR/SRV) enumeration is deferred to the future [i] detail view.
static void nsParseMDNS(const uint8_t* mac, const uint8_t* b, int blen) {
    if (blen < 12) return;
    uint16_t flags = (b[2] << 8) | b[3];
    if (!(flags & 0x8000)) return;                   // QR=1 → responses only
    uint16_t qd = (b[4] << 8) | b[5];
    uint16_t an = (b[6] << 8) | b[7];
    char nm[40], host[24]; host[0] = '\0';
    uint16_t svc = 0; uint32_t hip = 0;
    int o = 12;
    for (int q = 0; q < qd && o < blen; q++) {        // skip questions
        o = dnsName(b, blen, o, nm, sizeof(nm));
        if (o < 0 || o + 4 > blen) return;
        o += 4;                                      // QTYPE + QCLASS
    }
    for (int a = 0; a < an && o < blen; a++) {
        o = dnsName(b, blen, o, nm, sizeof(nm));
        if (o < 0 || o + 10 > blen) break;
        uint16_t type  = (b[o] << 8) | b[o + 1];
        uint16_t rdlen = (b[o + 8] << 8) | b[o + 9];
        const uint8_t* rd = b + o + 10;
        if (o + 10 + rdlen > blen) break;
        svc |= nsSvcFromName(nm);                     // PTR/SRV names carry services
        if ((type == 1 || type == 28) && !host[0] && nm[0] && nm[0] != '_') {  // A/AAAA host
            int L = (int)strlen(nm);
            if (L > 6 && strcmp(nm + L - 6, ".local") == 0) nm[L - 6] = '\0';
            if (nm[0]) { strncpy(host, nm, sizeof(host) - 1); host[sizeof(host) - 1] = '\0'; }
            if (type == 1 && rdlen == 4)
                hip = ((uint32_t)rd[0] << 24) | ((uint32_t)rd[1] << 16) | ((uint32_t)rd[2] << 8) | rd[3];
        }
        o += 10 + rdlen;
    }
    if (host[0] || svc) {
        nsAddDev(mac, hip, NS_HOW_MDNS, host[0] ? host : nullptr);
        nsOrSvc(mac, svc);
    }
}

// Find HTTP-style header h (e.g. "SERVER:") case-insensitively at a line start
// in t[len], copy its value (to CRLF) into out[outsz]. SSDP is plaintext.
static bool ssdpHeader(const uint8_t* t, int len, const char* h, char* out, int outsz) {
    int hl = (int)strlen(h);
    for (int i = 0; i + hl <= len; i++) {
        if (i != 0 && t[i - 1] != '\n') continue;    // header must start a line
        bool m = true;
        for (int j = 0; j < hl; j++) {
            char a = t[i + j], b = h[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { m = false; break; }
        }
        if (!m) continue;
        int v = i + hl;
        while (v < len && (t[v] == ' ' || t[v] == '\t')) v++;   // skip leading WS
        int o = 0;
        while (v < len && t[v] != '\r' && t[v] != '\n') {
            char c = t[v++];
            if (o < outsz - 1) out[o++] = (c >= 32 && c < 127) ? c : ' ';
        }
        out[o] = '\0';
        return o > 0;
    }
    return false;
}

// Stage 1b — parse an SSDP/UPnP advert (UDP 1900). NOTIFY ssdp:alive / M-SEARCH
// responses carry a SERVER: header naming the product (e.g. "Roku/9.4"). Fills
// the name only when empty (won't override a DHCP/mDNS hostname). Good for media
// players / smart TVs / printers that advertise UPnP but lack a friendly name.
static void nsParseSSDP(const uint8_t* mac, const uint8_t* t, int len) {
    if (len < 16) return;
    uint16_t svc = 0;
    if (nsRawHas(t, len, "MediaRenderer") || nsRawHas(t, len, "MediaServer")) svc |= NS_SVC_DLNA;
    char val[48];
    bool hasServer = ssdpHeader(t, len, "SERVER:", val, sizeof(val));
    if (!hasServer && !svc) return;                  // not an identifying advert
    if (hasServer) {
        char* prod = val;                            // prefer the token after UPnP/1.x
        char* u = strstr(val, "UPnP/");
        if (u) { char* sp = strchr(u, ' '); if (sp && sp[1]) prod = sp + 1; }
        int pl = (int)strlen(prod);
        while (pl > 0 && prod[pl - 1] == ' ') prod[--pl] = '\0';
        nsAddDev(mac, 0, NS_HOW_SSDP, prod[0] ? prod : nullptr, false);
    } else {
        nsAddDev(mac, 0, NS_HOW_SSDP);               // mark S flag for the DLNA service
    }
    nsOrSvc(mac, svc);
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
        int ihl = (p[0] & 0x0F) * 4;                 // Stage 1b: dig into UDP
        if (ihl >= 20 && p[9] == 17 && n >= ihl + 8) {   // proto 17 = UDP
            const uint8_t* udp = p + ihl;
            uint16_t sport = (udp[0] << 8) | udp[1];
            uint16_t dport = (udp[2] << 8) | udp[3];
            const uint8_t* ud = udp + 8; int udlen = n - ihl - 8;
            if (udlen > 0 && (sport == 67 || dport == 67))         // DHCP (both dirs)
                nsParseDHCP(ud, udlen);
            else if (udlen > 0 && (sport == 5353 || dport == 5353)) // mDNS
                nsParseMDNS(mac, ud, udlen);
            else if (udlen > 0 && (sport == 1900 || dport == 1900)) // SSDP/UPnP
                nsParseSSDP(mac, ud, udlen);
        }
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
            f.println("time,mac,ip,name,vendor,type,how,services");
            for (int i = 0; i < s_devN; i++) {
                NsDev& d = s_dev[i];
                char svcb[96]; nsSvcStr(d.svc, svcb, sizeof(svcb));
                char line[256];
                snprintf(line, sizeof(line),
                         "%s,%02x:%02x:%02x:%02x:%02x:%02x,%u.%u.%u.%u,%s,%s,%s,%s%s%s%s%s,%s",
                         ts, d.mac[0],d.mac[1],d.mac[2],d.mac[3],d.mac[4],d.mac[5],
                         (d.ip>>24)&0xff,(d.ip>>16)&0xff,(d.ip>>8)&0xff,d.ip&0xff,
                         d.name[0] ? d.name : "",
                         d.vendor ? d.vendor : "?", d.type ? d.type : "?",
                         (d.how & NS_HOW_ARP) ? "A" : "", (d.how & NS_HOW_IP) ? "I" : "",
                         (d.how & NS_HOW_DHCP) ? "D" : "", (d.how & NS_HOW_MDNS) ? "M" : "",
                         (d.how & NS_HOW_SSDP) ? "S" : "", svcb);
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
// pixel columns (6px/char): index · IP · name/vendor · HOW flags · svc dot.
// The index column is the number to type for `ps ns<#>` / `pg ns<#>`.
#define NSX_IDX   4
#define NSX_IP    26
#define NSX_WHO   122
#define NSX_HOW   220
#define NSX_SVC   252

static void nsIpStr(uint32_t ip, char* b, int n) {
    snprintf(b, n, "%u.%u.%u.%u", (unsigned)((ip >> 24) & 0xff), (unsigned)((ip >> 16) & 0xff),
             (unsigned)((ip >> 8) & 0xff), (unsigned)(ip & 0xff));
}

static void nsDraw(int page, int sel) {
    auto& dm = displayManager;
    netspyHeader("client-isolation recon");
    dm.setTextColor(0x7BEF);
    dm.setCursor(NSX_IDX, outputY + LINE_HEIGHT * 2); dm.printText("#");
    dm.setCursor(NSX_IP,  outputY + LINE_HEIGHT * 2); dm.printText("IP");
    dm.setCursor(NSX_WHO, outputY + LINE_HEIGHT * 2); dm.printText("NAME / VENDOR");
    dm.setCursor(NSX_HOW, outputY + LINE_HEIGHT * 2); dm.printText("HOW");
    int total = (s_devN + NS_ROWS - 1) / NS_ROWS; if (total < 1) total = 1;
    if (page >= total) page = total - 1;
    for (int r = 0; r < NS_ROWS; r++) {
        int idx = page * NS_ROWS + r;
        int y = outputY + LINE_HEIGHT * (3 + r);
        if (idx >= s_devN) continue;
        NsDev& d = s_dev[idx];
        bool s = (r == sel);
        if (s) dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, 0x0010);  // dark-blue bar
        // index (type this for ps ns<#> / pg ns<#>)
        char nb[4]; snprintf(nb, sizeof(nb), "%d", idx);
        dm.setCursor(NSX_IDX, y); dm.setTextColor(s ? TFT_YELLOW : TFT_DARKGREY); dm.printText(nb);
        // IP
        char ipb[16]; nsIpStr(d.ip, ipb, sizeof(ipb));
        dm.setCursor(NSX_IP, y); dm.setTextColor(s ? TFT_YELLOW : TFT_WHITE); dm.printText(ipb);
        // name (cyan) or vendor (grey)
        const char* who = d.name[0] ? d.name : (d.vendor ? d.vendor : "?");
        uint16_t wc = s ? TFT_YELLOW : (d.name[0] ? TFT_CYAN : (d.vendor ? 0xC618 : TFT_DARKGREY));
        char wb[18]; snprintf(wb, sizeof(wb), "%.16s", who);
        dm.setCursor(NSX_WHO, y); dm.setTextColor(wc); dm.printText(wb);
        // HOW flags
        char fl[6] = { (char)((d.how & NS_HOW_ARP)  ? 'A' : ' '),
                       (char)((d.how & NS_HOW_IP)   ? 'I' : ' '),
                       (char)((d.how & NS_HOW_DHCP) ? 'D' : ' '),
                       (char)((d.how & NS_HOW_MDNS) ? 'M' : ' '),
                       (char)((d.how & NS_HOW_SSDP) ? 'S' : ' '), 0 };
        dm.setCursor(NSX_HOW, y); dm.setTextColor(s ? TFT_YELLOW : 0x6FE8); dm.printText(fl);
        // service marker
        if (d.svc) { dm.setCursor(NSX_SVC, y); dm.setTextColor(s ? TFT_YELLOW : TFT_CYAN); dm.printText("+"); }
    }
    char foot[64];
    snprintf(foot, sizeof(foot), "dev:%d pg%d/%d ent=info p=ping o=port s=save q",
             s_devN, page + 1, total);
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText(foot);
}

// ── device detail overlay (Enter / [i]) ─────────────────────────────────────────
static void nsDetailRow(int y, const char* label, const char* val, uint16_t vc) {
    auto& dm = displayManager;
    dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, y);  dm.printText(label);
    dm.setTextColor(vc);           dm.setCursor(72, y); dm.printText(val);
}

static void nsDetail(int idx) {
    auto& dm = displayManager;
    auto draw = [&]() {
        if (dm.isBlocked()) return;
        netspyHeader("device detail");
        NsDev& d = s_dev[idx];
        int y = outputY + LINE_HEIGHT * 2;
        char b[80];
        snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x",
                 d.mac[0],d.mac[1],d.mac[2],d.mac[3],d.mac[4],d.mac[5]);
        nsDetailRow(y, "MAC", b, TFT_WHITE); y += LINE_HEIGHT;
        nsIpStr(d.ip, b, sizeof(b)); nsDetailRow(y, "IP", b, TFT_WHITE); y += LINE_HEIGHT;
        nsDetailRow(y, "Name", d.name[0] ? d.name : "-", d.name[0] ? TFT_CYAN : TFT_DARKGREY); y += LINE_HEIGHT;
        snprintf(b, sizeof(b), "%s (%s)", d.vendor ? d.vendor : "?", d.type ? d.type : "?");
        nsDetailRow(y, "Vendor", b, TFT_WHITE); y += LINE_HEIGHT;
        b[0] = '\0';
        if (d.how & NS_HOW_ARP)  strncat(b, "ARP ",  sizeof(b)-strlen(b)-1);
        if (d.how & NS_HOW_IP)   strncat(b, "IPv4 ", sizeof(b)-strlen(b)-1);
        if (d.how & NS_HOW_DHCP) strncat(b, "DHCP ", sizeof(b)-strlen(b)-1);
        if (d.how & NS_HOW_MDNS) strncat(b, "mDNS ", sizeof(b)-strlen(b)-1);
        if (d.how & NS_HOW_SSDP) strncat(b, "SSDP ", sizeof(b)-strlen(b)-1);
        nsDetailRow(y, "Seen", b, 0x6FE8); y += LINE_HEIGHT;
        nsSvcStr(d.svc, b, sizeof(b));
        nsDetailRow(y, "Svc", b[0] ? b : "none", b[0] ? TFT_CYAN : TFT_DARKGREY);
        dm.setTextColor(TFT_DARKGREY); dm.setCursor(6, 230); dm.printText("any key: back");
    };
    draw();
    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k || tb == TBALL_CLICK) break;            // any key / click returns
        if (LockScreenManager::getInstance().consumeJustUnlocked()) draw();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// Probe the selected device with ps/pg. These use normal TCP/ICMP, so we
// suspend promiscuous sniffing for the duration, then resume — the device
// table (s_dev) is static and preserved. No-op if the row has no IP yet.
static void nsProbe(int idx, bool portScan) {
    if (idx < 0 || idx >= s_devN) return;
    NsDev& d = s_dev[idx];
    if (!d.ip) return;                               // need an IP to probe
    char ipb[16]; nsIpStr(d.ip, ipb, sizeof(ipb));
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    if (portScan) networkScanner.topPortScan(ipb);   // both take over the screen
    else          networkScanner.pingHost(ipb);      // until the user quits them
    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(nsCb);
    esp_wifi_set_promiscuous(true);
}

static void netspyDiscover() {
    auto& dm = displayManager;
    const uint8_t* bm = WiFi.BSSID();
    if (!bm) { netspyHeader("recon"); dm.setTextColor(TFT_RED); dm.println("No BSSID (not associated)."); dm.printCommandScreen(); return; }
    memcpy(s_bssid, bm, 6);
    s_devN = 0; s_head = s_tail = 0;

    nsDraw(0, 0);

    wifi_promiscuous_filter_t flt = {}; flt.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(nsCb);
    esp_wifi_set_promiscuous(true);

    int page = 0, sel = 0; uint32_t lastDraw = 0; bool run = true;
    while (run) {
        // drain capture ring → parse
        while (s_tail != s_head) {
            NsRing e;
            memcpy(&e, (const void*)&s_ring[s_tail], sizeof(e));
            s_tail = (uint8_t)((s_tail + 1) % NS_RING);
            nsParse(e.mac, e.pl, e.len);
        }
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        int pageCount = s_devN - page * NS_ROWS; if (pageCount > NS_ROWS) pageCount = NS_ROWS;

        if (k == 'q' || k == 'Q') { run = false; break; }
        else if (k == 'c' || k == 'C') { s_devN = 0; sel = 0; page = 0; lastDraw = 0; }
        else if (k == 's' || k == 'S') { nsSave(); vTaskDelay(pdMS_TO_TICKS(1200)); lastDraw = 0; }
        else if (k == 'l' || k == 'L') { page++; sel = 0; lastDraw = 0; }
        else if (k == 'a' || k == 'A') { if (page > 0) { page--; sel = 0; } lastDraw = 0; }
        else if ((k == '\r' || k == '\n' || k == 'i' || k == 'I') && pageCount > 0) {
            nsDetail(page * NS_ROWS + sel); lastDraw = 0;   // Enter / i = detail
        }
        else if ((k == 'p' || k == 'P') && pageCount > 0) { nsProbe(page * NS_ROWS + sel, false); lastDraw = 0; }
        else if ((k == 'o' || k == 'O') && pageCount > 0) { nsProbe(page * NS_ROWS + sel, true);  lastDraw = 0; }
        else if (tb == TBALL_DOWN) { if (sel < pageCount - 1) { sel++; lastDraw = 0; } }
        else if (tb == TBALL_UP)   { if (sel > 0)             { sel--; lastDraw = 0; } }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
        if (!dm.isBlocked() && millis() - lastDraw >= 1000) { nsDraw(page, sel); lastDraw = millis(); }
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
