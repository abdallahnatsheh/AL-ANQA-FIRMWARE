// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Sources: lewisxhe/SensorLib (MIT) — TouchDrvGT911 driver (lib_deps @ ^0.4.1).
// begin()/setSwapXY/setMirrorXY/setMaxCoordinates config mirrors LilyGo's own
// official T-Deck example (Xinyuan-LilyGO/T-Deck examples/Touchpad/Touchpad.ino,
// MIT) verbatim — vendor-validated for this exact board/orientation, not a guess.
// (That example ships SensorLib 0.2.x; the read path here uses 0.4.x's current
// getTouchPoints() API instead of its now-deprecated getPoint() overload.)

#include "touch_manager.h"
#include "utilities.h"
#include "display_manager.h"
#include <Wire.h>
#if BOARD_HAS_TOUCH
// SensorLib 0.4.x: the per-driver "TouchDrvGT911.hpp" top-level header is
// deprecated (emits a #pragma message) — the umbrella "TouchDrv.hpp" is the
// supported entry point; it still pulls in TouchDrvGT911 + TouchPoints.
#include "TouchDrv.hpp"
#endif

#define TOUCH_POLL_MS       20    // ~50Hz poll throttle
#define TOUCH_MOVE_PX        8    // travel beyond this = drag, not tap
#define TOUCH_TAP_MS        250   // press+release faster than this = tap
#define TOUCH_LONGPRESS_MS  600   // held this long without moving = long-press

#if BOARD_HAS_TOUCH
static TouchDrvGT911 s_touch;
#endif

TouchManager& TouchManager::instance() {
    static TouchManager inst;
    return inst;
}

void TouchManager::begin() {
#if !BOARD_HAS_TOUCH
    _present = false;   // no capacitive touch on this board (e.g. T-Pager) — all
    return;             // consumers already degrade to keyboard/encoder on isPresent()==false
#else
    // No reset pin is broken out on this board (rst=-1) — the driver auto-
    // probes GT911_SLAVE_ADDRESS_L then _H internally, verifying the real
    // product-ID register (==911) rather than a bare I2C ACK, so it can't be
    // fooled by another chip answering the same address. Reuses the shared
    // Wire already begun by DisplayManager::init() — must never be given
    // different pins or a second Wire instance (shared bus with the keyboard).
    s_touch.setPins(-1, BOARD_TOUCH_INT);
    _present = s_touch.begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (!_present) return;

    // Orientation config — copied verbatim from LilyGo's official example,
    // not derived: this board's GT911 is configured for 320x240 native
    // output with X/Y swapped and Y mirrored to land on display coordinates.
    s_touch.setMaxCoordinates(SCREEN_WIDTH, SCREEN_HEIGHT);
    s_touch.setSwapXY(true);
    s_touch.setMirrorXY(false, true);

    _addr = GT911_SLAVE_ADDRESS_L;   // the address requested first; the driver's internal auto-probe doesn't expose which of L/H it actually landed on
#endif
}

TouchEvent TouchManager::poll() {
    TouchEvent ev;
    if (!_present) return ev;
#if BOARD_HAS_TOUCH

    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if (now - lastPoll < TOUCH_POLL_MS) return ev;
    lastPoll = now;

    static bool     touching  = false;
    static bool     moved     = false;
    static bool     longFired = false;
    static int16_t  startX = 0, startY = 0;
    static int16_t  lastX  = 0, lastY  = 0;
    static uint32_t startMs = 0;

    bool down = false;
    int16_t rawX = 0, rawY = 0;
    // SensorLib 0.4.x: getTouchPoints() is the current API (the older
    // getPoint(int16_t*,int16_t*,n) overload still exists but is marked
    // deprecated → compiler warning). One I2C read per call; coordinates are
    // already transformed by the driver's setSwapXY/setMirrorXY/setMaxCoordinates.
    const TouchPoints& pts = s_touch.getTouchPoints();
    if (pts.hasPoints()) {
        const TouchPoint& p = pts.getPoint(0);
        down = true;
        rawX = (int16_t)p.x;
        rawY = (int16_t)p.y;
        if (rawX < 0) rawX = 0; else if (rawX >= SCREEN_WIDTH)  rawX = SCREEN_WIDTH  - 1;
        if (rawY < 0) rawY = 0; else if (rawY >= SCREEN_HEIGHT) rawY = SCREEN_HEIGHT - 1;
    }

    if (down) {
        int16_t x = rawX, y = rawY;

        if (!touching) {
            touching  = true;
            moved     = false;
            longFired = false;
            startX = lastX = x;
            startY = lastY = y;
            startMs = now;
            return ev;   // press-down alone is not an event yet — wait for move/release/hold
        }

        int16_t dx = x - lastX, dy = y - lastY;
        bool pastMoveThresh = abs(x - startX) > TOUCH_MOVE_PX || abs(y - startY) > TOUCH_MOVE_PX;

        if (pastMoveThresh) {
            ev.type = moved ? TouchEvent::DRAG_MOVE : TouchEvent::DRAG_START;
            ev.x = x; ev.y = y; ev.dx = dx; ev.dy = dy;
            moved = true;
            lastX = x; lastY = y;
            return ev;
        }

        if (!moved && !longFired && (now - startMs) >= TOUCH_LONGPRESS_MS) {
            longFired = true;
            ev.type = TouchEvent::LONG_PRESS;
            ev.x = x; ev.y = y;
            return ev;
        }

        lastX = x; lastY = y;
        return ev;   // still holding, nothing new to report this tick
    }

    // Released
    if (touching) {
        touching = false;
        uint32_t heldMs = now - startMs;
        if (moved) {
            ev.type = TouchEvent::DRAG_END;
            ev.x = lastX; ev.y = lastY;
        } else if (!longFired && heldMs < TOUCH_TAP_MS) {
            ev.type = TouchEvent::TAP;
            ev.x = lastX; ev.y = lastY;
        }
        // else: already reported LONG_PRESS while held — release is silent
    }
#endif // BOARD_HAS_TOUCH
    return ev;
}
