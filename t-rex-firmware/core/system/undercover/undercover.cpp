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
    dm.setTextColor(TFT_WHITE); dm.printText("Undercover passphrase: ");
    if (ucHasPassphrase()) {
        char buf[32]; snprintf(buf, sizeof(buf), "SET (%d chars)", ucPhraseLen());
        dm.setTextColor(TFT_GREEN); dm.println(buf);
    } else {
        dm.setTextColor(TFT_YELLOW); dm.println("not set  (use 'uc set')");
    }
    dm.setTextColor(TFT_WHITE);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runUndercover(char* args) {
    if (args && *args) {
        char buf[32]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        char* sub = strtok(buf, " ");
        if      (!sub)                    {}
        else if (strcmp(sub, "set")    == 0) { cmdUcSet();    displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "clear")  == 0) { cmdUcClear();  displayManager.printCommandScreen(); return; }
        else if (strcmp(sub, "status") == 0) { cmdUcStatus(); displayManager.printCommandScreen(); return; }
        else {
            displayManager.setDefaultTextSize();
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Usage: uc [set|clear|status]");
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
