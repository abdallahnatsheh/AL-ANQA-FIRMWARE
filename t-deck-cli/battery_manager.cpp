#include "battery_manager.h"
#include <Arduino.h>

BatteryManager::BatteryManager(DisplayManager& displayManager)
    : bl(BOARD_BAT_ADC, CONV_FACTOR, READS), displayManager(displayManager) {}

void BatteryManager::printBatteryInfo() {
    displayManager.newLinePrint("Average value from pin: ");
    displayManager.println(bl.pinRead());
    displayManager.newLinePrint("Volts: ");
    displayManager.println(bl.getBatteryVolts());
    displayManager.newLinePrint("Charge level: ");
    displayManager.println(getBatteryChargeLevel(bl.getBatteryVolts()));
    displayManager.printCommandScreen();
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

