#include "battery_manager.h"
#include <Arduino.h>

BatteryManager::BatteryManager(DisplayManager& displayManager)
    : bl(BOARD_BAT_ADC, CONV_FACTOR, READS), displayManager(displayManager) {}

float BatteryManager::getVolts() {
    return bl.getBatteryVolts();
}

void BatteryManager::printBatteryInfo() {
    float volts = bl.getBatteryVolts();
    int   pct   = (int)voltageToPercentage(volts);
    uint16_t color = pct >= 60 ? TFT_GREEN : (pct >= 30 ? TFT_YELLOW : TFT_RED);

    char buf[40];
    snprintf(buf, sizeof(buf), "%.2fV", volts);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText(buf);

    snprintf(buf, sizeof(buf), "  %d%%", pct);
    displayManager.setTextColor(color);
    displayManager.println(buf);
}

float BatteryManager::voltageToPercentage(float voltage) {
    float percentage = (voltage - 3.0) / (4.2 - 3.0) * 100;
    return percentage < 0 ? 0 : (percentage > 100 ? 100 : percentage);
}

String BatteryManager::getBatteryChargeLevel(float volts) {
    float percentage = voltageToPercentage(volts);
    return String(percentage, 1) + "%";
}

int BatteryManager::getPct() {
    return (int)voltageToPercentage(bl.getBatteryVolts());
}

