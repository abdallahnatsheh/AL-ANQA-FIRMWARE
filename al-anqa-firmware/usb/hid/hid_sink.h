// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// HidSink — output abstraction so the BadUSB DuckyScript engine can drive EITHER
// the USB HID keyboard (g_hid_keyboard) or the BLE HID keyboard (bleKeyboard) with
// the exact same parser. The engine speaks Arduino USBHIDKeyboard keycodes; each
// sink translates to its own transport. Lets `ux` gain a `ble` subcommand without
// duplicating any DuckyScript logic.

#ifndef HID_SINK_H
#define HID_SINK_H

#include <Arduino.h>

class HidSink {
public:
    virtual ~HidSink() {}
    virtual void press(uint8_t arduinoKey) = 0; // Arduino USBHIDKeyboard keycode — press + hold
    virtual void printChar(char c)         = 0; // type one printable ASCII char (press+release)
    virtual void releaseAll()              = 0; // release all held keys + modifiers
};

extern HidSink* g_usbHidSink;   // wraps g_hid_keyboard (USB HID)
extern HidSink* g_bleHidSink;   // wraps bleKeyboard    (BLE HID)

#endif // HID_SINK_H
