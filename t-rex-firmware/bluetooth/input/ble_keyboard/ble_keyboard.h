// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <Arduino.h>
#include <NimBLECharacteristic.h>

class BleKeyboard {
public:
    void start();    // btkbd/bk — T-DECK keyboard+trackball as BLE keyboard+mouse
    void jiggle();   // jg ble — BLE mouse jiggler (prevent host screen lock)

private:
    NimBLECharacteristic* _inputKbd   = nullptr;
    NimBLECharacteristic* _inputMouse = nullptr;

    void   beginHid();   // NimBLE HID init + advertise (shared by start/jiggle)
    void   endHid();     // teardown: stop adv, disconnect, idle the stack (no deinit)
    void   sendKey(char k);
    void   sendMouseMove(int8_t x, int8_t y);
    void   sendMouseClick(uint8_t btn);
    int8_t mouseStep(uint32_t elapsedMs);
};

extern BleKeyboard bleKeyboard;

#endif // BLE_KEYBOARD_H
