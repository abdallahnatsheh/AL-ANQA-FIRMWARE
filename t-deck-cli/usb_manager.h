// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <Arduino.h>

class USBManager {
public:
    void begin();      // Call in setup() — configures MSC callbacks, registers HID
    void startMSC();   // usbmsc command — expose SD as USB drive, blocks until q
    void startHID();   // usbhid command — type test keystroke, returns immediately
};

extern USBManager usbManager;

#endif // USB_MANAGER_H
