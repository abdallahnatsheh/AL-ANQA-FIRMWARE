// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Undercover mode — `uc`/`undercover` drops the device into a silent Notes
// disguise. Subcommands:
//   uc          — enter the cover (blocks until secret exit or q)
//   uc set      — set the secret exit passphrase (SHA-256, SD-backed)
//   uc clear    — remove the passphrase
//   uc status   — show whether a passphrase is configured
//
// g_covert is raised for the whole cover session so every sound/visual tell
// (notifications, direct I2S beeps) goes silent regardless of which background
// tools are running underneath.

#ifndef UNDERCOVER_H
#define UNDERCOVER_H

#include "covert.h"

void runUndercover(char* args);

#endif // UNDERCOVER_H
