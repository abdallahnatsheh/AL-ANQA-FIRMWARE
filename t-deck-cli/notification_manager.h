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
    void notify(NotifLevel level);
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
    bool playMp3(const char* path);
};

#endif // NOTIFICATION_MANAGER_H
