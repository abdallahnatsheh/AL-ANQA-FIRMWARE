#include "clock_manager.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include "input_handling.h"
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>
#include <WiFi.h>
#include <SD.h>
#include <Preferences.h>

#ifdef BOARD_TDECK_PLUS
#include "gps_manager.h"
#endif

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;
extern InputHandling  inputHandler;

static const time_t MIN_VALID_EPOCH = 1577836800L;   // 2020-01-01 00:00:00 UTC

// ── Timezone presets ──────────────────────────────────────────────────────────

struct TzPreset { const char* label; const char* posix; };

static const TzPreset PRESETS[] = {
    { "UTC-11  American Samoa",          "UTC+11"                              },
    { "UTC-10  Hawaii",                  "HST10"                               },
    { "UTC-9   Alaska",                  "AKST9AKDT,M3.2.0,M11.1.0"           },
    { "UTC-8   US Pacific",              "PST8PDT,M3.2.0,M11.1.0"             },
    { "UTC-7   US Mountain",             "MST7MDT,M3.2.0,M11.1.0"             },
    { "UTC-6   US Central",              "CST6CDT,M3.2.0,M11.1.0"             },
    { "UTC-5   US Eastern",              "EST5EDT,M3.2.0,M11.1.0"             },
    { "UTC-4   Atlantic / Caracas",      "UTC+4"                               },
    { "UTC-3   Argentina / Sao Paulo",   "UTC+3"                               },
    { "UTC-1   Cape Verde / Azores",     "UTC+1"                               },
    { "UTC+0   London / Reykjavik",      "GMT0BST,M3.5.0/1,M10.5.0"           },
    { "UTC+1   Paris / Berlin",          "CET-1CEST,M3.5.0,M10.5.0/3"         },
    { "UTC+2   Cairo / South Africa",    "UTC-2"                               },
    { "UTC+2   Jerusalem (IST/IDT)",     "IST-2IDT,M3.4.4/2,M10.5.0/2"        },
    { "UTC+3   Moscow / Istanbul",       "UTC-3"                               },
    { "UTC+3   Nairobi / Riyadh",        "UTC-3"                               },
    { "UTC+3:30 Tehran",                 "UTC-3:30"                            },
    { "UTC+4   Dubai / Baku",            "UTC-4"                               },
    { "UTC+4:30 Kabul",                  "UTC-4:30"                            },
    { "UTC+5   Karachi / Islamabad",     "UTC-5"                               },
    { "UTC+5:30 New Delhi / Mumbai",     "UTC-5:30"                            },
    { "UTC+5:45 Kathmandu",              "UTC-5:45"                            },
    { "UTC+6   Dhaka / Almaty",          "UTC-6"                               },
    { "UTC+6:30 Yangon (Myanmar)",       "UTC-6:30"                            },
    { "UTC+7   Bangkok / Jakarta",       "UTC-7"                               },
    { "UTC+8   Beijing / Singapore",     "UTC-8"                               },
    { "UTC+9   Tokyo / Seoul",           "UTC-9"                               },
    { "UTC+9:30 Adelaide",               "ACST-9:30ACDT,M10.1.0,M4.1.0/3"     },
    { "UTC+10  Sydney / Melbourne",      "AEST-10AEDT,M10.1.0,M4.1.0/3"       },
    { "UTC+11  Noumea / Vladivostok",    "UTC-11"                              },
    { "UTC+12  Auckland / Fiji",         "NZST-12NZDT,M9.5.0,M4.1.0/3"        },
};
static const int N_PRESETS = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

// ── Singleton ─────────────────────────────────────────────────────────────────

ClockManager& ClockManager::instance() {
    static ClockManager inst;
    return inst;
}

// ── Init ──────────────────────────────────────────────────────────────────────

void ClockManager::init() {
    // SD takes priority; if absent or file missing, fall back to NVS
    if (!loadConfig()) {
        Preferences prefs;
        prefs.begin("clock", true);
        String v = prefs.getString("tz", "");
        prefs.end();
        if (v.length()) {
            strncpy(_tzStr, v.c_str(), sizeof(_tzStr) - 1);
            _tzStr[sizeof(_tzStr) - 1] = '\0';
        }
    }
    applyTZ();
}

