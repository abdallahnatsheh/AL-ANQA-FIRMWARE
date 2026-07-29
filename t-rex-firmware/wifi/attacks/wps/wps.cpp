// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wps / wps — see wps.h. WPS-IE recon + candidate-PIN calculator + PBC connect.

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

// ── beacon WPS-IE capture ─────────────────────────────────────────────────────
static volatile bool     s_got = false;
static uint8_t           s_ie[256];
static volatile uint16_t s_ieLen = 0;
static uint8_t           s_bssid[6];

static void IRAM_ATTR wpsCapCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (s_got || t != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t* pk = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pk->payload;
    int len = pk->rx_ctrl.sig_len;
    if (len < 38) return;
    uint8_t subtype = (d[0] >> 4) & 0x0F;
    if (subtype != 8 && subtype != 5) return;          // beacon / probe-response
    if (memcmp(d + 16, s_bssid, 6) != 0) return;       // A3 = BSSID
    int pos = 24 + 12;                                  // 802.11 hdr + fixed params
    while (pos + 2 <= len) {
        uint8_t tid = d[pos], tlen = d[pos + 1];
        if (pos + 2 + tlen > len) break;
        if (tid == 0xDD && tlen >= 4 &&
            d[pos+2]==0x00 && d[pos+3]==0x50 && d[pos+4]==0xF2 && d[pos+5]==0x04) {
            int wlen = tlen - 4;                        // WPS data after OUI(3)+type(1)
            if (wlen > (int)sizeof(s_ie)) wlen = sizeof(s_ie);
            memcpy(s_ie, d + pos + 6, wlen);
            s_ieLen = (uint16_t)wlen;
            s_got = true;
            return;
        }
        pos += 2 + tlen;
    }
}

// ── WPS attribute (TLV, 2-byte BE type+len) lookup ────────────────────────────
static const uint8_t* wpsAttr(uint16_t type, int& outLen) {
    int p = 0;
    while (p + 4 <= (int)s_ieLen) {
        uint16_t t = (s_ie[p] << 8) | s_ie[p+1];
        uint16_t l = (s_ie[p+2] << 8) | s_ie[p+3];
        if (p + 4 + l > (int)s_ieLen) break;
        if (t == type) { outLen = l; return s_ie + p + 4; }
        p += 4 + l;
    }
    outLen = 0; return nullptr;
}
static String wpsStr(uint16_t type) {
    int l = 0; const uint8_t* v = wpsAttr(type, l);
    String s; for (int i = 0; i < l && i < 40; i++) s += (char)v[i];
    return s;
}
static int wpsByte(uint16_t type, int def) {
    int l = 0; const uint8_t* v = wpsAttr(type, l);
    return (v && l >= 1) ? v[0] : def;
}

// ── WPS PIN checksum + ComputePIN (from BSSID) ────────────────────────────────
static int wpsChecksum(uint32_t pin) {
    uint32_t accum = 0;
    while (pin) { accum += 3 * (pin % 10); pin /= 10; accum += (pin % 10); pin /= 10; }
    return (int)((10 - accum % 10) % 10);
}
static uint32_t computePin(const uint8_t mac[6]) {
    uint32_t nic = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    uint32_t p7  = nic % 10000000UL;
    return p7 * 10 + (uint32_t)wpsChecksum(p7);
}

static String macStr(const uint8_t m[6]) {
    char s[18]; snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X", m[0],m[1],m[2],m[3],m[4],m[5]);
    return String(s);
}

// ── resolve a scan index → bssid/channel/ssid ────────────────────────────────
static bool resolveIdx(int idx, uint8_t bssid[6], int& chan, char* ssid) {
    if (idx < 0 || idx >= wifiFunctions.getNetworkCount()) return false;
    if (!wifiFunctions.getNetworkInfo(idx, bssid, &chan)) return false;
    wifiFunctions.getNetworkSSID(idx, ssid);
    return true;
}

