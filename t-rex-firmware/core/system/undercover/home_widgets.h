// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_widgets — Ui, the shared widget toolkit ("widget manager") for the home
// launcher and its apps. It wraps the cover_kit graphics context (the shared PSRAM
// sprite + baked fonts) with a dark palette and a set of reusable widgets — status
// bar, app bar + back chevron, action buttons, on/off toggle, list rows, signal
// bars, tile icons — plus the shared alarm banner + repeating ring used by the
// Clock and Reminders apps. Apps get a Ui& injected and draw through it, so the
// styling and the shared chrome live in exactly one place.

#ifndef HOME_WIDGETS_H
#define HOME_WIDGETS_H

#include "cover_kit.h"            // cover::G + shared fonts, SCREEN_WIDTH/HEIGHT
#include "notification_manager.h" // NotifLevel for the alarm center

// ── Shared layout constants (used by Ui + every app) ─────────────────────────
#define UI_SB_H       22                       // phone status bar height
#define UI_APPBAR_H   32                       // sub-app title bar (back chevron + title)
#define UI_CONTENT_Y  (UI_SB_H + UI_APPBAR_H)  // where app content begins (54)
#define UI_FAB_R      20
#define UI_FAB_CX     (SCREEN_WIDTH - 16 - UI_FAB_R)
#define UI_FAB_CY     (SCREEN_HEIGHT - 16 - UI_FAB_R)

class Ui {
public:
    void init();   // build the palette (call once cover::G is live)

    // ── graphics context / fonts ─────────────────────────────────────────────
    lgfx::LovyanGFX* g()      const { return cover::G; }
    lgfx::VLWfont*   fBig()   { return &cover::fBig; }
    lgfx::VLWfont*   fTitle() { return &cover::fTitle; }
    lgfx::VLWfont*   fBody()  { return &cover::fBody; }
    lgfx::VLWfont*   fMeta()  { return &cover::fMeta; }
    uint16_t         col(uint32_t rgb) const;   // 0xRRGGBB → RGB565
    void             present() { cover::flush(); }  // blit the composed frame now (for blocking ops)

    // ── dark palette ─────────────────────────────────────────────────────────
    uint16_t bg, bar, ink, muted, hair, badge, sel, white, sun, shadow;

    // ── chrome ───────────────────────────────────────────────────────────────
    void statusBar();                            // the shared phone status bar
    void appBar(const char* title);              // back chevron + title
    static bool hitAppBack(int x, int y);

    // ── controls ─────────────────────────────────────────────────────────────
    void twoButtons(const char* a, const char* b, uint16_t aCol);  // bottom A/B buttons
    static bool hitBtnA(int x, int y);
    static bool hitBtnB(int x, int y);
    void toggle(int x, int y, int w, int h, bool on);              // on/off switch

    // ── misc widgets ─────────────────────────────────────────────────────────
    void signalBars(int x, int cy, int rssi);
    void lockGlyph(int x, int cy);
    void wifiGlyph(int cx, int cy, uint16_t c);
    void tileIcon(int cx, int cy, char code, uint16_t accent);     // app-tile glyphs
    void weatherIcon(int cx, int cy, int wmoCode);                 // hero + Weather app

    // ── shared alarm banner + repeating ring (Clock / Reminders) ─────────────
    void raiseAlarm(NotifLevel lvl, const char* text);   // start ringing + show banner
    void tickAlarm();                                    // re-ring up to 5x
    void dismissAlarm();                                 // stop ring + clear banner
    bool alarmShowing() const;
    void drawBanner();                                   // overlay, drawn last each frame

private:
    // repeating-ring state
    bool       _ringActive = false;
    NotifLevel _ringLevel  = NOTIF_INFO;
    int        _ringCount  = 0;
    uint32_t   _ringNextMs = 0;
    uint32_t   _bannerMs   = 0;
    char       _bannerText[40] = {};
};

#endif // HOME_WIDGETS_H
