#ifndef ESP_INFO_H
#define ESP_INFO_H

#include <WiFi.h>
#include "display_manager.h"
#include "utilities.h"
#include "battery_manager.h"
#include "input_handling.h"

class ESPInfoPrinter {
public:
    ESPInfoPrinter(DisplayManager& displayManager);
    void printESPInfo();

private:
    DisplayManager& displayManager;
    BatteryManager  batteryManager;

    void drawPageSystem();
    void drawPageRadio();
    void drawPageHardware();
};

#endif // ESP_INFO_H
