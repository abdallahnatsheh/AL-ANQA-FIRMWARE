// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// SettingsApp — Settings menu → Wi-Fi page (on/off · scan · connect). Reuses the
// same layer as sw/cw: WiFi.scan/begin, NVS "wifi" namespace, wifi_creds. See home_app.h.

#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "home_app.h"
#include "home_widgets.h"
#include <Arduino.h>
#include <vector>

class SettingsApp : public HomeApp {
public:
    explicit SettingsApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Settings"; }
    void onEnter() override;
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;
    Nav  onTrackball(TrackballEvent) override;
    Nav  onKey(char) override;
private:
    struct WNet { char ssid[33]; int rssi; bool open; uint8_t bssid[6]; };
    enum Page { MENU, WIFI };
    enum WifiSub { MAIN, PASSWORD };
    void drawMenu();
    void drawWifiPage();
    void drawPassword();
    void wfToggle();
    void wfScan();
    void wfConnect(const char* pass);
    void wfStartConnect(int idx);
    bool savedPassword(const char* ssid, String& out);
    void saveCreds(const char* ssid, const char* pass, bool open, const uint8_t* bssid);
    void msg(const char* m, uint16_t c);
    Page    _page = MENU;
    WifiSub _sub  = MAIN;
    bool    _on   = false;
    std::vector<WNet> _nets;
    int     _sel = 0, _scroll = 0;
    char    _pass[65] = {}; int _passLen = 0;
    char    _tgtSsid[33] = {}; bool _tgtOpen = false; uint8_t _tgtBssid[6] = {};
    uint32_t _msgMs = 0; char _msgText[40] = {}; uint16_t _msgCol = 0;
};

#endif // APP_SETTINGS_H
