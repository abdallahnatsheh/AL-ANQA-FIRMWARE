#ifndef HIDDEN_SSID_H
#define HIDDEN_SSID_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "wifi_functions.h"
#include "deauth_functions.h"

class HiddenSSID {
public:
    HiddenSSID(DisplayManager& dm, WiFiFunctions& wf, DeauthAttack& da);
    void start(char* args);

private:
    DisplayManager& _dm;
    WiFiFunctions&  _wf;
    DeauthAttack&   _da;

    bool   parseMac(const char* str, uint8_t* mac);
    String macStr(const uint8_t* m);
    void   run(const uint8_t* bssid, int channel, bool silent);
};

#endif // HIDDEN_SSID_H
