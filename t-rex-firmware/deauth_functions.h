#ifndef DEAUTH_FUNCTIONS_H
#define DEAUTH_FUNCTIONS_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "wifi_functions.h"

class DeauthAttack {
public:
    DeauthAttack(DisplayManager& displayManager, WiFiFunctions& wifiFunctions);
    void start(char* args);
    void sendBroadcastBurst(const uint8_t* bssid);

private:
    DisplayManager& displayManager;
    WiFiFunctions&  wifiFunctions;

    bool   parseMac(const char* str, uint8_t* mac);
    void   buildDeauthFrame(uint8_t* frame, const uint8_t* da, const uint8_t* sa, const uint8_t* bssid);
    void   sendDeauthFrames(const uint8_t* bssid, const uint8_t* client, int channel, bool directed);
    String macStr(const uint8_t* mac);
};

#endif // DEAUTH_FUNCTIONS_H
