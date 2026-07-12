// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef BAD_USB_H
#define BAD_USB_H

#include <Arduino.h>
#include "usb_keyboard.h"  // provides g_hid_keyboard shared instance
#include "hid_sink.h"      // output abstraction (USB vs BLE HID)

class BadUsb {
public:
    void begin();                        // no-op — g_hid_keyboard registered by usbKeyboard.begin()
    // usbexec/ux [ble [clone <mac|#>]] — run demo or SD script over USB or BLE HID.
    // cloneMacStr!=nullptr → Phase-2 BLESA MAC-clone target.
    void start(char* args, bool ble = false, const char* cloneMacStr = nullptr,
               uint8_t cloneType = 1, const char* cloneName = nullptr);
    void startInteractive();             // bare `ux ble` → guided menu (mode/target/name/script)

private:
    bool     _aborted;
    int      _defaultCharDelay; // ms between characters in STRING (DEFAULT_STRING_DELAY)
    int      _nextCharDelay;    // one-shot override for next STRING (-1 = use default)
    bool     _ble = false;      // true = drive BLE HID, false = USB HID
    HidSink* _sink = nullptr;   // active output sink (chosen in start())

    // BLE: bring up HID (spoofing cloneMacStr if set) + block until a host connects
    // (q/hold aborts). Returns false on abort.
    bool     bleWaitForHost(const char* cloneMacStr, uint8_t cloneType, const char* cloneName);

    // Interactive `ux ble` helpers (list pickers modelled on the wpa3down picker)
    void     drawBleHeader(const char* label);
    void     drawBleFooter(const char* hint);
    int      blePickMode();                     // 0=connect, 1=spoof, -1=cancel
    int      blePickTarget();                   // index into sbl cache, -1=cancel/back
    bool     blePickScript(char* out, size_t n);// out="demo" or full path; false=cancel
    void     blePromptName(char* buf, size_t n);// edit buf in place (Enter=keep)

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
