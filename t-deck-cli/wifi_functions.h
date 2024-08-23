#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include <WiFi.h>
#include "display_manager.h"

class WiFiFunctions {

String HOST_NAME = "T-DECk";

public:
    WiFiFunctions(DisplayManager& displayManager);
    void connectToWiFiCommand(char* args);
    void scanWiFiNetworks();
    void clearAllWiFiCredentials();
    void networkDiscovery();
    void networkPortScan(char* args);
private:
    DisplayManager& displayManager;
    int numberOfNetworks = 0;
    bool networkScanExecuted = false;
    bool connectedToNetwork = false;
    String readPassword();
    void storeWiFiCredentials(const String& ssid, const String& password);
    String getWiFiPassword(const String& ssid);
    void pingScan(const IPAddress& gatewayIP, const IPAddress& subnetMask);
    void performPortScan(const IPAddress& targetIP, int startPort, int endPort);
    bool performPortCheck(const IPAddress& ip, int port);
};

#endif
