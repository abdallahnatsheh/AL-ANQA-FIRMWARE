// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// g_covert — the spine of undercover mode. When true the device is wearing its
// disguise and must emit NOTHING that gives it away: no notification sounds, no
// direct I2S beeps, no LED/vibration. Every audible/visible leak checks this
// flag. Kept in its own tiny header so leak-point files can include just the
// flag without pulling in the whole undercover module.
//
// (Visual tells are additionally covered by displayManager.setBlocked(true)
// while the cover is foreground; g_covert is what silences AUDIO, which bypasses
// display blocking, and is the durable "we are covert" signal.)

#ifndef COVERT_H
#define COVERT_H

extern volatile bool g_covert;

#endif // COVERT_H
