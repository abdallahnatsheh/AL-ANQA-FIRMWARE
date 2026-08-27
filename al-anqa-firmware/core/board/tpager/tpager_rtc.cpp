/**
 * @file   tpager_rtc.cpp
 * @brief  PCF85063A RTC backend. See tpager_rtc.h.
 */
#include "tpager_rtc.h"
#include "board.h"

#if defined(BOARD_TPAGER)

#include <Wire.h>
#include <SensorPCF85063.hpp>
#include "pins.h"

static const time_t RTC_MIN_VALID = 1577836800L;   // 2020-01-01 UTC

static SensorPCF85063 s_rtc;
static bool           s_ok = false;

// Broken-down UTC time -> epoch (no TZ involved, unlike mktime). Same day-count
// approach as ClockManager's GPS helper.
static time_t utcToEpoch(int year, int mon, int day, int h, int m, int s) {
    static const uint8_t dpm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days = 0;
    for (int y = 1970; y < year; y++) {
        bool leap = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
        days += leap ? 366 : 365;
    }
    bool isLeap = (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
    for (int mo = 1; mo < mon; mo++) {
        days += dpm[mo - 1];
        if (mo == 2 && isLeap) days++;
    }
    days += day - 1;
    return (time_t)(days * 86400L + h * 3600L + m * 60L + s);
}

void tpagerRtcBegin() {
    s_ok = s_rtc.begin(Wire);   // shared bus, addr auto (PCF85063 default 0x51)
}

bool tpagerRtcPresent() { return s_ok; }

bool tpagerRtcReadEpoch(time_t& outUtc) {
    if (!s_ok) return false;
    RTC_DateTime dt = s_rtc.getDateTime();
    if (dt.getYear() < 2020 || dt.getMonth() < 1 || dt.getMonth() > 12 ||
        dt.getDay()  < 1    || dt.getDay()   > 31) return false;
    outUtc = utcToEpoch(dt.getYear(), dt.getMonth(), dt.getDay(),
                        dt.getHour(), dt.getMinute(), dt.getSecond());
    return outUtc >= RTC_MIN_VALID;
}

void tpagerRtcWriteEpoch(time_t utc) {
    if (!s_ok || utc < RTC_MIN_VALID) return;
    struct tm tmv;
    gmtime_r(&utc, &tmv);                 // epoch -> UTC broken-down
    s_rtc.setDateTime(RTC_DateTime(tmv)); // ctor does tm_year+1900 / tm_mon+1
}

#else   // ── T-Deck / T-Deck-Plus: no hardware RTC ──────────────────────────────

void tpagerRtcBegin() {}
bool tpagerRtcPresent() { return false; }
bool tpagerRtcReadEpoch(time_t&) { return false; }
void tpagerRtcWriteEpoch(time_t) {}

#endif
