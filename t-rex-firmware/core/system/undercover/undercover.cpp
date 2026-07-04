// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "undercover.h"
#include "undercover_config.h"
#include "notes_ui.h"
#include "display_manager.h"
#include "input_handling.h"
#include <Arduino.h>
#include <string.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// The one flag the whole feature hangs off. `volatile` because background
// pollers (wguard/macwatch/espchat, run inside getKeyboardInput) read it from
// their own execution context to decide whether to stay silent.
volatile bool g_covert = false;

// True only while `uc panic set` is capturing a key. Read by the panic hook in
// getKeyboardInput() so pressing the CURRENT panic key during capture gets read
// as the new key instead of firing the cover. Same task as the hook → no race.
volatile bool g_ucCapturingPanic = false;

// ── Passphrase prompt (same look as lock new/update) ─────────────────────────

static bool promptPhrase(const char* label, char* buf, uint8_t maxLen) {
    DisplayManager& dm = displayManager;
    uint8_t len = 0;
    buf[0] = '\0';
    dm.setTextColor(TFT_CYAN); dm.printText(label);
    int32_t px = dm.getCursorX(), py = dm.getCursorY();

    auto redraw = [&]() {
        dm.fillRect(px, py, SCREEN_WIDTH - px - 4, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(px, py);
        dm.setTextColor(TFT_YELLOW);
        for (uint8_t i = 0; i < len; i++) dm.printText("* ");
        dm.setTextColor(0x4208); dm.printText("_");
    };
    redraw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == '\x1B')            return false;
        if (k == '\r' || k == '\n') return true;
        if ((k == '\x08' || k == '\x7F') && len > 0)
            { buf[--len] = '\0'; redraw(); }
        else if (k >= 0x20 && k < 0x7F && len < maxLen - 1)
            { buf[len++] = k; buf[len] = '\0'; redraw(); }
        delay(10);
    }
}

// ── Panic key ─────────────────────────────────────────────────────────────────

// Keys that already carry meaning — the panic trigger must not shadow them.
static bool ucPanicReserved(char k) {
    return k == '\''                                   // KEY_AUTOCOMPLETE
        || k == 'q' || k == 'Q'                        // cover fallback exit
        || k == ' '                                    // too easy to hit by accident
        || k == '\r' || k == '\n' || k == '\b' || k == '\x7F';
}

static void cmdUcPanic(const char* arg) {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();

    if (arg && strncmp(arg, "off", 3) == 0) {
        bool saved = ucSetPanicKey(0);
        dm.setTextColor(TFT_GREEN);
        dm.println(saved ? "Panic key disabled."
                         : "Panic key disabled (no SD — this session only).");
        dm.setTextColor(TFT_WHITE);
        return;
    }
    if (!arg || strncmp(arg, "set", 3) != 0) {
        dm.setTextColor(TFT_RED); dm.println("Usage: uc panic set|off");
        dm.setTextColor(TFT_WHITE); return;
    }

    dm.setTextColor(TFT_WHITE);
    dm.println("Press the key for instant-hide:");
    dm.setTextColor(0x7BEF);
    dm.println("  blocked: ' q space Enter Bksp  (trackball click = cancel)");
    dm.setTextColor(TFT_WHITE);

    g_ucCapturingPanic = true;   // suppress the panic hook while we read the key
    while (true) {
        if (inputHandler.getTrackballEvent() == TBALL_CLICK) {
            g_ucCapturingPanic = false;
            dm.setTextColor(TFT_YELLOW); dm.println("Cancelled.");
            dm.setTextColor(TFT_WHITE); return;
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 0) { delay(10); continue; }
        if (k < 0x20 || k >= 0x7F) continue;           // printable only
        if (ucPanicReserved(k)) {
            char b[48]; snprintf(b, sizeof(b), "  '%c' is reserved — pick another.", k);
            dm.setTextColor(TFT_RED); dm.println(b); dm.setTextColor(TFT_WHITE);
            continue;
        }
        g_ucCapturingPanic = false;
        bool saved = ucSetPanicKey((uint8_t)k);
        char b[48]; snprintf(b, sizeof(b), "Panic key set to '%c'.", k);
        dm.setTextColor(TFT_GREEN); dm.println(b);
        if (!saved) { dm.setTextColor(TFT_YELLOW); dm.println("(no SD — this session only)"); }
        if (!ucHasPassphrase()) {
            dm.setTextColor(TFT_YELLOW);
            dm.println("Set an exit passphrase (uc set) — panic won't fire without one.");
        }
        dm.setTextColor(TFT_WHITE);
        return;
    }
}

// ── Subcommands ───────────────────────────────────────────────────────────────

