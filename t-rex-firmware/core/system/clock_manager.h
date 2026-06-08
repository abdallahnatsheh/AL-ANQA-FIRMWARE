#ifndef CLOCK_MANAGER_H
#define CLOCK_MANAGER_H

#include <stddef.h>
#include <stdint.h>

// ClockManager — local-time wall clock with two sync sources:
//   1. NTP  — when WiFi connected; uses configured POSIX TZ string for local time
//   2. GPS  — when WiFi not connected (T-Deck Plus only); gives UTC, TZ applied locally
//
// Priority: NTP wins whenever WiFi is connected (more accurate, handles DST).
//           GPS fills in when offline.
//
// TZ config: POSIX TZ string stored in /clock.conf (key tz=...).
//   Set via `tz +3`, `tz -5:30`, or full POSIX string `tz IST-2IDT,M3.4.4/2,M10.5.0/2`.
//   Default: UTC0 (UTC, no offset).
//
// All getters return LOCAL time after TZ is applied.

class ClockManager {
public:
    static ClockManager& instance();

    void init();         // load TZ from SD, apply TZ env; call after sdCardManager.begin()
    void update();       // poll GPS / trigger NTP; call from main loop (rate-limited)

    bool        isValid()    const { return _valid; }
    const char* tzStr()      const { return _tzStr; }

    void getTimeStr  (char* buf, size_t len) const;  // "HH:MM:SS"  or "--:--:--"
    void getDateStr  (char* buf, size_t len) const;  // "YYYY-MM-DD" or "----/--/--"
    void getShortTime(char* buf, size_t len) const;  // "HH:MM"     or "--:--"
    void getTimestamp(char* buf, size_t len) const;  // "YYYY-MM-DD HH:MM:SS" or ""

    void setTZ(const char* posixStr);  // apply + save new TZ
    bool loadConfig();
    bool saveConfig();

private:
    ClockManager() = default;
    void applyTZ();    // setenv + tzset

    bool     _valid       = false;
    uint32_t _lastSyncMs  = 0;
    uint32_t _lastBarMs   = 0;   // separate timer for status bar refresh (3 s)
    bool     _ntpStarted  = false;
    char     _tzStr[64]   = "UTC0";
};

// Command handler registered as "tz/tz"
void runTzCmd(char* args);

#endif // CLOCK_MANAGER_H
