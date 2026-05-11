// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "mac_changer.h"
#include <WiFi.h>
#include <SD.h>
#include "esp_wifi.h"

extern DisplayManager displayManager;

#define CFG_PATH "/macchanger.cfg"

MacChanger& MacChanger::getInstance() {
    static MacChanger instance;
    return instance;
}

static void fmtMac(const uint8_t* m, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static bool parseMac(const char* str, uint8_t* out) {
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &out[0],&out[1],&out[2],&out[3],&out[4],&out[5]) == 6;
}

void MacChanger::randomMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(esp_random() & 0xFF);
    mac[0] = (mac[0] & 0xFE) | 0x02; // locally-administered, unicast
}

void MacChanger::applyMac(const uint8_t* mac) {
    // esp_wifi_set_mac() requires the interface to be stopped first.
    // After esp_wifi_stop() the driver still owns the NVS/mode state, so we call
    // esp_wifi_start() directly rather than going through WiFi.mode() (which is a
    // no-op when the mode register hasn't changed).
    if (WiFi.getMode() == WIFI_MODE_NULL)
        WiFi.mode(WIFI_STA);   // first-time init via Arduino path
    esp_wifi_stop();
    if (esp_wifi_set_mac(WIFI_IF_STA, (uint8_t*)mac) == ESP_OK)
        memcpy(_currentMac, mac, 6);
    esp_wifi_start();   // restart directly — avoids Arduino mode-guard no-op
    delay(300);
}

void MacChanger::applyIfEnabled() {
    if (!_enabled) return;
    uint8_t mac[6];
    if (_useCustom) memcpy(mac, _customMac, 6);
    else            randomMac(mac);
    applyMac(mac);
}

String MacChanger::spoofedMacStr() const {
    char buf[18];
    fmtMac(_currentMac, buf);
    return String(buf);
}

void MacChanger::loadConfig() {
    if (!SD.exists(CFG_PATH)) return;
    File f = SD.open(CFG_PATH, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim(); val.trim();
        if (key == "enabled")  _enabled   = (val == "true");
        if (key == "mode")     _useCustom = (val == "custom");
        if (key == "custom")   parseMac(val.c_str(), _customMac);
    }
    f.close();
}

void MacChanger::saveConfig() {
    SD.remove(CFG_PATH);
    File f = SD.open(CFG_PATH, FILE_WRITE);
    if (!f) return;
    f.printf("enabled=%s\n", _enabled ? "true" : "false");
    f.printf("mode=%s\n",    _useCustom ? "custom" : "random");
    if (_useCustom) {
        char buf[18]; fmtMac(_customMac, buf);
        f.printf("custom=%s\n", buf);
    }
    f.close();
}

void MacChanger::begin() {
    loadConfig();
}

void MacChanger::printStatus() {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setCursor(10, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("MAC");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("CHANGER");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);  dm.printText("Status : ");
    dm.setTextColor(_enabled ? TFT_GREEN : TFT_RED);
    dm.println(_enabled ? "ON" : "OFF");

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);  dm.printText("Mode   : ");
    dm.setTextColor(TFT_CYAN);
    dm.println(_useCustom ? "custom" : "random");

    if (_useCustom) {
        char buf[18]; fmtMac(_customMac, buf);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.printText("Custom : ");
        dm.setTextColor(TFT_YELLOW); dm.println(buf);
    }

    // Show real hardware MAC
    uint8_t real[6]; esp_read_mac(real, ESP_MAC_WIFI_STA);
    char realStr[18]; fmtMac(real, realStr);
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.printText("HW MAC : ");
    dm.setTextColor(0x7BEF); dm.println(realStr);

    if (_enabled && _currentMac[0] | _currentMac[1] | _currentMac[2] |
                    _currentMac[3] | _currentMac[4] | _currentMac[5]) {
        char cur[18]; fmtMac(_currentMac, cur);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.printText("Active : ");
        dm.setTextColor(TFT_GREEN); dm.println(cur);
    }

    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("mc on/off/random/set <mac>");
    dm.setTextColor(TFT_WHITE);
    dm.printCommandScreen();
}

void MacChanger::handleCommand(char* args) {
    auto& dm = displayManager;

    if (!args || !*args) { printStatus(); return; }

    String a(args); a.trim();

    if (a == "on") {
        _enabled = true;
        saveConfig();
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);
        dm.println("MAC changer ON — active on next scan/connect");
        dm.setTextColor(TFT_WHITE);
        dm.printCommandScreen();
        return;
    }

    if (a == "off") {
        _enabled = false;
        saveConfig();
        // restore real HW MAC — same stop/set/restart sequence as applyMac()
        uint8_t real[6]; esp_read_mac(real, ESP_MAC_WIFI_STA);
        if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
        esp_wifi_stop();
        esp_wifi_set_mac(WIFI_IF_STA, real);
        esp_wifi_start();
        delay(300);
        memset(_currentMac, 0, 6);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        dm.println("MAC changer OFF — HW MAC restored");
        dm.setTextColor(TFT_WHITE);
        dm.printCommandScreen();
        return;
    }

    if (a == "random") {
        _useCustom = false;
        _enabled   = true;
        saveConfig();
        applyIfEnabled();
        char buf[18]; fmtMac(_currentMac, buf);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);  dm.printText("New MAC: ");
        dm.setTextColor(TFT_YELLOW); dm.println(buf);
        dm.setTextColor(TFT_WHITE);
        dm.printCommandScreen();
        return;
    }

    if (a.startsWith("set ")) {
        String macStr = a.substring(4); macStr.trim();
        uint8_t mac[6];
        if (!parseMac(macStr.c_str(), mac)) {
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_RED);
            dm.println("Bad format — use: mc set XX:XX:XX:XX:XX:XX");
            dm.setTextColor(TFT_WHITE);
            dm.printCommandScreen();
            return;
        }
        memcpy(_customMac, mac, 6);
        _useCustom = true;
        _enabled   = true;
        saveConfig();
        applyMac(_customMac);
        char buf[18]; fmtMac(_currentMac, buf);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);  dm.printText("Custom MAC set: ");
        dm.setTextColor(TFT_YELLOW); dm.println(buf);
        dm.setTextColor(TFT_WHITE);
        dm.printCommandScreen();
        return;
    }

    printStatus();
}
