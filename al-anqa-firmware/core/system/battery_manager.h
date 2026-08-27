#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "display_manager.h"
#include "utilities.h"
#if !defined(BOARD_TPAGER)
#include <Pangodream_18650_CL.h>   // T-Deck: ADC voltage divider on BOARD_BAT_ADC
#endif
// T-Pager: a BQ27220 I2C fuel gauge is used instead (backend in battery_manager.cpp).

#define CONV_FACTOR 1.8
#define READS       20

// Definitive charge state. On the T-Pager it comes from the BQ25896 charger
// (REG0B CHRG_STAT). On the T-Deck it's derived from the ADC voltage (>4.5V = USB),
// which has no distinct "full" state.
enum ChargeState { CHG_ON_BATTERY, CHG_CHARGING, CHG_FULL };

class BatteryManager {
public:
    BatteryManager(DisplayManager& displayManager);

    void   printBatteryInfo();
    String getBatteryChargeLevel(float volts);

    int          getPct();
    float        getVolts();
    bool         isCharging();          // true only while actively charging
    ChargeState  getChargeState();      // on-battery / charging / full
    bool         isUsbPresent();        // VBUS/PG present (T-Pager); T-Deck: charging heuristic

private:
#if !defined(BOARD_TPAGER)
    Pangodream_18650_CL bl;   // T-Deck ADC backend (T-Pager uses a file-static BQ27220)
#endif
    DisplayManager& displayManager;
    float voltageToPercentage(float voltage);
};

#endif // BATTERY_MANAGER_H