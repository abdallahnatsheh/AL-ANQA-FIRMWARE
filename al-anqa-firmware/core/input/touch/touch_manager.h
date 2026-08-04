// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// TouchManager — GT911 capacitive touch, singleton, mirrors InputHandling's
// event-poll style (see getTrackballEvent()). Touch is additive: every
// consumer must keep working with keyboard/trackball alone if isPresent()
// is false (no touch panel found / not probed yet).

#ifndef TOUCH_MANAGER_H
#define TOUCH_MANAGER_H

#include <Arduino.h>

struct TouchEvent {
    enum Type { NONE, TAP, LONG_PRESS, DRAG_START, DRAG_MOVE, DRAG_END } type = NONE;
    int16_t x = 0, y = 0;     // mapped to displayManager's 320x240 landscape
    int16_t dx = 0, dy = 0;   // delta since the previous DRAG_* event (DRAG_MOVE/END only)
};

class TouchManager {
public:
    static TouchManager& instance();

    void begin();               // probes GT911; assumes Wire already begun (DisplayManager::init())
    TouchEvent poll();           // call once per main loop iteration; type=NONE when idle/not present
    bool isPresent() const { return _present; }
    uint8_t address() const { return _addr; }   // 0x00 if never found — shown by `test touch`

private:
    TouchManager() = default;
    bool    _present = false;
    uint8_t _addr    = 0;
};

#endif // TOUCH_MANAGER_H
