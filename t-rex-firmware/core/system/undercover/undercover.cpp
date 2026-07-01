// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "undercover.h"
#include "notes_ui.h"

// The one flag the whole feature hangs off. `volatile` because background
// pollers (wguard/macwatch/espchat, run inside getKeyboardInput) read it from
// their own execution context to decide whether to stay silent.
volatile bool g_covert = false;

// Deliberate entry. The Notes cover is a blocking session that already draws
// its own chrome and calls displayManager.setBlocked(true) (hides the real
// status bar + any background popup/shield that honours isBlocked). Raising
// g_covert around it adds the AUDIO silence that display blocking can't: while
// covert, NotificationManager::notify() and the hiddenssid beep no-op, so a
// wguard/macwatch/espchat background alert can't beep through the disguise.
// Passive tools keep running and logging to SD underneath — only the tells go
// quiet.
void runUndercover() {
    g_covert = true;
    runNotesUi();          // blocks until the user exits the cover (q for now)
    g_covert = false;
}
