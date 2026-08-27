#include "battery_manager.h"
#include <Arduino.h>

#if defined(BOARD_TPAGER)
// T-Pager fuel-gauge backend: BQ27220 over the shared I2C bus (SensorLib).
// ⚠️ HW-VERIFY: the isCharging() current-sign convention.
#include <Wire.h>
#include <GaugeBQ27220.hpp>
static GaugeBQ27220 s_gauge;
static bool     s_gaugeReady    = false;
static bool     s_gaugeRefreshed = false;
static uint32_t s_lastRefreshMs = 0;
// Ensure the gauge is up and its register block is refreshed at most once per
// second — getVolts()/getPct()/isCharging() all call this, so one status-bar draw
// that reads all three does a single I2C register-block read, not three (the Wire
// bus is shared with the TCA8418 keyboard).
static bool gaugeReady() {
    if (!s_gaugeReady) s_gaugeReady = s_gauge.begin(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (s_gaugeReady && (!s_gaugeRefreshed || millis() - s_lastRefreshMs >= 1000)) {
        s_gauge.refresh();
        s_lastRefreshMs  = millis();
        s_gaugeRefreshed = true;
    }
    return s_gaugeReady;
}

// BQ25896 charger status register REG0B (raw Wire, same pattern as ship-mode in
// board_power.cpp). Bits [4:3] CHRG_STAT: 0=not charging, 1=pre-charge, 2=fast
// charge, 3=charge done. Bit [2] PG_STAT: power good (valid VBUS present).
// Throttled to 1s — the bus is shared with the keyboard/gauge. Returns 0xFF if the
// charger never answered (treated as "unknown" by callers).
static uint8_t  s_chgReg0B  = 0xFF;
static uint32_t s_chgLastMs = 0;
static bool     s_chgEver   = false;
static uint8_t chargerReg0B() {
    if (!s_chgEver || millis() - s_chgLastMs >= 1000) {
        Wire.beginTransmission(TPAGER_I2C_ADDR_BQ25896);
        Wire.write(0x0B);
        if (Wire.endTransmission(false) == 0 &&
            Wire.requestFrom((int)TPAGER_I2C_ADDR_BQ25896, 1) == 1) {
            s_chgReg0B = Wire.read();
            s_chgEver  = true;
        }
        s_chgLastMs = millis();
    }
    return s_chgEver ? s_chgReg0B : 0xFF;
}
#endif

BatteryManager::BatteryManager(DisplayManager& displayManager)
#if defined(BOARD_TPAGER)
    : displayManager(displayManager) {}
#else
    : bl(BOARD_BAT_ADC, CONV_FACTOR, READS), displayManager(displayManager) {}
#endif

float BatteryManager::getVolts() {
#if defined(BOARD_TPAGER)
    if (!gaugeReady()) return 0.0f;
    return s_gauge.getVoltage() / 1000.0f;   // BQ27220 reports mV
#else
    return bl.getBatteryVolts();
#endif
}

void BatteryManager::printBatteryInfo() {
#if defined(BOARD_TPAGER)
    // Real SOC from the BQ27220 gauge + real charge state from the BQ25896 charger.
    float       volts = getVolts();
    int         pct   = getPct();
    ChargeState cs    = getChargeState();
    uint16_t color = pct >= 60 ? TFT_GREEN : (pct >= 30 ? TFT_YELLOW : TFT_RED);

    char buf[40];
    snprintf(buf, sizeof(buf), "%.2fV", volts);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText(buf);

    if (cs == CHG_CHARGING) {
        snprintf(buf, sizeof(buf), "  CHG %d%%", pct);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println(buf);
    } else if (cs == CHG_FULL) {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("  FULL");
    } else {
        snprintf(buf, sizeof(buf), "  %d%%", pct);
        displayManager.setTextColor(color);
        displayManager.println(buf);
    }
#else
    float volts   = getVolts();       // T-Deck ADC
    bool  chg     = volts > 4.5f;
    int   pct     = (int)voltageToPercentage(chg ? 4.2f : volts);  // clamp for display
    uint16_t color = pct >= 60 ? TFT_GREEN : (pct >= 30 ? TFT_YELLOW : TFT_RED);

    char buf[40];
    snprintf(buf, sizeof(buf), "%.2fV", volts);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText(buf);

    if (chg) {
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("  CHG");
    } else {
        snprintf(buf, sizeof(buf), "  %d%%", pct);
        displayManager.setTextColor(color);
        displayManager.println(buf);
    }
#endif
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
#if defined(BOARD_TPAGER)
    if (!gaugeReady()) return 0;
    return (int)s_gauge.getStateOfCharge();   // BQ27220 reports SOC % directly
#else
    return (int)voltageToPercentage(bl.getBatteryVolts());
#endif
}

ChargeState BatteryManager::getChargeState() {
#if defined(BOARD_TPAGER)
    // Definitive state from the BQ25896 charger (replaces the old gauge current-sign
    // guess). CHRG_STAT [4:3]: 1/2 = actively charging, 3 = charge complete.
    uint8_t r = chargerReg0B();
    if (r == 0xFF) return CHG_ON_BATTERY;        // charger unreadable → assume battery
    uint8_t chrg = (r >> 3) & 0x03;
    if (chrg == 0x01 || chrg == 0x02) return CHG_CHARGING;
    if (chrg == 0x03)                 return CHG_FULL;
    return CHG_ON_BATTERY;                        // 0 = not charging
#else
    return (bl.getBatteryVolts() > 4.5f) ? CHG_CHARGING : CHG_ON_BATTERY;
#endif
}

bool BatteryManager::isUsbPresent() {
#if defined(BOARD_TPAGER)
    uint8_t r = chargerReg0B();
    if (r == 0xFF) return false;
    return (r & 0x04) != 0;                       // PG_STAT (bit 2) = valid VBUS
#else
    return bl.getBatteryVolts() > 4.5f;
#endif
}

bool BatteryManager::isCharging() {
    return getChargeState() == CHG_CHARGING;
}

