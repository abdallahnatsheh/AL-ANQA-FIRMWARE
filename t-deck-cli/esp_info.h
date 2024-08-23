#ifndef ESP_INFO_H
#define ESP_INFO_H

#include <WiFi.h>
#include "display_manager.h"
#include "utilities.h"
#include "battery_manager.h"

class ESPInfoPrinter {
public:
    ESPInfoPrinter(DisplayManager& displayManager);
    void printESPInfo();

private:
    DisplayManager& displayManager;
    BatteryManager batteryManager;
};

#endif // ESP_INFO_H