// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "hid_sink.h"
#include "usb_keyboard.h"   // g_hid_keyboard + (transitively) USBHIDKeyboard KEY_* macros
#include "ble_keyboard.h"   // bleKeyboard (BLE HID)

// ── USB sink — direct passthrough to the TinyUSB HID keyboard ────────────────────
class UsbHidSink : public HidSink {
public:
    void press(uint8_t k) override  { g_hid_keyboard.press(k); }
    void printChar(char c) override { g_hid_keyboard.print(c); }
    void releaseAll() override      { g_hid_keyboard.releaseAll(); }
};

// ── Arduino special/modifier keycode → HID usage (printables go via the BLE
//    keyboard's own ASCII table through kbdHoldChar, so only 0x80+ codes here) ─────
static void arduinoSpecialToHid(uint8_t ak, uint8_t& mod, uint8_t& kc) {
    mod = 0; kc = 0;
    switch (ak) {
        case KEY_LEFT_CTRL:   case KEY_RIGHT_CTRL:   mod = 0x01; return;
        case KEY_LEFT_SHIFT:  case KEY_RIGHT_SHIFT:  mod = 0x02; return;
        case KEY_LEFT_ALT:    case KEY_RIGHT_ALT:    mod = 0x04; return;
        case KEY_LEFT_GUI:    case KEY_RIGHT_GUI:    mod = 0x08; return;
        case KEY_RETURN:      kc = 0x28; return;
        case KEY_ESC:         kc = 0x29; return;
        case KEY_BACKSPACE:   kc = 0x2A; return;
        case KEY_TAB:         kc = 0x2B; return;
        case KEY_CAPS_LOCK:   kc = 0x39; return;
        case KEY_INSERT:      kc = 0x49; return;
        case KEY_HOME:        kc = 0x4A; return;
        case KEY_PAGE_UP:     kc = 0x4B; return;
        case KEY_DELETE:      kc = 0x4C; return;
        case KEY_END:         kc = 0x4D; return;
        case KEY_PAGE_DOWN:   kc = 0x4E; return;
        case KEY_RIGHT_ARROW: kc = 0x4F; return;
        case KEY_LEFT_ARROW:  kc = 0x50; return;
        case KEY_DOWN_ARROW:  kc = 0x51; return;
        case KEY_UP_ARROW:    kc = 0x52; return;
        default: break;
    }
    if (ak >= KEY_F1  && ak <= KEY_F12) { kc = 0x3A + (ak - KEY_F1);  return; }
    if (ak >= KEY_F13 && ak <= KEY_F24) { kc = 0x68 + (ak - KEY_F13); return; }
}

// ── BLE sink — translate Arduino keycodes and drive the BLE HID keyboard ──────────
class BleHidSink : public HidSink {
public:
    void press(uint8_t k) override {
        if (k >= 0x20 && k < 0x7F) {              // printable ASCII (letter/digit/symbol/space)
            bleKeyboard.kbdHoldChar((char)k);     // BLE keyboard owns the ASCII→HID table
        } else {                                  // modifier or special key
            uint8_t mod = 0, kc = 0;
            arduinoSpecialToHid(k, mod, kc);
            if (mod || kc) bleKeyboard.kbdHoldUsage(mod, kc);
        }
    }
    void printChar(char c) override { bleKeyboard.kbdType(c); }
    void releaseAll() override      { bleKeyboard.kbdReleaseAll(); }
};

static UsbHidSink s_usbSink;
static BleHidSink s_bleSink;
HidSink* g_usbHidSink = &s_usbSink;
HidSink* g_bleHidSink = &s_bleSink;
