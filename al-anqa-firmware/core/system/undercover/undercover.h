// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Undercover mode — `uc`/`undercover` drops the device into a silent Notes
// disguise. Subcommands:
//   uc              — enter the cover (blocks until secret exit or q)
//   uc set          — set the secret exit passphrase (SHA-256, SD-backed)
//   uc clear        — remove the passphrase
//   uc status       — show whether a passphrase / boot-cover is configured
//   uc boot on|off  — enable/disable boot-cover (boots into Notes from cold start)
//   uc panic set|off— set/disable the instant-hide key (default '@', fires anywhere)
//
// g_covert is raised for the whole cover session so every sound/visual tell
// (notifications, direct I2S beeps) goes silent regardless of which background
// tools are running underneath.

#ifndef UNDERCOVER_H
#define UNDERCOVER_H

#include "covert.h"
#include "undercover_config.h"

// Set only while `uc panic set` reads the new key — the panic hook in
// getKeyboardInput() checks this so the current panic key can be re-captured
// without firing the cover.
extern volatile bool g_ucCapturingPanic;

void ucInit();              // call from setup() after setupCommands(); boots into cover if boot_cover=1
void runUndercover(char* args);

#endif // UNDERCOVER_H
