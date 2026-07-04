// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Undercover passphrase config — SHA-256(salt+phrase) stored in
// /config/undercover.conf, same pattern as lockscreen PIN.
// Loaded lazily on first use; explicit ucLoadConfig() to force a refresh.

#ifndef UNDERCOVER_CONFIG_H
#define UNDERCOVER_CONFIG_H

#include <stdint.h>

// Returns true if a passphrase hash is currently loaded.
bool ucLoadConfig();
bool ucHasPassphrase();
int  ucPhraseLen();                        // byte length of stored phrase; 0 if unset

// Set: gen salt, hash, save.  Returns false if SD save failed (active in RAM only).
bool ucSetPassphrase(const char* phrase);

// Clear: wipe in-RAM state, rewrite config (empty hash/salt/len).
bool ucClearPassphrase();

// True iff SHA-256(salt+candidate) matches stored hash and len matches.
bool ucCheckPhrase(const char* candidate);

// Boot-cover: device boots directly into the Notes disguise when enabled.
bool ucBootCoverEnabled();
bool ucSetBootCover(bool on);   // persists to SD; returns false if SD save failed

// Panic key: a single keyboard byte that instantly drops the device into the
// Notes cover from anywhere (even mid-command). Default '@'. 0 = disabled.
// Only actually fires when a passphrase is set (so there's always a way back).
uint8_t ucPanicKey();
bool    ucSetPanicKey(uint8_t key);   // persists to SD; returns false if SD save failed

#endif // UNDERCOVER_CONFIG_H
