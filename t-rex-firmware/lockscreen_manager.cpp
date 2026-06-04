// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "lockscreen_manager.h"
#include "input_handling.h"
#include "utilities.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include <SD.h>
#include <Preferences.h>
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── Y-coordinate constants for lock screen layout ─────────────────────────────
// Dormant (padlock art) layout:
//   outputY+0:          header
//   outputY+LINE_HEIGHT: separator
//   outputY+LINE_HEIGHT*3..7: padlock art (5 lines)
//   outputY+LINE_HEIGHT*9:  separator
//   outputY+LINE_HEIGHT*10: instruction
//   outputY+LINE_HEIGHT*11: separator
//   outputY+LINE_HEIGHT*12: duration
//
// PIN-entry layout:
//   outputY+0:          header
//   outputY+LINE_HEIGHT: separator
//   outputY+LINE_HEIGHT*4: "PIN: * * _"
//   outputY+LINE_HEIGHT*6: hint "[DEL] / [Enter]"
//   outputY+LINE_HEIGHT*7: separator
//   outputY+LINE_HEIGHT*8: duration

// ── Singleton ─────────────────────────────────────────────────────────────────

LockScreenManager& LockScreenManager::getInstance() {
    static LockScreenManager inst;
    return inst;
}

// ── Init ──────────────────────────────────────────────────────────────────────

void LockScreenManager::init() {
    _lastActivityMs = millis();

    // Check for a pending wipe scheduled by `lock wipe` when SD was absent.
    // We use NVS so the flag survives power-off without needing the SD card.
    Preferences prefs;
    prefs.begin("lockscreen", false);
    bool wipe = prefs.getBool("wipe", false);
    if (wipe) {
        prefs.putBool("wipe", false);
        prefs.end();
        if (sdCardManager.canAccessSD())
            SD.remove("/lockscreen.conf");
        return;   // start with no PIN — don't call loadConfig()
    }
    prefs.end();

    loadConfig();
}

// ── Config ────────────────────────────────────────────────────────────────────

bool LockScreenManager::loadConfig() {
    _hashHex[0] = '\0'; _saltHex[0] = '\0';
    _timeout = 0; _hasPassword = false;
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/lockscreen.conf", FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.length() || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String k = line.substring(0, eq);
        String v = line.substring(eq + 1);
        if      (k == "timeout") _timeout = (uint32_t)v.toInt();
        else if (k == "hash")    { strncpy(_hashHex, v.c_str(), 64); _hashHex[64] = '\0'; }
        else if (k == "salt")    { strncpy(_saltHex, v.c_str(), 16); _saltHex[16] = '\0'; }
    }
    f.close();
    _hasPassword = (strlen(_hashHex) == 64);
    return true;
}

bool LockScreenManager::saveConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/lockscreen.conf", FILE_WRITE);
    if (!f) return false;
    f.printf("timeout=%u\nhash=%s\nsalt=%s\n", _timeout, _hashHex, _saltHex);
    f.close();
    return true;
}

// ── Crypto (SHA-256 via mbedTLS, available on ESP32) ──────────────────────────

void LockScreenManager::genSalt(char* outHex17) {
    uint32_t r[2] = { esp_random(), esp_random() };
    for (int i = 0; i < 8; i++)
        snprintf(outHex17 + i * 2, 3, "%02x", ((uint8_t*)r)[i]);
    outHex17[16] = '\0';
}

void LockScreenManager::hashPin(const char* pin, const char* saltHex, char* outHex65) {
    char input[80];
    snprintf(input, sizeof(input), "%s%s", saltHex, pin);
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);   // 0 = SHA-256
    mbedtls_sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; i++)
        snprintf(outHex65 + i * 2, 3, "%02x", hash[i]);
    outHex65[64] = '\0';
}

bool LockScreenManager::checkPin(const char* pin) const {
    if (!_hasPassword) return true;
    char computed[65];
    hashPin(pin, _saltHex, computed);
    return strcmp(computed, _hashHex) == 0;
}

