// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// cover_kit — see cover_kit.h. Shared shell for the undercover covers.

#include "cover_kit.h"
#include "notes_fonts.h"          // baked Noto Sans VLW smooth fonts (convert_font.py)
#include "undercover_config.h"    // ucHasPassphrase / ucPhraseLen / ucCheckPhrase
#include "powersave_manager.h"
#include "input_handling.h"
#include <Arduino.h>
#include <string.h>

extern LGFX          tft;
extern InputHandling inputHandler;

namespace cover {

// ── Double-buffer sprite (PSRAM) ─────────────────────────────────────────────
static LGFX_Sprite s_canvas(&tft);
lgfx::LovyanGFX*   G = &tft;
static bool        s_have = false;

// ── Baked fonts (loaded once per session) ────────────────────────────────────
lgfx::VLWfont               fBig, fTitle, fBody, fMeta;
static lgfx::PointerWrapper wBig, wTitle, wBody, wMeta;
static bool                 s_fontsLoaded = false;

static void loadFonts() {
    if (s_fontsLoaded) return;
    wBig.set(NOTES_FONT_BIG,     sizeof(NOTES_FONT_BIG));
    wTitle.set(NOTES_FONT_TITLE, sizeof(NOTES_FONT_TITLE));
    wBody.set(NOTES_FONT_BODY,   sizeof(NOTES_FONT_BODY));
    wMeta.set(NOTES_FONT_META,   sizeof(NOTES_FONT_META));
    fBig.loadFont(&wBig);
    fTitle.loadFont(&wTitle);
    fBody.loadFont(&wBody);
    fMeta.loadFont(&wMeta);
    s_fontsLoaded = true;
}
static void unloadFonts() {
    if (!s_fontsLoaded) return;
    fBig.unloadFont();  fTitle.unloadFont();
    fBody.unloadFont(); fMeta.unloadFont();
    s_fontsLoaded = false;
}

bool haveCanvas() { return s_have; }
void flush()      { if (s_have) s_canvas.pushSprite(&tft, 0, 0); }

void setupCanvas() {
    s_canvas.setPsram(true);
    s_canvas.setColorDepth(16);
    s_have = s_canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    G = s_have ? (lgfx::LovyanGFX*)&s_canvas : (lgfx::LovyanGFX*)&tft;
    loadFonts();
}
void teardownCanvas() {
    unloadFonts();
    if (s_have) { s_canvas.deleteSprite(); s_have = false; }
    G = &tft;
    tft.setFont(&fonts::Font0);
    tft.setTextDatum(textdatum_t::top_left);
}

// ── Secret-passphrase rolling buffer ─────────────────────────────────────────
// Records the raw edited keystroke stream (backspaces applied) and checks whether
// its last `plen` characters equal the passphrase. Handling backspace is the key
// fix: previously a typo+correction while typing the phrase left stale characters
// in the window, so the match silently failed even though the note showed the
// right text. The buffer is larger than the phrase so a correction near the end
// still lands on a matching suffix.
static char s_kbuf[65] = {};
static int  s_kpos     = 0;

void resetPassphrase() { s_kbuf[0] = '\0'; s_kpos = 0; }

bool feedPassphrase(char k) {
    if (!ucHasPassphrase()) return false;
    int plen = ucPhraseLen();
    if (plen <= 0 || plen > 32) return false;
    if (k == 0x08 || k == 0x7F) {                 // backspace/delete → undo last char
        if (s_kpos > 0) s_kbuf[--s_kpos] = '\0';
        return false;
    }
    if (k < 0x20 || k >= 0x7F) return false;       // ignore Enter / control keys
    if (s_kpos >= (int)sizeof(s_kbuf) - 1) {       // full → roll one char
        memmove(s_kbuf, s_kbuf + 1, sizeof(s_kbuf) - 2);
        s_kpos = (int)sizeof(s_kbuf) - 2;
    }
    s_kbuf[s_kpos++] = k;
    s_kbuf[s_kpos]   = '\0';
    if (s_kpos < plen) return false;
    return ucCheckPhrase(s_kbuf + s_kpos - plen);  // check the last plen chars
}

// ── Touch-wake (undercover only) ─────────────────────────────────────────────
void handleTouchWake(const TouchEvent& te, uint32_t& lastTapMs) {
    if (te.type == TouchEvent::NONE) return;
    inputHandler.updateActivity();
    PowerSaveManager& psm = PowerSaveManager::getInstance();
    if (psm.isManualOff()) return;
    if (psm.isScreenOff()) {
        // Fully off: double-tap (two TAPs within 500 ms) to wake.
        if (te.type == TouchEvent::TAP) {
            uint32_t now = millis();
            if (lastTapMs && (now - lastTapMs) < 500) { psm.updateActivity(); lastTapMs = 0; }
            else lastTapMs = now;
        }
    } else {
        // Half-dimmed: any touch wakes.
        psm.updateActivity(); lastTapMs = 0;
    }
}

} // namespace cover