void ClockManager::applyTZ() {
    setenv("TZ", _tzStr, 1);
    tzset();
}

// ── Config ────────────────────────────────────────────────────────────────────

bool ClockManager::loadConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/clock.conf", FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.length() || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        if (line.substring(0, eq) == "tz") {
            strncpy(_tzStr, line.substring(eq + 1).c_str(), sizeof(_tzStr) - 1);
            _tzStr[sizeof(_tzStr) - 1] = '\0';
        }
    }
    f.close();
    return true;
}

bool ClockManager::saveConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/clock.conf", FILE_WRITE);
    if (!f) return false;
    f.printf("tz=%s\n", _tzStr);
    f.close();
    return true;
}

// ── TZ apply ─────────────────────────────────────────────────────────────────

void ClockManager::setTZ(const char* posixStr) {
    strncpy(_tzStr, posixStr, sizeof(_tzStr) - 1);
    _tzStr[sizeof(_tzStr) - 1] = '\0';
    applyTZ();
    // Always persist to NVS — survives reboots even without an SD card
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putString("tz", _tzStr);
    prefs.end();
    if (_ntpStarted)
        configTzTime(_tzStr, "pool.ntp.org", "time.cloudflare.com");
}

// ── GPS epoch helper ──────────────────────────────────────────────────────────

