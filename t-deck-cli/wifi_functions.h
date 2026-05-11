#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <WiFi.h>
#include "display_manager.h"

struct NetworkEntry {
    char    ssid[33];
    int     rssi;
    uint8_t bssid[6];
    int     channel;
    bool    isOpen;
};

class WiFiFunctions {
public:
    WiFiFunctions(DisplayManager& displayManager);
    void scanWiFiNetworks();
    void showWiFiResults();
    void connectToWiFiCommand(char* args);
    void clearAllWiFiCredentials();
    bool getNetworkInfo(int index, uint8_t* bssidOut, int* channelOut);
    bool getNetworkSSID(int index, char* ssidOut) const;
    int  getNetworkCount() const;
    bool isScanDone() const;
    void refreshHiddenCache();
    bool isHiddenKnown(const uint8_t* bssid) const;

private:
    DisplayManager& displayManager;
    int  numberOfNetworks    = 0;
    bool networkScanExecuted = false;

    String readPassword();
    void   storeWiFiCredentials(const String& ssid, const String& password);
    String getWiFiPassword(const String& ssid);
};

#endif
