// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef BAD_USB_H
#define BAD_USB_H

#include <Arduino.h>
#include "usb_keyboard.h"  // provides g_hid_keyboard shared instance

class BadUsb {
public:
    void begin();           // no-op — g_hid_keyboard already registered by usbKeyboard.begin()
    void start(char* args); // usbexec/ux — run demo or SD script

private:
    bool _aborted;
    int  _defaultCharDelay; // ms between characters in STRING (DEFAULT_STRING_DELAY)
    int  _nextCharDelay;    // one-shot override for next STRING (-1 = use default)

    // Script runners
    void runDemo();
    void runFile(const char* path);
    void runLines(const char* const* lines, int count);
    bool executeLine(const char* line, int& defaultDelay);

    // Interruptible delay — returns false if aborted
    bool scriptDelay(uint32_t ms);

    // Key helpers
    void    typeString(const char* s);
    void    pressSpecialKey(uint8_t keyCode);
    void    pressCombo(uint8_t mods[], int nMods, uint8_t key);
    uint8_t resolveSpecialKey(const char* token);
    bool    isModifier(const char* token);
    uint8_t modifierCode(const char* token);

    // Hyphenated combination table (CTRL-ALT, GUI-SHIFT, etc.)
    struct HyphenCombo { const char* cmd; uint8_t k1, k2, k3; };
    static const HyphenCombo COMBOS[];
    static const int          COMBOS_COUNT;
    const HyphenCombo* findHyphenCombo(const char* token);
};

extern BadUsb badUsb;

#endif // BAD_USB_H
