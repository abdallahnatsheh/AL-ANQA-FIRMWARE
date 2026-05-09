#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Pangodream_18650_CL.h>
#include "display_manager.h"
#include "utilities.h"

#define CONV_FACTOR 1.8
#define READS       20

class BatteryManager {
public:
    BatteryManager(DisplayManager& displayManager);

    void   printBatteryInfo();
    String getBatteryChargeLevel(float volts);

    int   getPct();
    float getVolts();

private:
    Pangodream_18650_CL bl;
    DisplayManager& displayManager;
    float voltageToPercentage(float voltage);
};

#endif // BATTERY_MANAGER_H