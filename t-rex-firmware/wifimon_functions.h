#ifndef WIFIMON_FUNCTIONS_H
#define WIFIMON_FUNCTIONS_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"

#define MAX_TRACKED_NETS 24
#define PKT_RING_SIZE    32

struct PktSummary {
    uint8_t type;
    uint8_t subtype;
    int8_t  rssi;
    uint8_t channel;
    uint8_t src[6];
    uint8_t bssid[6];
    char    ssid[33];
};

struct NetEntry {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    uint32_t beacons;
};

class WiFiMonitor {
public:
    WiFiMonitor(DisplayManager& displayManager, SDCardManager& sdCardManager);
    void start(int fixedChannel = 0);

private:
    DisplayManager& displayManager;
    SDCardManager&  sdCardManager;

    uint32_t totalPkts;
    uint32_t beaconPkts;
    uint32_t probePkts;
    uint32_t dataPkts;
    uint32_t mgmtPkts;
    uint32_t ctrlPkts;

    NetEntry nets[MAX_TRACKED_NETS];
    int      netCount;
    bool     logging;
    int      currentChannel;
    int      displayPage;

    void     resetStats();
    void     drawDisplay();
    void     processRing();
    void     handlePacket(const PktSummary& pkt);
    void     trackNet(const PktSummary& pkt);
    void     logToSD(const PktSummary& pkt);
    void     setChannel(int ch);
    String   macStr(const uint8_t* mac);

    static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static void parseFrame(const uint8_t* data, uint16_t len, uint8_t ch, int8_t rssi, PktSummary& out);

public:
    static void IRAM_ATTR extractSSID(const uint8_t* data, uint16_t len, uint8_t subtype, char* ssidOut);

private:

    static volatile PktSummary ringBuf[PKT_RING_SIZE];
    static volatile uint8_t    ringHead;
    static volatile uint8_t    ringTail;
};

#endif // WIFIMON_FUNCTIONS_H