static void cmdUcSet() {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();
    if (ucHasPassphrase()) {
        dm.setTextColor(TFT_RED);
        dm.println("Passphrase already set. Use 'uc clear' first.");
        dm.setTextColor(TFT_WHITE); return;
    }
    char p1[33] = {}, p2[33] = {};
    dm.setTextColor(TFT_WHITE);
    dm.println("New exit passphrase (4-32 chars, any keyboard):");
    if (!promptPhrase("  New: ", p1, 33)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    dm.println("");
    if (strlen(p1) < 4) {
        dm.setTextColor(TFT_RED); dm.println("Min 4 characters.");
        dm.setTextColor(TFT_WHITE); return;
    }
    if (!promptPhrase("  Confirm: ", p2, 33)) {
        dm.println(""); dm.setTextColor(TFT_RED); dm.println("Cancelled.");
        dm.setTextColor(TFT_WHITE); return;
    }
    dm.println("");
    if (strcmp(p1, p2) != 0) {
        dm.setTextColor(TFT_RED); dm.println("Mismatch — not saved.");
        dm.setTextColor(TFT_WHITE); return;
    }
    bool saved = ucSetPassphrase(p1);
    dm.setTextColor(TFT_GREEN);
    dm.println(saved ? "Passphrase set."
                     : "Passphrase set (no SD — active this session only).");
    dm.setTextColor(TFT_WHITE);
}

static void cmdUcClear() {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();
    if (!ucHasPassphrase()) {
        dm.setTextColor(TFT_YELLOW); dm.println("No passphrase set.");
        dm.setTextColor(TFT_WHITE); return;
    }
    bool saved = ucClearPassphrase();
    dm.setTextColor(TFT_GREEN);
    dm.println(saved ? "Passphrase cleared."
                     : "Passphrase cleared (SD update failed — will reload on next boot).");
    dm.setTextColor(TFT_WHITE);
}

static void cmdUcStatus() {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();
    dm.setTextColor(TFT_WHITE); dm.printText("Passphrase : ");
    if (ucHasPassphrase()) {
        char buf[32]; snprintf(buf, sizeof(buf), "SET (%d chars)", ucPhraseLen());
        dm.setTextColor(TFT_GREEN); dm.println(buf);
    } else {
        dm.setTextColor(TFT_YELLOW); dm.println("not set  (use 'uc set')");
    }
    dm.setTextColor(TFT_WHITE); dm.printText("Boot cover : ");
    if (ucBootCoverEnabled()) {
        dm.setTextColor(TFT_GREEN); dm.println("ON  — boots into Notes disguise");
    } else {
        dm.setTextColor(TFT_YELLOW); dm.println("off (use 'uc boot on')");
    }
    dm.setTextColor(TFT_WHITE); dm.printText("Panic key  : ");
    uint8_t pk = ucPanicKey();
    if (pk == 0) {
        dm.setTextColor(TFT_YELLOW); dm.println("off (use 'uc panic set')");
    } else if (!ucHasPassphrase()) {
        char b[48]; snprintf(b, sizeof(b), "'%c' set — inactive (no passphrase)", pk);
        dm.setTextColor(TFT_YELLOW); dm.println(b);
    } else {
        char b[40]; snprintf(b, sizeof(b), "'%c'  — armed (instant hide)", pk);
        dm.setTextColor(TFT_GREEN); dm.println(b);
    }
    dm.setTextColor(TFT_WHITE);
}

static void cmdUcBoot(const char* arg) {
    DisplayManager& dm = displayManager;
    dm.setDefaultTextSize();
    if (!arg || (*arg != 'o')) {
        dm.setTextColor(TFT_RED); dm.println("Usage: uc boot on|off");
        dm.setTextColor(TFT_WHITE); return;
    }
    bool on = (strncmp(arg, "on", 2) == 0);
    bool saved = ucSetBootCover(on);
    dm.setTextColor(TFT_GREEN);
    if (on)
        dm.println(saved ? "Boot cover ON — device boots into Notes disguise."
                         : "Boot cover ON (no SD — active this session only).");
    else
        dm.println(saved ? "Boot cover OFF."
                         : "Boot cover OFF (no SD — active this session only).");
    dm.setTextColor(TFT_WHITE);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runUndercover(char* args) {
    if (args && *args) {
        char buf[40]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        char* sub  = strtok(buf, " ");
        char* arg2 = strtok(nullptr, " ");
        if      (!sub)                    {}
        else if (strcmp(sub, "set")    == 0) { cmdUcSet();          displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "clear")  == 0) { cmdUcClear();        displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "status") == 0) { cmdUcStatus();       displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "boot")   == 0) { cmdUcBoot(arg2);     displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "panic")  == 0) { cmdUcPanic(arg2);    displayManager.printCommandScreen(); return; }
        else {
            displayManager.setDefaultTextSize();
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Usage: uc [set|clear|status|boot on|off|panic set|off]");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printCommandScreen();
            return;
        }
    }

    // No args → enter cover. Reload passphrase from SD so it's always current.
    ucLoadConfig();
    g_covert = true;
    runNotesUi();        // blocks until secret-passphrase match OR q (q kept for now)
    g_covert = false;
}

// Called from setup() after setupCommands(). If boot_cover is enabled, enters the
// cover immediately so the device boots into the Notes disguise instead of the CLI.
void ucInit() {
    ucLoadConfig();
    if (!ucBootCoverEnabled()) return;
    g_covert = true;
    runNotesUi();   // blocks; runNotesUi() restores display + calls printCommandScreen on exit
    g_covert = false;
}
