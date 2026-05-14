// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef WIFI_CREDS_H
#define WIFI_CREDS_H

#include <Arduino.h>

#define WIFIPASS_PATH "/wpa_supplicant.conf"

struct WifiNetwork {
    String ssid;
    String psk;
    bool   isHashed = false;  // psk is 64-char hex PMK — cannot connect on ESP32
    bool   hidden   = false;  // scan_ssid=1
    bool   open     = false;  // key_mgmt=NONE
    int    priority = 0;
    String bssid;             // informational comment only, not a pin
};

void        wifiPassCommand();
WifiNetwork getWifiNetwork(const String& ssid);
bool        appendWpaNetwork(const WifiNetwork& net);

#endif // WIFI_CREDS_H
