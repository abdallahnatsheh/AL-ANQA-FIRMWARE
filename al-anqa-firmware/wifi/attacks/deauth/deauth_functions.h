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
    // both return the number of esp_wifi_80211_tx calls that did NOT return ESP_OK
    // (0 = every frame queued to the radio; >0 = the driver rejected some — e.g. a
    // channel/interface problem). Callers may ignore the return.
    uint32_t sendBroadcastBurst(const uint8_t* bssid);
    // directed deauth+disassoc to ONE client, both directions (AP<->STA). Quieter than
    // a broadcast burst (no storm signature) — used by pwn's stealth/targeted path.
    uint32_t sendDirectedBurst(const uint8_t* bssid, const uint8_t* client);

private:
    DisplayManager& displayManager;
    WiFiFunctions&  wifiFunctions;

    bool   parseMac(const char* str, uint8_t* mac);
    void   buildDeauthFrame(uint8_t* frame, const uint8_t* da, const uint8_t* sa, const uint8_t* bssid);
    void   sendDeauthFrames(const uint8_t* bssid, const uint8_t* client, int channel, bool directed);
    String macStr(const uint8_t* mac);
};

#endif // DEAUTH_FUNCTIONS_H
