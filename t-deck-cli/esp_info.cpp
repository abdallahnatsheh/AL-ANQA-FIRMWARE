#include "esp_info.h"

ESPInfoPrinter::ESPInfoPrinter(DisplayManager& displayManager)
    : displayManager(displayManager), batteryManager(displayManager) {}

void ESPInfoPrinter::printESPInfo() {
    displayManager.newLinePrintLn("T-DECK Info:");
    // Print the IP address if connected to Wi-Fi
    if (WiFi.status() == WL_CONNECTED) {
        displayManager.newLinePrint("IP Address: ");
        displayManager.println(WiFi.localIP().toString());
    } else {
        displayManager.newLinePrint("IP Address: ");
        displayManager.println("not connected to network");
    }
    
    displayManager.newLinePrint("MAC Address: ");
    displayManager.println(WiFi.macAddress());

    // Print battery information
    batteryManager.printBatteryInfo();
}