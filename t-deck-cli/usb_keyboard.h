// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <Arduino.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

class UsbKeyboard {
public:
    void begin();  // Register HID descriptors with TinyUSB — must be called before USB.begin()
    void start();  // usbkbd/uk — T-DECK keyboard+trackball as USB keyboard+mouse, blocks until exit

private:
    USBHIDKeyboard _keyboard;
    USBHIDMouse    _mouse;

    void    sendKey(char k);
    int8_t  mouseStep(uint32_t elapsedMs);
};

extern UsbKeyboard usbKeyboard;

#endif // USB_KEYBOARD_H
