#include "input_handling.h"
#include "utilities.h"
#include "powersave_manager.h"
#include <Wire.h>

void InputHandling::begin() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    lastActivityTime = millis();
    delay(300);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available()) Wire.read();

    // Trackpad GPIO init — read actual state to avoid false triggers on first poll
    static const uint8_t tpPins[5] = {
        BOARD_TBOX_G02, BOARD_TBOX_G01, BOARD_TBOX_G04, BOARD_TBOX_G03, BOARD_BOOT_PIN
    };
    for (int i = 0; i < 5; i++) {
        pinMode(tpPins[i], INPUT_PULLUP);
        _tballLast[i] = digitalRead(tpPins[i]);
    }
}

TrackballEvent InputHandling::getTrackballEvent() {
    static const uint8_t pins[5] = {
        BOARD_TBOX_G02, BOARD_TBOX_G01, BOARD_TBOX_G04, BOARD_TBOX_G03, BOARD_BOOT_PIN
    };
    static const TrackballEvent evts[4] = { TBALL_RIGHT, TBALL_UP, TBALL_LEFT, TBALL_DOWN };

    // Directions — fire on any edge
    for (int i = 0; i < 4; i++) {
        bool cur = digitalRead(pins[i]);
        if (cur != _tballLast[i]) {
            _tballLast[i] = cur;
            updateActivity();
            PowerSaveManager::getInstance().updateActivity();
            return evts[i];
        }
    }

    // Click — falling edge only (press, not release)
    bool clickCur = digitalRead(BOARD_BOOT_PIN);
    if (!clickCur && _tballLast[4]) {
        _tballLast[4] = false;
        updateActivity();
        PowerSaveManager::getInstance().updateActivity();
        return TBALL_CLICK;
    }
    _tballLast[4] = clickCur;

    return TBALL_NONE;
}

char InputHandling::getKeyboardInput() {
    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if (now - lastPoll < 10) return 0;
    lastPoll = now;

    PowerSaveManager::getInstance().update();

    if (Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1) == 0) return 0;
    char key = Wire.read();
    if (key != 0) {
        updateActivity();
        PowerSaveManager::getInstance().updateActivity();
    }
    return key;
}