// ── recon: capture the beacon, parse the WPS IE, print + log + PIN calc ───────
static void wpsRecon(int idx) {
    DisplayManager& dm = displayManager;
    uint8_t bssid[6]; int chan; char ssid[33] = {0};
    if (!resolveIdx(idx, bssid, chan, ssid)) {
        dm.clearScreen(); dm.setTextColor(TFT_RED); dm.println("Bad index. Run `sw` first."); delay(1800); return;
    }
    memcpy(s_bssid, bssid, 6);
    s_got = false; s_ieLen = 0;

    dm.clearScreen();
    dm.setTextColor(TFT_CYAN); dm.println(String("[WPS] ") + ssid);
    dm.setTextColor(0x7BEF);   dm.println(macStr(bssid) + "  ch" + String(chan));
    dm.setTextColor(TFT_WHITE);dm.println("capturing beacon...");

    // park on the AP channel + sniff mgmt frames for its WPS IE
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t f; f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(wpsCapCb);
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
    uint32_t t0 = millis();
    while (!s_got && millis() - t0 < 4000) { if (inputHandler.getKeyboardInput()=='q') break; delay(30); }
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    if (!s_got) { dm.setTextColor(TFT_RED); dm.println("No WPS IE seen (out of range / not WPS)."); delay(2200); return; }

    // decode
    int ver    = wpsByte(0x104A, 0x10);
    int state  = wpsByte(0x1044, 0);
    int locked = wpsByte(0x1057, 0);
    int cmL = 0; const uint8_t* cm = wpsAttr(0x1008, cmL);
    uint16_t methods = (cm && cmL >= 2) ? ((cm[0]<<8)|cm[1]) : 0;
    String manuf = wpsStr(0x1021), model = wpsStr(0x1023), modelN = wpsStr(0x1024);
    String devN  = wpsStr(0x1011), serial = wpsStr(0x1042);
    uint32_t pin = computePin(bssid);

    String mstr;
    if (methods & 0x0080) mstr += "PBC ";
    if (methods & 0x0008) mstr += "Display ";
    if (methods & 0x0100) mstr += "Keypad ";
    if (methods & 0x0004) mstr += "Label ";
    if (mstr.isEmpty()) mstr = "-";

    dm.clearScreen();
    int y = 38;
    dm.setTextColor(TFT_CYAN);  dm.setCursor(6,y); dm.printText(String("[WPS] ")+ssid); y+=16;
    dm.setTextColor(0x7BEF);    dm.setCursor(6,y); dm.printText(macStr(bssid)+" ch"+String(chan)); y+=16;
    dm.setTextColor(TFT_WHITE);
    dm.setCursor(6,y); dm.printText(String("Ver ")+(ver==0x20?"2.0":"1.0")+"   State "+(state==2?"configured":"open")); y+=14;
    dm.setTextColor(locked?TFT_RED:TFT_GREEN);
    dm.setCursor(6,y); dm.printText(locked?"AP-SETUP-LOCKED (PIN would be refused)":"WPS not locked"); y+=14;
    dm.setTextColor(TFT_WHITE);
    dm.setCursor(6,y); dm.printText("Methods: "+mstr); y+=14;
    dm.setTextColor(0xC618);
    if (manuf.length())  { dm.setCursor(6,y); dm.printText("Mfr:   "+manuf); y+=13; }
    if (model.length())  { dm.setCursor(6,y); dm.printText("Model: "+model+" "+modelN); y+=13; }
    if (devN.length())   { dm.setCursor(6,y); dm.printText("Dev:   "+devN); y+=13; }
    if (serial.length()) { dm.setCursor(6,y); dm.printText("Ser:   "+serial); y+=13; }
    y+=4;
    dm.setTextColor(TFT_YELLOW);
    char pinbuf[16]; snprintf(pinbuf, sizeof(pinbuf), "%08lu", (unsigned long)pin);
    dm.setCursor(6,y); dm.printText(String("try PINs: ")+pinbuf+" 12345670"); y+=13;
    dm.setTextColor(0x7BEF);
    dm.setCursor(6,y); dm.printText("(PIN calc only - test w/ Reaver; wps pbc <#>)"); y+=16;

    // log
    File fp = SD.open(String(SD_DIR_WPS)+"/wps.csv", FILE_APPEND);
    if (fp) {
        fp.printf("%lu,%s,%s,%s,%s,%s,%s,%s,%s,%08lu\n", (unsigned long)millis(),
                  macStr(bssid).c_str(), ssid, ver==0x20?"2.0":"1.0",
                  locked?"locked":"open", mstr.c_str(), manuf.c_str(), model.c_str(),
                  devN.c_str(), (unsigned long)pin);
        fp.close();
    }
    dm.setTextColor(0x5AEB); dm.setCursor(6,214); dm.printText("saved /apps/wps/wps.csv - any key");
    while (inputHandler.getKeyboardInput()==0) delay(20);
}

