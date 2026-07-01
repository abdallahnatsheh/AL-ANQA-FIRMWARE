// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Undercover mode — Phase 1 of PLAN-undercover-touch. `uc`/`undercover` drops
// the device into a silent Notes disguise: raises g_covert (so every leak point
// goes quiet) and shows the Notes cover UI. Exit returns to the CLI.
//
// Phase 1a scope: deliberate `uc` entry + the sound-leak audit. NOT yet built:
// panic-chord entry (fire mid-command), secret-passphrase exit, duress/decoy.

#ifndef UNDERCOVER_H
#define UNDERCOVER_H

#include "covert.h"

void runUndercover();   // `uc` — enter the silent cover, block until exit

#endif // UNDERCOVER_H
