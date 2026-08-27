/**
 * @file   tpager_rtc.h
 * @brief  PCF85063A battery-backed RTC (T-Lora Pager) — persistent wall clock.
 *
 * The T-Pager has a PCF85063A on the shared I2C bus (0x51) with a backup cell, so
 * it keeps time across reboots and full power-off (ship mode). ClockManager uses it
 * as: (1) a boot seed — read it into the ESP32 system clock so the time is valid
 * immediately, before any NTP/GPS sync; and (2) a sink — whenever NTP or GPS gives a
 * fresh fix, write it back so the RTC stays accurate. Everything is stored as UTC;
 * ClockManager applies the timezone on read, exactly like the GPS path.
 *
 * All functions are safe to call on any board: they are real on the T-Pager and
 * no-ops (Read returns false) elsewhere, so ClockManager can call them unguarded.
 */
#pragma once
#include <time.h>

// Bring up the RTC (Wire is already begun in boardPowerOn()). Call once from init().
void tpagerRtcBegin();

// True if the RTC chip answered at begin(). T-Deck: always false.
bool tpagerRtcPresent();

// Read the RTC as a UTC epoch. Returns false if absent or holding an implausible
// time (year < 2020 = uninitialised / dead backup cell).
bool tpagerRtcReadEpoch(time_t& outUtc);

// Write a UTC epoch to the RTC (keeps it accurate from NTP/GPS). No-op if absent.
void tpagerRtcWriteEpoch(time_t utc);
