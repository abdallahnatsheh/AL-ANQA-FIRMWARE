// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef WGUARD_H
#define WGUARD_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "wifi_functions.h"

#define WG_RING_SIZE    32
#define WG_EVENT_MAX     8
#define WG_CTR_MAX      16
#define WG_BSSID_MAX    32   // ISR beacon flood tracking

struct WgFrame {
    uint8_t  subtype;   // 0xFF=EAPOL data, 0xFE=beacon flood trigger, 8=Evil Twin beacon
    uint8_t  src[6];
    uint8_t  dst[6];
    char     ssid[33];
    uint8_t  eapolMsg;
    int8_t   rssi;
};

struct WgEvent {
    uint32_t ts;
    uint8_t  sev;       // 0=info 1=warning 2=critical
    char     msg[44];
};

struct WgCounter {
    uint8_t  mac[6];
    uint32_t count;
    uint32_t winStart;
};

class WGuard {
public:
    WGuard(DisplayManager& dm, WiFiFunctions& wf);
    void start(char* args);
    void beginBackground(char* args);
    void pollBackground();
    void stopBackground();
    void enterView();
    bool isBackground() const { return _bgMode; }

private:
    DisplayManager& _dm;
    WiFiFunctions&  _wf;

    uint8_t  _bssid[6];
    char     _ssid[33];
    int      _channel;

    uint32_t _cntBeacons;
    uint32_t _cntProbes;
    uint32_t _cntAuths;
    uint32_t _cntDeauths;
    uint32_t _cntEapols;

    uint8_t  _maxSev;

    WgEvent  _events[WG_EVENT_MAX];
    uint8_t  _evHead;
    uint8_t  _evCount;

    WgCounter _deauthCtr[WG_CTR_MAX];
    uint8_t   _deauthCtrN;
    WgCounter _probeCtr[WG_CTR_MAX];
    uint8_t   _probeCtrN;

    uint32_t _authFloodStart;
    uint8_t  _authMacs[32][6];
    uint8_t  _authMacN;

    uint32_t _deauthBurstStart;
    uint32_t _deauthBurstCount;

    uint8_t  _recentProbers[WG_CTR_MAX][6];
    uint8_t  _recentProberN;
    uint32_t _recentProberWindow;

    uint8_t  _evilTwinSeen[8][6];
    uint8_t  _evilTwinN;

    bool     _bgMode       = false;
    uint32_t _popupUntil   = 0;
    uint32_t _lastBgPoll   = 0;
    uint8_t  _lastBgHead   = 0;   // tracks _evHead, not _evCount — avoids cap-at-8 deaf bug

    void run(const uint8_t* bssid, int channel, const char* ssid);
    void runUI();          // blocking UI loop — shared by run() and enterView()
    void processFrame(const WgFrame& f);
    void addEvent(uint8_t sev, const char* msg);
    WgCounter* findOrAdd(WgCounter* table, uint8_t& cnt, const uint8_t* mac);
    uint32_t   rollCount(WgCounter* ctr, uint32_t windowMs, uint32_t now);

    static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static void IRAM_ATTR extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype, char* out);
    static void IRAM_ATTR enqueue(uint8_t sub, const uint8_t* src, const uint8_t* dst,
                                  const char* ssid, uint8_t eapolMsg, int8_t rssi);

    static volatile WgFrame  s_ring[WG_RING_SIZE];
    static volatile uint8_t  s_head;
    static volatile uint8_t  s_tail;
    static volatile uint8_t  s_bssid[6];
    static volatile char     s_ssid[33];
    static volatile bool     s_active;
    static volatile uint32_t s_isrBeacons;
    static volatile uint8_t  s_bssidSeen[WG_BSSID_MAX][6];
    static volatile uint8_t  s_bssidSeenN;
    static volatile uint32_t s_bssidFloodStart;
    static volatile bool     s_bssidFloodFired;
};

#endif // WGUARD_H
