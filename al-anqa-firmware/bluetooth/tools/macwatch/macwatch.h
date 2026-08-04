// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// macwatch / mw — WiFi probe + BLE MAC watchlist with proximity alert.
// Register named devices (full MAC or vendor OUI prefix); alert (beep + screen
// wake + popup) when one comes into range. Foreground interactive watcher
// (dual-radio) + BLE-only background mode (mw bg). "trackme-lite".

#ifndef MACWATCH_H
#define MACWATCH_H

#include <Arduino.h>

// Foreground entry — dispatches: "add"|"bg"|"stop"|<interactive watch>.
void runMacwatch(char* args);

// Background BLE-only presence watch (mw bg / mw stop).
void startMacwatchBg();
void stopMacwatchBg();
bool isMacwatchBgActive();
void pollMacwatchBg();   // hooked in getKeyboardInput()

#endif // MACWATCH_H
