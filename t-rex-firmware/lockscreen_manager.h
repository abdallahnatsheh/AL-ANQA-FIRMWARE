// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef LOCKSCREEN_MANAGER_H
#define LOCKSCREEN_MANAGER_H

#include <Arduino.h>
#include "display_manager.h"
#include "input_handling.h"

class LockScreenManager {
public:
    static LockScreenManager& getInstance();

    void           init();
    char           intercept(char k, uint32_t now);
    TrackballEvent interceptTrackball(TrackballEvent evt);
    void           updateActivity();           // call on any user input to reset idle timer
    void           lock();
    bool           isLocked() const { return _locked; }
    bool           consumeJustUnlocked() { bool v = _justUnlocked; _justUnlocked = false; return v; }
    void           cmd(char* args);

private:
    LockScreenManager() = default;

    bool     _locked          = false;
    bool     _hasPassword     = false;
    char     _hashHex[65]     = {};   // SHA-256 of salt||pin, as 64 hex chars
    char     _saltHex[17]     = {};   // 8 random bytes as 16 hex chars
    uint32_t _timeout         = 0;    // idle seconds before auto-lock; 0=off
    uint32_t _lockedAtMs      = 0;
    uint32_t _lastActivityMs  = 0;

    char     _pinBuf[17]      = {};   // current PIN entry buffer
    uint8_t  _pinLen          = 0;
    bool     _pinActive       = false;  // PIN entry overlay is showing
    uint32_t _wrongPinMs      = 0;      // millis() when wrong PIN entered; 0=none

    uint32_t _tpadDownMs      = 0;
    bool     _tpadHeld        = false;

    uint8_t  _spaceCount      = 0;       // consecutive Spaces pressed while locked (no-pwd)

    uint32_t _lastDurRefresh  = 0;
    bool     _justUnlocked    = false;

    void  drawDormant();
    void  drawPinScreen();
    void  refreshDuration();
    void  tryUnlock();
    bool  checkPin(const char* pin) const;
    bool  loadConfig();
    bool  saveConfig();   // returns false if no SD — caller shows warning
    static void hashPin(const char* pin, const char* saltHex, char* outHex65);
    static void genSalt(char* outHex17);
    static bool promptPin(const char* prompt, char* buf, uint8_t maxLen);

    void cmdNew();
    void cmdUpdate();
    void cmdClean();
    void cmdWipe();
    void cmdTimeout(const char* arg);
    void cmdStatus();
};

#endif // LOCKSCREEN_MANAGER_H
