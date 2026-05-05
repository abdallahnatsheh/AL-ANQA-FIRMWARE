#ifndef NETWORK_SCANNER_H
#define NETWORK_SCANNER_H

#include <WiFi.h>
#include "display_manager.h"

class NetworkScanner {
public:
    NetworkScanner(DisplayManager& displayManager);
    void networkDiscovery();
    void networkPortScan(char* args);
    void topPortScan(char* args);
    void pingHost(char* args);

private:
    DisplayManager& displayManager;
    void performPortScan(const IPAddress& targetIP, int startPort, int endPort);
};

#endif
