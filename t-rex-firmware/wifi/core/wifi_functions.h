#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <WiFi.h>
#include "display_manager.h"

// Security classification for the w3d (WPA3 downgrade) recon path.
enum WifiSec { WSEC_OPEN, WSEC_WEP, WSEC_WPA, WSEC_WPA2, WSEC_WPA3, WSEC_TD };

struct NetworkEntry {
    char    ssid[33];
    int     rssi;
    uint8_t bssid[6];
    int     channel;
    bool    isOpen;
    bool    wps;
    uint8_t sec;    // WifiSec — from the AP's authmode (WPA2_WPA3 = transition/downgradeable)
};

class WiFiFunctions {
public:
    WiFiFunctions(DisplayManager& displayManager);
    void runWifiManager(char* args);   // sw — WiFi manager: no arg = interactive, "on"/"off" = radio
    void showWiFiResults();
    void connectToWiFiCommand(char* args);
    void clearAllWiFiCredentials();
    bool forgetNetwork(const String& ssid);  // remove saved creds (NVS + wpa_supplicant.conf)
    bool getNetworkInfo(int index, uint8_t* bssidOut, int* channelOut);
    bool getNetworkSSID(int index, char* ssidOut) const;
    bool getNetworkOpen(int index) const;
    bool getNetworkWps(int index) const;   // WPS advertised in the last scan (used by wps)
    int  getNetworkSec(int index) const;   // WifiSec, or -1 if invalid (used by w3d)
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
    void   connectSelected(int idx);   // WiFi manager: connect to the highlighted row
    void   forgetSelected(int idx);    // WiFi manager: forget the highlighted row (with toast)
};

#endif