// ── Display helpers ───────────────────────────────────────────────────────────

static void drawLockHeader() {
    DisplayManager& dm = displayManager;
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_RED); dm.printText("##");
    dm.setTextColor(TFT_WHITE); dm.printText("  T-REX LOCKED  ");
    dm.setTextColor(TFT_RED); dm.printText("##");
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.fillRect(0, outputY + LINE_HEIGHT, SCREEN_WIDTH, 1, 0x7BEF);
}

void LockScreenManager::refreshDuration() {
    displayManager.setBlocked(false);
    displayManager.updateStatusBar();   // keep WiFi/battery/clock in status bar fresh while locked
    DisplayManager& dm = displayManager;
    uint32_t secs = (millis() - _lockedAtMs) / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "Locked: %02u:%02u:%02u",
             (unsigned)((secs / 3600) % 24),
             (unsigned)((secs / 60) % 60),
             (unsigned)(secs % 60));
    int32_t dy = _pinActive
               ? (outputY + LINE_HEIGHT * 8)
               : (outputY + LINE_HEIGHT * 12);
    dm.fillRect(0, dy, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    dm.setCursor(10, dy);
    dm.setTextColor(0x4208); dm.println(buf);

    // UTC clock line — only when GPS time is available
    {
        ClockManager& clk = ClockManager::instance();
        int32_t utcY = _pinActive
                     ? (outputY + LINE_HEIGHT * 9)
                     : (outputY + LINE_HEIGHT * 13);
        dm.fillRect(0, utcY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
        if (clk.isValid()) {
            char t[10], d[12], utcLine[26];
            clk.getTimeStr(t, sizeof(t));
            clk.getDateStr(d, sizeof(d));
            snprintf(utcLine, sizeof(utcLine), "%s  %s", t, d);
            dm.setCursor(10, utcY);
            dm.setTextColor(TFT_CYAN);
            dm.println(utcLine);
        }
    }

    dm.setTextColor(TFT_WHITE);
    _lastDurRefresh = millis();
    displayManager.setBlocked(true);
}

void LockScreenManager::drawDormant() {
    displayManager.setBlocked(false);
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    drawLockHeader();

    // ── Padlock ASCII art — Nokia-style, centered ─────────────────────────
    // Each string ~11 chars @ ~6px = 66px wide; left edge ≈ (320-66)/2 = 127
    const int32_t ax = 122;
    dm.setTextColor(TFT_YELLOW);
    dm.setCursor(ax, outputY + LINE_HEIGHT * 3);  dm.println("  .------.");
    dm.setCursor(ax, outputY + LINE_HEIGHT * 4);  dm.println(" /        \\");
    dm.setTextColor(TFT_WHITE);
    dm.setCursor(ax, outputY + LINE_HEIGHT * 5);  dm.println("+---------+");
    dm.setCursor(ax, outputY + LINE_HEIGHT * 6);  dm.println("|   (.)   |");
    dm.setCursor(ax, outputY + LINE_HEIGHT * 7);  dm.println("+---------+");

    // ── Instruction ───────────────────────────────────────────────────────
    dm.fillRect(0, outputY + LINE_HEIGHT * 9,  SCREEN_WIDTH, 1, 0x7BEF);
    dm.setCursor(10, outputY + LINE_HEIGHT * 10);
    if (!_hasPassword) {
        dm.setTextColor(TFT_GREEN);  dm.println("Press [SPACE] x3 to unlock");
    } else {
        dm.setTextColor(TFT_YELLOW); dm.println("Type PIN  then  [Enter]");
    }
    dm.fillRect(0, outputY + LINE_HEIGHT * 11, SCREEN_WIDTH, 1, 0x7BEF);

    refreshDuration();
    dm.setTextColor(TFT_WHITE);
    displayManager.setBlocked(true);
}

void LockScreenManager::drawPinScreen() {
    displayManager.setBlocked(false);
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    drawLockHeader();

    // ── PIN dots: "PIN:  * * * _" ─────────────────────────────────────────
    dm.setCursor(10, outputY + LINE_HEIGHT * 4);
    dm.setTextColor(TFT_CYAN);   dm.printText("PIN:  ");
    dm.setTextColor(TFT_YELLOW);
    for (int i = 0; i < _pinLen; i++) dm.printText("* ");
    dm.setTextColor(0x4208);     dm.println("_");

    // ── Hint ─────────────────────────────────────────────────────────────
    dm.setCursor(10, outputY + LINE_HEIGHT * 6);
    dm.setTextColor(0x4208);
    dm.println("[DEL] delete     [Enter] unlock");

    dm.fillRect(0, outputY + LINE_HEIGHT * 7, SCREEN_WIDTH, 1, 0x7BEF);
    refreshDuration();
    dm.setTextColor(TFT_WHITE);
    displayManager.setBlocked(true);
}

// ── Lock / Unlock ─────────────────────────────────────────────────────────────

void LockScreenManager::lock() {
    if (_locked) return;
    _locked     = true;
    _lockedAtMs = millis();
    _pinActive  = false;
    _pinLen     = 0;
    _pinBuf[0]  = '\0';
    _wrongPinMs = 0;
    _spaceCount = 0;
    _tpadHeld   = false;
    displayManager.setBlocked(true);   // prevent apps from drawing over the lock screen
    drawDormant();
}

void LockScreenManager::tryUnlock() {
    if (checkPin(_pinBuf)) {
        _locked         = false;
        _pinActive      = false;
        _pinLen         = 0;
        _pinBuf[0]      = '\0';
        _lastActivityMs = millis();
        _justUnlocked   = true;
        displayManager.setBlocked(false);
        displayManager.tdeck_begin();   // full redraw: status bar + prompt
    } else {
        // Flash red for 1.5 s, then redraw PIN entry
        displayManager.setBlocked(false);
        DisplayManager& dm = displayManager;
        dm.fillRect(0, outputY + LINE_HEIGHT * 4, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_RED);
        dm.setCursor(50, outputY + LINE_HEIGHT * 4 + LINE_HEIGHT / 2);
        dm.setTextColor(TFT_WHITE);
        dm.println("!! Wrong PIN — try again");
        dm.setTextColor(TFT_WHITE);
        displayManager.setBlocked(true);
        _wrongPinMs = millis();
        _pinLen     = 0;
        _pinBuf[0]  = '\0';
    }
}

// ── Main keyboard intercept (called from getKeyboardInput) ────────────────────

char LockScreenManager::intercept(char k, uint32_t now) {
    // ── Trackpad hold 3 s → lock (only while unlocked) ───────────────────
    if (!_locked) {
        bool tpadDown = (digitalRead(BOARD_BOOT_PIN) == LOW);
        if (tpadDown) {
            if (!_tpadHeld) { _tpadHeld = true; _tpadDownMs = now; }
            else if (now - _tpadDownMs >= 3000) {
                _tpadHeld = false;
                inputHandler.clearPendingClicks();   // suppress stale TBALL_CLICK
                lock();
                return 0;
            }
        } else {
            _tpadHeld = false;
        }
    }

    // ── Idle-timeout check ────────────────────────────────────────────────
    if (!_locked) {
        if (k != 0) _lastActivityMs = now;
        if (_timeout > 0 && _lastActivityMs > 0 &&
            (now - _lastActivityMs) >= _timeout * 1000UL) {
            lock();
            return 0;
        }
        return k;
    }

    // ═══════════════ Screen is locked ════════════════════════════════════

    // Wrong-PIN cooldown: swallow all keys for 1.5 s, then redraw
    if (_wrongPinMs > 0) {
        if (now - _wrongPinMs < 1500) return 0;
        _wrongPinMs = 0;
        drawPinScreen();
        return 0;
    }

    // Periodic duration-counter refresh (every 1 s)
    if (now - _lastDurRefresh >= 1000) refreshDuration();

    if (k == 0) return 0;

    // ── No-password mode: 3x Space unlocks (prevents accidental unlock) ─────
    if (!_hasPassword) {
        if (k == ' ') {
            _spaceCount++;
            if (_spaceCount >= 3) {
                _spaceCount     = 0;
                _locked         = false;
                _lastActivityMs = millis();
                _justUnlocked   = true;
                displayManager.setBlocked(false);
                displayManager.tdeck_begin();   // full redraw: status bar + prompt
            } else {
                DisplayManager& dm = displayManager;
                dm.fillRect(0, outputY + LINE_HEIGHT * 10, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
                dm.setCursor(10, outputY + LINE_HEIGHT * 10);
                dm.setTextColor(TFT_GREEN);
                char hint[32];
                snprintf(hint, sizeof(hint), "Press [SPACE] x3  (%u/3)", _spaceCount);
                dm.println(hint);
                dm.setTextColor(TFT_WHITE);
            }
        } else if (k != 0) {
            if (_spaceCount > 0) {
                _spaceCount = 0;
                DisplayManager& dm = displayManager;
                dm.fillRect(0, outputY + LINE_HEIGHT * 10, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
                dm.setCursor(10, outputY + LINE_HEIGHT * 10);
                dm.setTextColor(TFT_GREEN);
                dm.println("Press [SPACE] x3 to unlock");
                dm.setTextColor(TFT_WHITE);
            }
        }
        return 0;
    }

    // ── PIN-entry mode ────────────────────────────────────────────────────
    if (!_pinActive) {
        _pinActive = true;
        drawPinScreen();
    }

    if      (k == '\x08' || k == '\x7F') {
        if (_pinLen > 0) { _pinBuf[--_pinLen] = '\0'; drawPinScreen(); }
    }
    else if (k == '\r'   || k == '\n') {
        tryUnlock();
    }
    else if (k == '\x1B') {              // Esc → back to dormant screen
        _pinActive = false; _pinLen = 0; _pinBuf[0] = '\0';
        drawDormant();
    }
    else if (_pinLen < 16 && k >= 0x20 && k < 0x7F) {
        _pinBuf[_pinLen++] = k; _pinBuf[_pinLen] = '\0';
        drawPinScreen();
    }

    return 0;
}

// ── Trackball intercept (called from main loop) ───────────────────────────────

TrackballEvent LockScreenManager::interceptTrackball(TrackballEvent evt) {
    if (!_locked) return evt;
    return TBALL_NONE;   // swallow all trackball events while locked
}

void LockScreenManager::updateActivity() {
    if (!_locked) _lastActivityMs = millis();
}

// ── Interactive PIN prompt (used by cmd functions) ────────────────────────────
// Shows prompt text, reads masked PIN, returns true on Enter / false on Esc.

bool LockScreenManager::promptPin(const char* prompt, char* buf, uint8_t maxLen) {
    DisplayManager& dm = displayManager;
    uint8_t len = 0;
    buf[0] = '\0';
    dm.setTextColor(TFT_CYAN); dm.printText(prompt);
    int32_t dotX = dm.getCursorX();
    int32_t dotY = dm.getCursorY();

    auto redraw = [&]() {
        dm.fillRect(dotX, dotY, SCREEN_WIDTH - dotX - 4, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(dotX, dotY);
        dm.setTextColor(TFT_YELLOW);
        for (uint8_t i = 0; i < len; i++) dm.printText("* ");
        dm.setTextColor(0x4208); dm.printText("_");
    };
    redraw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == '\x1B')              return false;
        if (k == '\r' || k == '\n')   return true;
        if ((k == '\x08' || k == '\x7F') && len > 0)
            { buf[--len] = '\0'; redraw(); }
        else if (k >= 0x20 && k < 0x7F && len < maxLen - 1)
            { buf[len++] = k; buf[len] = '\0'; redraw(); }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Command dispatcher ────────────────────────────────────────────────────────

void LockScreenManager::cmd(char* args) {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();

    if (!args || !*args) {
        lock();   // always lock — no PIN required for simple keypad lock
        return;
    }

    char buf[64]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* sub = strtok(buf, " ");
    char* arg = strtok(nullptr, " ");

    if      (strcmp(sub, "new")     == 0) cmdNew();
    else if (strcmp(sub, "update")  == 0) cmdUpdate();
    else if (strcmp(sub, "clean")   == 0) cmdClean();
    else if (strcmp(sub, "wipe")    == 0) cmdWipe();
    else if (strcmp(sub, "timeout") == 0) cmdTimeout(arg);
    else if (strcmp(sub, "status")  == 0) cmdStatus();
    else {
        dm.setTextColor(TFT_RED);
        dm.println("Usage: lock [new|update|clean|wipe|timeout <s>|status]");
        dm.setTextColor(TFT_WHITE);
    }

    dm.printCommandScreen();   // restore prompt after any subcommand
}

void LockScreenManager::cmdNew() {
    DisplayManager& dm = displayManager;
    if (_hasPassword) {
        dm.setTextColor(TFT_RED); dm.println("PIN already set. Use 'lock update'.");
        dm.setTextColor(TFT_WHITE); return;
    }
    char p1[17] = {}, p2[17] = {};
    dm.setTextColor(TFT_WHITE); dm.println("New PIN (4+ chars, any keyboard):");
    if (!promptPin("  New: ", p1, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (strlen(p1) < 4) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Min 4 characters.");
        dm.setTextColor(TFT_WHITE); return;
    }
    dm.println("");
    if (!promptPin("  Confirm: ", p2, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (strcmp(p1, p2) != 0) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("PINs don't match.");
        dm.setTextColor(TFT_WHITE); return;
    }
    genSalt(_saltHex);
    hashPin(p1, _saltHex, _hashHex);
    _hasPassword = true;
    dm.println("");
    if (!saveConfig()) {
        dm.setTextColor(TFT_YELLOW); dm.println("No SD — PIN active this session only.");
    }
    dm.setTextColor(TFT_GREEN); dm.println("PIN set. Type 'lock' to lock the screen.");
    dm.setTextColor(TFT_WHITE);
}

void LockScreenManager::cmdUpdate() {
    DisplayManager& dm = displayManager;
    if (!_hasPassword) {
        dm.setTextColor(TFT_RED); dm.println("No PIN set. Use 'lock new' first.");
        dm.setTextColor(TFT_WHITE); return;
    }
    char old[17] = {}, p1[17] = {}, p2[17] = {};
    dm.setTextColor(TFT_WHITE); dm.println("Change PIN:");
    if (!promptPin("  Current: ", old, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (!checkPin(old)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Wrong PIN.");
        dm.setTextColor(TFT_WHITE); return;
    }
    dm.println("");
    if (!promptPin("  New: ", p1, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (strlen(p1) < 4) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Min 4 characters.");
        dm.setTextColor(TFT_WHITE); return;
    }
    dm.println("");
    if (!promptPin("  Confirm: ", p2, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (strcmp(p1, p2) != 0) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("PINs don't match.");
        dm.setTextColor(TFT_WHITE); return;
    }
    genSalt(_saltHex);
    hashPin(p1, _saltHex, _hashHex);
    dm.println("");
    if (!saveConfig()) {
        dm.setTextColor(TFT_YELLOW); dm.println("No SD — PIN active this session only.");
    }
    dm.setTextColor(TFT_GREEN); dm.println("PIN updated.");
    dm.setTextColor(TFT_WHITE);
}

void LockScreenManager::cmdClean() {
    DisplayManager& dm = displayManager;
    if (!_hasPassword) {
        dm.setTextColor(TFT_YELLOW); dm.println("No PIN set.");
        dm.setTextColor(TFT_WHITE); return;
    }
    char old[17] = {};
    dm.setTextColor(TFT_WHITE); dm.println("Remove PIN:");
    if (!promptPin("  Current PIN: ", old, 17)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (!checkPin(old)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Wrong PIN.");
        dm.setTextColor(TFT_WHITE); return;
    }
    _hashHex[0] = '\0'; _saltHex[0] = '\0'; _hasPassword = false;
    dm.println("");
    if (!saveConfig()) {
        dm.setTextColor(TFT_YELLOW); dm.println("No SD — change active this session only.");
    }
    dm.setTextColor(TFT_GREEN); dm.println("PIN removed. Screen lock disabled.");
    dm.setTextColor(TFT_WHITE);
}

void LockScreenManager::cmdWipe() {
    DisplayManager& dm = displayManager;
    if (_hasPassword) {
        dm.setTextColor(TFT_RED);
        dm.println("PIN is active. Wipe only works after booting");
        dm.println("without SD (remove SD + reboot, then run wipe).");
        dm.setTextColor(TFT_WHITE); return;
    }
    // SD accessible right now — delete the file directly
    if (sdCardManager.canAccessSD()) {
        SD.remove("/lockscreen.conf");
        dm.setTextColor(TFT_GREEN); dm.println("PIN config removed from SD.");
        dm.println("Run 'lock new' to set a new PIN.");
        dm.setTextColor(TFT_WHITE); return;
    }
    // SD absent — schedule wipe via NVS; executes on next boot with SD inserted
    Preferences prefs;
    prefs.begin("lockscreen", false);
    prefs.putBool("wipe", true);
    prefs.end();
    dm.setTextColor(TFT_YELLOW);
    dm.println("Wipe scheduled in NVS.");
    dm.println("Insert SD card and reboot to apply.");
    dm.setTextColor(TFT_WHITE);
}

void LockScreenManager::cmdTimeout(const char* arg) {
    DisplayManager& dm = displayManager;
    if (!arg) {
        char buf[40];
        if (_timeout == 0) strncpy(buf, "Auto-lock: off", sizeof(buf));
        else               snprintf(buf, sizeof(buf), "Auto-lock: %lus idle", (unsigned long)_timeout);
        dm.setTextColor(TFT_WHITE); dm.println(buf); return;
    }
    int val = atoi(arg);
    if (val < 0) val = 0;
    _timeout        = (uint32_t)val;
    _lastActivityMs = millis();
    if (!saveConfig()) {
        dm.setTextColor(TFT_YELLOW); dm.println("No SD — timeout active this session only.");
    }
    char buf[48];
    if (_timeout == 0) strncpy(buf, "Auto-lock disabled.", sizeof(buf));
    else               snprintf(buf, sizeof(buf), "Auto-lock: %us of inactivity.", (unsigned)_timeout);
    dm.setTextColor(TFT_GREEN); dm.println(buf); dm.setTextColor(TFT_WHITE);
}

void LockScreenManager::cmdStatus() {
    DisplayManager& dm = displayManager;
    dm.setTextColor(TFT_CYAN);  dm.printText("Lock:    ");
    dm.setTextColor(TFT_WHITE); dm.println(_locked ? "LOCKED" : "unlocked");
    dm.setTextColor(TFT_CYAN);  dm.printText("PIN:     ");
    dm.setTextColor(TFT_WHITE); dm.println(_hasPassword ? "set" : "not set");
    dm.setTextColor(TFT_CYAN);  dm.printText("Timeout: ");
    dm.setTextColor(TFT_WHITE);
    if (_timeout == 0) {
        dm.println("off");
    } else {
        char b[16]; snprintf(b, sizeof(b), "%lus", (unsigned long)_timeout); dm.println(b);
    }
}
