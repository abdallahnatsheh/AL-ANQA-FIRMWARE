// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <Arduino.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

// Shared HID keyboard — one instance registered with TinyUSB; used by both UsbKeyboard and BadUsb
extern USBHIDKeyboard g_hid_keyboard;

class UsbKeyboard {
public:
    void begin();   // Register HID descriptors with TinyUSB — must be called before USB.begin()
    void start();   // usbkbd/uk — T-DECK keyboard+trackball as USB keyboard+mouse, blocks until exit
    void jiggle();  // jiggle/jg — mouse jiggler, nudges cursor every 30s to prevent screen lock
    void moveMouse(int8_t dx, int8_t dy) { _mouse.move(dx, dy, 0); }
    void clickMouse(uint8_t btn)         { _mouse.click(btn); }

private:
    friend class BadUsb;   // startLog() drives the mouse via _mouse directly
    USBHIDMouse _mouse;

    void    sendKey(char k);
    int8_t  mouseStep(uint32_t elapsedMs);
};

extern UsbKeyboard usbKeyboard;

#endif // USB_KEYBOARD_H
