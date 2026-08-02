#ifndef NETWORK_SCANNER_H
#define NETWORK_SCANNER_H

#include <WiFi.h>
#include "display_manager.h"

class NetworkScanner {
public:
    NetworkScanner(DisplayManager& displayManager);
    void networkDiscovery();
    void showHostResults();
    void networkPortScan(char* args);
    void topPortScan(char* args);
    void pingHost(char* args);

private:
    DisplayManager& displayManager;
    void performPortScan(const IPAddress& targetIP, int startPort, int endPort);
};

// Resolve a target token (ip | nd# | ns#) into an IP. Shared by ps/pg and arpspoof.
bool resolveNetTarget(const String& tok, IPAddress& out);

#endif
