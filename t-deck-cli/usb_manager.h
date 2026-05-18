// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <Arduino.h>

class USBManager {
public:
    void begin();              // Call in setup() — registers HID + MSC with TinyUSB, starts USB
    void startMSC();           // usbmsc/um — expose SD as USB drive, blocks until q
    bool isConnected() const;  // true when USB host is connected and active
};

extern USBManager usbManager;

#endif // USB_MANAGER_H
