// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// cover_kit — the shared "cover shell" for the undercover disguises (the home
// launcher and the notes cover). It owns the one PSRAM back-buffer sprite, the
// baked Noto VLW fonts, the secret-passphrase rolling buffer, and the touch-wake
// handling, so the covert plumbing lives in ONE place instead of being mirrored
// across home_ui.cpp and notes_ui.cpp. Only one cover is ever active at a time,
// so a single shared sprite + font set is safe (and avoids a 2x150KB PSRAM peak).
//
// Each cover keeps its own palette + screen logic; it just draws through cover::G
// and switches cover::f* fonts. Typical use:
//   cover::setupCanvas();  <cover's own initColors()>  ... draw ...  cover::flush();
//   in the loop:  if (cover::feedPassphrase(k)) { exit }
//                 cover::handleTouchWake(te, lastTapMs);
//   on exit:      cover::teardownCanvas();

#ifndef COVER_KIT_H
#define COVER_KIT_H

#include "display_manager.h"   // LGFX / LGFX_Sprite / lgfx types + SCREEN_WIDTH/HEIGHT
#include "touch_manager.h"     // TouchEvent

namespace cover {
    extern lgfx::LovyanGFX* G;                 // current draw target (sprite, or panel on fallback)
    extern lgfx::VLWfont    fBig, fTitle, fBody, fMeta;   // shared baked fonts

    void setupCanvas();          // create PSRAM sprite + load fonts; sets G
    void teardownCanvas();       // unload fonts + delete sprite; G = panel, Font0 restored
    void flush();                // blit the sprite to the panel
    bool haveCanvas();

    void resetPassphrase();      // clear the rolling buffer (call at session start)
    bool feedPassphrase(char k); // true iff the stored passphrase just matched

    // Undercover screen-off double-tap / half-dim single-tap wake. Updates
    // PowerSaveManager; lastTapMs is the caller's own double-tap timer.
    void handleTouchWake(const TouchEvent& te, uint32_t& lastTapMs);
}

#endif // COVER_KIT_H