// ── attack: WPS push-button connect (only works while AP button is active) ────
static void wpsPbc(int idx) {
    DisplayManager& dm = displayManager;
    uint8_t bssid[6]; int chan; char ssid[33] = {0};
    if (!resolveIdx(idx, bssid, chan, ssid)) {
        dm.clearScreen(); dm.setTextColor(TFT_RED); dm.println("Bad index. Run `sw` first."); delay(1800); return;
    }
    dm.clearScreen();
    dm.setTextColor(TFT_CYAN); dm.println(String("[WPS::PBC] ")+ssid);
    dm.setTextColor(0x7BEF);   dm.println("PRESS the AP's WPS button now.");
    dm.setTextColor(TFT_WHITE);dm.println("Trying push-button connect (120s)...");

    WiFi.mode(WIFI_STA);
    esp_wps_config_t cfg = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
    if (esp_wifi_wps_enable(&cfg) != ESP_OK) {
        dm.setTextColor(TFT_RED); dm.println("WPS enable failed."); delay(1800); return;
    }
    esp_wifi_wps_start(0);

    bool ok = false; uint32_t t0 = millis();
    int lastSec = -1;
    while (millis() - t0 < 120000) {
        if (WiFi.status() == WL_CONNECTED) { ok = true; break; }
        if (inputHandler.getKeyboardInput()=='q') break;
        int sec = (int)((120000 - (millis()-t0)) / 1000);
        if (sec != lastSec) {
            lastSec = sec;
            dm.fillRect(6, 96, SCREEN_WIDTH-12, 16, TFT_BLACK);
            dm.setCursor(6, 96); dm.setTextColor(0x7BEF);
            char b[24]; snprintf(b, sizeof(b), "waiting... %ds  [q]", sec); dm.printText(b);
        }
        delay(150);
    }
    esp_wifi_wps_disable();

    if (ok) {
        String s = WiFi.SSID(), p = WiFi.psk();
        dm.clearScreen(); dm.setTextColor(TFT_GREEN);
        dm.println("WPS PBC SUCCESS - credentials:");
        dm.setTextColor(TFT_WHITE);
        dm.println("SSID: " + s);
        dm.println("PSK:  " + (p.length() ? p : String("(open/none)")));
        File fp = SD.open(String(SD_DIR_WPS)+"/creds.csv", FILE_APPEND);
        if (fp) { fp.printf("%lu,%s,%s,%s\n", (unsigned long)millis(), macStr(bssid).c_str(), s.c_str(), p.c_str()); fp.close(); }
        dm.setTextColor(0x5AEB); dm.println("saved /apps/wps/creds.csv - any key");
        while (inputHandler.getKeyboardInput()==0) delay(20);
    } else {
        dm.setTextColor(TFT_RED); dm.println("No PBC connect (button not pressed / timeout)."); delay(2400);
    }
}

// ── entry ─────────────────────────────────────────────────────────────────────
void runWps(char* args) {
    DisplayManager& dm = displayManager;
    String a = args ? String(args) : ""; a.trim();

    // wps pbc <idx>
    if (a.startsWith("pbc")) {
        String rest = a.substring(3); rest.trim();
        if (rest.isEmpty()) { dm.clearScreen(); dm.setTextColor(TFT_YELLOW); dm.println("Usage: wps pbc <idx>"); delay(1800); return; }
        wpsPbc(rest.toInt()); return;
    }

    // wps <idx> → recon
    if (a.length() && isdigit((unsigned char)a[0])) { wpsRecon(a.toInt()); return; }

    // wps (no arg) → list WPS-flagged APs from the last sw scan
    dm.clearScreen();
    if (wifiFunctions.getNetworkCount() <= 0) {
        dm.setTextColor(TFT_YELLOW); dm.println("Run `sw` to scan first, then:");
        dm.setTextColor(TFT_WHITE);  dm.println("  wps <idx>       WPS recon + PIN calc");
        dm.println("  wps pbc <idx>   push-button connect");
        delay(2600); return;
    }
    dm.setTextColor(TFT_CYAN); dm.println("WPS-enabled APs (wps <#> for detail):");
    dm.setTextColor(TFT_WHITE);
    int n = wifiFunctions.getNetworkCount(), shown = 0;
    for (int i = 0; i < n; i++) {
        if (!wifiFunctions.getNetworkWps(i)) continue;
        char ssid[33] = {0}; wifiFunctions.getNetworkSSID(i, ssid);
        char line[48]; snprintf(line, sizeof(line), "[%2d] %s", i, ssid);
        dm.println(line); shown++;
    }
    if (!shown) { dm.setTextColor(0x7BEF); dm.println("(none flagged WPS in the last scan)"); }
    dm.setTextColor(0x5AEB); dm.println("any key");
    while (inputHandler.getKeyboardInput()==0) delay(20);
}
