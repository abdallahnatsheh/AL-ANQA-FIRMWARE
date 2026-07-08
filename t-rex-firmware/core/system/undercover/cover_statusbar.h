// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// cover_statusbar — the ONE "phone" status bar shared by the undercover covers
// (the `home` launcher and the notes app it opens), so both show the identical
// bar and it can't drift: real HH:MM (ClockManager), "CRIMSON MOBILE", signal
// bars, and the real battery with a charging bolt. Height = COVER_SB_H.

#ifndef COVER_STATUSBAR_H
#define COVER_STATUSBAR_H

#include "display_manager.h"   // LGFX/lgfx types + SCREEN_WIDTH

static const int COVER_SB_H = 22;

// Draw the status bar into y=0..COVER_SB_H of G, using the caller's VLW meta font.
// Battery is read through an internal 10 s cache (getPct() samples the ADC 20x).
void drawCoverStatusBar(lgfx::LovyanGFX* G, lgfx::VLWfont* metaFont);

#endif // COVER_STATUSBAR_H
