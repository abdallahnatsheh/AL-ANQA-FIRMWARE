#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <Arduino.h>
#include <functional>

enum NotifLevel {
    NOTIF_ALERT   = 0,
    NOTIF_WARNING = 1,
    NOTIF_SUCCESS = 2,
    NOTIF_INFO    = 3,
    NOTIF_PING    = 4,
    NOTIF_COUNT   = 5
};

class NotificationManager {
public:
    static NotificationManager& getInstance();

    void begin();
    void setWakeCallback(std::function<void()> cb) { _wakeCallback = cb; }
    // force=true ignores per-level enable (for testing). allowCovert=true lets a
    // sound ring even under the undercover g_covert silence gate — used ONLY by the
    // home-launcher timer/reminder alarms (a real phone's alarm rings); every other
    // caller leaves it false and stays silent under cover.
    void notify(NotifLevel level, bool force = false, bool allowCovert = false);
    void setNotifVol(uint8_t vol);
    void enable(NotifLevel level, bool on);
    void enableAll(bool on);
    void loadConfig();
    void saveConfig();
    void printStatus();

    static void handleNotifCmd(char* args);

private:
    NotificationManager() {}

    uint8_t _notifVol = 70;
    bool    _enabled[NOTIF_COUNT]   = { true, true, true, true, true };
    char    _mp3File[NOTIF_COUNT][64];   // SD path per level, empty = use default tone
    std::function<void()> _wakeCallback;

    void playTones(const int* freqs, const int* durs, int count);
    bool playWav(const char* path);   // WAV-only, no external lib needed
};

#endif // NOTIFICATION_MANAGER_H