#ifdef BOARD_TDECK_PLUS
static time_t gpsToEpoch(uint16_t yr, uint8_t mon, uint8_t day,
                          uint8_t h, uint8_t m, uint8_t s) {
    static const uint8_t dpm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days = 0;
    for (uint16_t y = 1970; y < yr; y++) {
        bool leap = (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
        days += leap ? 366 : 365;
    }
    bool isLeap = (yr % 4 == 0) && (yr % 100 != 0 || yr % 400 == 0);
    for (uint8_t mo = 1; mo < mon; mo++) {
        days += dpm[mo - 1];
        if (mo == 2 && isLeap) days++;
    }
    days += day - 1;
    return (time_t)(days * 86400L + h * 3600L + m * 60L + s);
}
#endif

// ── Update (main poll loop) ───────────────────────────────────────────────────

void ClockManager::update() {
    uint32_t now    = millis();
    bool     wifiUp = (WiFi.status() == WL_CONNECTED);

    // ── NTP start/stop — NOT throttled ───────────────────────────────────────
    // Reacts immediately to WiFi state changes; the 10s throttle below would
    // otherwise suppress configTzTime() when WiFi connects during a blocking loop.
    if (!_ntpStarted && wifiUp) {
        configTzTime(_tzStr, "pool.ntp.org", "time.cloudflare.com");
        _ntpStarted = true;
    } else if (_ntpStarted && !wifiUp) {
        _ntpStarted = false;
    }

    // ── Status bar refresh — 3-second timer ──────────────────────────────────
    // Keeps the clock, WiFi bars, battery, and GPS icon live during idle.
    // Skipped when blocked (locked) — refreshDuration() handles it every 1s there.
    if (!displayManager.isBlocked() && now - _lastBarMs >= 3000) {
        _lastBarMs = now;
        displayManager.updateStatusBar();
    }

    // ── Heavy checks — 10-second timer ───────────────────────────────────────
    if (now - _lastSyncMs < 10000) return;
    _lastSyncMs = now;

#ifdef BOARD_TDECK_PLUS
    if (!wifiUp) {
        GpsManager& gm = GpsManager::instance();
        if (gm.isRunning() && gm.timeValid() && gm.dateValid() &&
            gm.year() >= 2020 && gm.month() >= 1 && gm.month() <= 12 &&
            gm.day()  >= 1    && gm.day()   <= 31)
        {
            time_t epoch = gpsToEpoch(gm.year(), gm.month(), gm.day(),
                                       gm.hour(), gm.minute(), gm.second());
            struct timeval tv;
            tv.tv_sec  = (long)epoch;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
        }
    }
#endif

    if (!_valid && time(nullptr) > MIN_VALID_EPOCH)
        _valid = true;
}

// ── Getters (local time) ──────────────────────────────────────────────────────

void ClockManager::getTimeStr(char* buf, size_t len) const {
    if (!_valid) { snprintf(buf, len, "--:--:--"); return; }
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    snprintf(buf, len, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

void ClockManager::getDateStr(char* buf, size_t len) const {
    if (!_valid) { snprintf(buf, len, "----/--/--"); return; }
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    snprintf(buf, len, "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
}

void ClockManager::getShortTime(char* buf, size_t len) const {
    if (!_valid) { snprintf(buf, len, "--:--"); return; }
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    snprintf(buf, len, "%02d:%02d", t->tm_hour, t->tm_min);
}

void ClockManager::getTimestamp(char* buf, size_t len) const {
    if (!_valid || len == 0) { if (len > 0) buf[0] = '\0'; return; }
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

// ── Interactive timezone picker ───────────────────────────────────────────────

#define TZ_VISIBLE 9   // rows visible at once

static void renderTzPicker(int cursor, int scrollTop) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("SELECT TIMEZONE");
    dm.fillRect(0, outputY + LINE_HEIGHT, SCREEN_WIDTH, 1, 0x7BEF);

    const char* currentPosix = ClockManager::instance().tzStr();

    for (int i = 0; i < TZ_VISIBLE && (scrollTop + i) < N_PRESETS; i++) {
        int idx = scrollTop + i;
        int32_t y = outputY + LINE_HEIGHT * (2 + i);
        bool selected = (idx == cursor);
        bool isCurrent = (strcmp(PRESETS[idx].posix, currentPosix) == 0);

        dm.setCursor(4, y);
        if (selected) {
            dm.setTextColor(TFT_GREEN);
            dm.printText("> ");
        } else {
            dm.setTextColor(0x2104);
            dm.printText("  ");
        }
        dm.setTextColor(selected ? TFT_GREEN : (isCurrent ? TFT_CYAN : TFT_WHITE));
        dm.println(PRESETS[idx].label);
    }

    // Scrollbar
    if (N_PRESETS > TZ_VISIBLE) {
        int totalH = TZ_VISIBLE * LINE_HEIGHT;
        int barH   = max(4, totalH * TZ_VISIBLE / N_PRESETS);
        int barY   = outputY + LINE_HEIGHT * 2 + (scrollTop * (totalH - barH)) / (N_PRESETS - TZ_VISIBLE);
        dm.fillRect(SCREEN_WIDTH - 4, outputY + LINE_HEIGHT * 2, 3, totalH, 0x2104);
        dm.fillRect(SCREEN_WIDTH - 4, barY, 3, barH, TFT_CYAN);
    }

    dm.fillRect(0, outputY + LINE_HEIGHT * 11, SCREEN_WIDTH, 1, 0x7BEF);
    dm.setCursor(4, outputY + LINE_HEIGHT * 12);
    dm.setTextColor(0x7BEF);
    dm.println("[UP/DN] move  [Enter] select  [q] cancel");
}

static void runTzPicker() {
    int cursor    = 0;
    int scrollTop = 0;

    // Pre-select if current TZ matches a preset
    const char* cur = ClockManager::instance().tzStr();
    for (int i = 0; i < N_PRESETS; i++) {
        if (strcmp(PRESETS[i].posix, cur) == 0) {
            cursor    = i;
            scrollTop = max(0, i - TZ_VISIBLE / 2);
            if (scrollTop + TZ_VISIBLE > N_PRESETS)
                scrollTop = max(0, N_PRESETS - TZ_VISIBLE);
            break;
        }
    }

    renderTzPicker(cursor, scrollTop);

    while (true) {
        char k           = inputHandler.getKeyboardInput();
        TrackballEvent e = inputHandler.getTrackballEvent();

        if (k == 'q' || k == 'Q') {
            displayManager.clearScreen();
            displayManager.printCommandScreen();
            return;
        }

        bool moved = false;

        if (e == TBALL_UP || k == 'w' || k == 'W') {
            if (cursor > 0) { cursor--; moved = true; }
        }
        if (e == TBALL_DOWN || k == 's' || k == 'S') {
            if (cursor < N_PRESETS - 1) { cursor++; moved = true; }
        }

        if (moved) {
            if (cursor < scrollTop) scrollTop = cursor;
            if (cursor >= scrollTop + TZ_VISIBLE) scrollTop = cursor - TZ_VISIBLE + 1;
            renderTzPicker(cursor, scrollTop);
        }

        // Select
        bool confirmed = (k == '\n' || k == '\r' || e == TBALL_CLICK);
        if (confirmed) {
            ClockManager& cm = ClockManager::instance();
            cm.setTZ(PRESETS[cursor].posix);
            bool saved = cm.saveConfig();

            displayManager.clearScreen();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_GREEN);
            displayManager.println("TIMEZONE SET");

            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            displayManager.println(PRESETS[cursor].label);

            if (cm.isValid()) {
                char t[10], d[12], line[32];
                cm.getTimeStr(t, sizeof(t));
                cm.getDateStr(d, sizeof(d));
                snprintf(line, sizeof(line), "Local: %s  %s", t, d);
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(line);
            }

            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_GREEN);
            displayManager.println(saved ? "Saved to /clock.conf" : "Saved to device memory");
            displayManager.printCommandScreen();
            return;
        }
    }
}

// ── `tz` command entry point ──────────────────────────────────────────────────

void runTzCmd(char* args) {
    ClockManager& cm = ClockManager::instance();

    // No args or "list" → interactive picker
    if (!args || !args[0] || strcmp(args, "list") == 0) {
        runTzPicker();
        return;
    }

    // "status" → show current TZ + time
    if (strcmp(args, "status") == 0) {
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("TIMEZONE STATUS");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        char line[72];
        snprintf(line, sizeof(line), "TZ: %s", cm.tzStr());
        displayManager.println(line);
        displayManager.setCursor(4, displayManager.getCursorY());
        if (cm.isValid()) {
            char t[10], d[12];
            cm.getTimeStr(t, sizeof(t));
            cm.getDateStr(d, sizeof(d));
            snprintf(line, sizeof(line), "Now: %s  %s", t, d);
            displayManager.setTextColor(TFT_GREEN);
        } else {
            strncpy(line, "Now: not synced yet", sizeof(line));
            displayManager.setTextColor(TFT_YELLOW);
        }
        displayManager.println(line);
        displayManager.printCommandScreen();
        return;
    }

    // "+H", "+H:MM", "-H", "-H:MM" → generate POSIX offset string
    // "+3" means UTC+3 (east) → POSIX sign inverted → "UTC-3"
    char newTz[64];
    if ((args[0] == '+' || args[0] == '-') && args[1] >= '0' && args[1] <= '9') {
        int posixSign = (args[0] == '+') ? -1 : 1;   // POSIX sign is opposite of UTC offset
        char* p = args + 1;
        int hours   = atoi(p);
        int minutes = 0;
        char* colon = strchr(p, ':');
        if (colon) minutes = atoi(colon + 1);
        if (minutes > 0)
            snprintf(newTz, sizeof(newTz), "UTC%+d:%02d", posixSign * hours, minutes);
        else
            snprintf(newTz, sizeof(newTz), "UTC%+d", posixSign * hours);
    } else {
        // Raw POSIX string — power-user path
        strncpy(newTz, args, sizeof(newTz) - 1);
        newTz[sizeof(newTz) - 1] = '\0';
    }

    cm.setTZ(newTz);
    bool saved = cm.saveConfig();

    displayManager.clearScreen();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("TIMEZONE SET");
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_WHITE);
    char msg[72];
    snprintf(msg, sizeof(msg), "TZ: %s", newTz);
    displayManager.println(msg);
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println(saved ? "Saved to /clock.conf" : "Saved to device memory");
    if (cm.isValid()) {
        char t[10], d[12], line[32];
        cm.getTimeStr(t, sizeof(t));
        cm.getDateStr(d, sizeof(d));
        snprintf(line, sizeof(line), "Local: %s  %s", t, d);
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.println(line);
    }
    displayManager.printCommandScreen();
}
