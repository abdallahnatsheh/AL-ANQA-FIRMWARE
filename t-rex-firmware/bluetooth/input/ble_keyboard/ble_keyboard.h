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

    // ── BadUSB-over-BLE support (driven by BleHidSink; reuses the btkbd HID stack) ──
    // cloneMacStr!=nullptr → BLESA MAC-clone: advertise as the target's address so a host
    // bonded to it auto-reconnects (Phase 2). cloneType: 0=public, else random. cloneName
    // = advertised name to mimic (nullptr keeps T-REX-KBD).
    bool badusbBegin(const char* cloneMacStr = nullptr, uint8_t cloneType = 1,
                     const char* cloneName = nullptr);
    void badusbEnd();            // endHid()
    bool badusbConnected();      // true once a host has connected
    bool badusbCloneMacOk();     // clone: false if the MAC spoof was rejected (RPA/public) → name-only
    void kbdHoldChar(char c);            // printable ASCII → add to live report + notify (hold)
    void kbdHoldUsage(uint8_t mod, uint8_t kc); // special/mod HID usage → add + notify (hold)
    void kbdType(char c);                // press+release one printable ASCII char (standalone)
    void kbdReleaseAll();                // clear the live report + notify

private:
    NimBLECharacteristic* _inputKbd   = nullptr;
    NimBLECharacteristic* _inputMouse = nullptr;
    uint8_t _rpt[8] = {0};   // live keyboard HID report (mod, resv, k0..k5) for BadUSB combos

    void   beginHid(bool bond = true, const char* cloneMacStr = nullptr,
                    uint8_t cloneType = 1, const char* cloneName = nullptr);
                    // NimBLE HID init + advertise (shared by start/jiggle).
                    // bond=false → Just Works, NO stored bond (fast BadBLE use).
                    // cloneMacStr → spoof that address instead of the stable T-REX one.
    void   endHid();     // teardown: stop adv, disconnect, idle the stack (no deinit)
    void   kbdNotify();  // notify the current _rpt on the keyboard input characteristic
    void   sendKey(char k);
    void   sendMouseMove(int8_t x, int8_t y);
    void   sendMouseClick(uint8_t btn);
    int8_t mouseStep(uint32_t elapsedMs);
};

extern BleKeyboard bleKeyboard;

#endif // BLE_KEYBOARD_H
