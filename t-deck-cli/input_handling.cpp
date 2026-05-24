#include "input_handling.h"
#include "utilities.h"
#include "powersave_manager.h"
#include "lockscreen_manager.h"
#include "wguard.h"
#include <Wire.h>
#include <esp_timer.h>

extern WGuard wGuard;

// ISR-based click detection — works even during blocking BLE scans
static volatile int64_t s_isrLastClickUs    = 0;
static volatile bool    s_doubleClickPending = false;
static volatile bool    s_singleClickPending = false;

static void IRAM_ATTR clickISR() {
    int64_t now = esp_timer_get_time();  // microseconds, ISR-safe
    if (now - s_isrLastClickUs < 400000LL) {
        s_doubleClickPending = true;
        s_singleClickPending = false;
        s_isrLastClickUs = 0;
    } else {
        s_singleClickPending = true;
        s_isrLastClickUs = now;
    }
}

void InputHandling::begin() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    lastActivityTime = millis();
    delay(300);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available()) Wire.read();

    // Trackpad GPIO init — read actual state to avoid false triggers on first poll
    static const uint8_t tpPins[4] = {
        BOARD_TBOX_G02, BOARD_TBOX_G01, BOARD_TBOX_G04, BOARD_TBOX_G03
    };
    for (int i = 0; i < 4; i++) {
        pinMode(tpPins[i], INPUT_PULLUP);
        _tballLast[i] = digitalRead(tpPins[i]);
    }

    // Click pin — ISR handles all timing
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOARD_BOOT_PIN), clickISR, FALLING);
}

TrackballEvent InputHandling::getTrackballEvent() {
    static const uint8_t pins[4] = {
        BOARD_TBOX_G02, BOARD_TBOX_G01, BOARD_TBOX_G04, BOARD_TBOX_G03
    };
    static const TrackballEvent evts[4] = { TBALL_RIGHT, TBALL_UP, TBALL_LEFT, TBALL_DOWN };

    // Directions — fire on any edge
    for (int i = 0; i < 4; i++) {
        bool cur = digitalRead(pins[i]);
        if (cur != _tballLast[i]) {
            _tballLast[i] = cur;
            updateActivity();
            if (!PowerSaveManager::getInstance().isManualOff())
                PowerSaveManager::getInstance().updateActivity();
            LockScreenManager::getInstance().updateActivity();
            return evts[i];
        }
    }

    // Single click — flagged by ISR
    if (s_singleClickPending) {
        s_singleClickPending = false;
        updateActivity();
        if (!PowerSaveManager::getInstance().isManualOff())
            PowerSaveManager::getInstance().updateActivity();
        LockScreenManager::getInstance().updateActivity();
        return TBALL_CLICK;
    }

    return TBALL_NONE;
}

char InputHandling::getKeyboardInput() {
    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if (now - lastPoll < 10) return 0;
    lastPoll = now;

    PowerSaveManager::getInstance().update();
    wGuard.pollBackground();

    // Double-click screen-off — ISR captured it, we just act on the flag
    if (s_doubleClickPending) {
        s_doubleClickPending = false;
        PowerSaveManager::getInstance().toggleManualOff();
    }

    if (Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1) == 0) return 0;
    char key = Wire.read();
    if (key != 0) {
        updateActivity();
        PowerSaveManager::getInstance().updateActivity();
    }
    return LockScreenManager::getInstance().intercept(key, now);
}

void InputHandling::clearPendingClicks() {
    s_singleClickPending = false;
    s_doubleClickPending = false;
}
