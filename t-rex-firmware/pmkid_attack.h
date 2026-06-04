// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef PMKID_ATTACK_H
#define PMKID_ATTACK_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include "display_manager.h"
#include "wifi_functions.h"
#include "deauth_functions.h"

class PmkidAttack {
public:
    PmkidAttack(DisplayManager& dm, WiFiFunctions& wf, DeauthAttack& da);
    void start(char* args);

private:
    DisplayManager& _dm;
    WiFiFunctions&  _wf;
    DeauthAttack&   _da;

    void   run(const uint8_t* bssid, int channel, const char* ssid);
    void   crack();
    bool   tryPassword(const char* pwd, mbedtls_md_context_t* ctx, const mbedtls_md_info_t* sha1);
    bool   parseMac(const char* str, uint8_t* mac);
    String macStr(const uint8_t* m);
};

extern PmkidAttack pmkidAttack;

#endif // PMKID_ATTACK_H
