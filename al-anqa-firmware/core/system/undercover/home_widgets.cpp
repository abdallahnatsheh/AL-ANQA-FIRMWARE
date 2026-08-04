// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_widgets — see home_widgets.h. Implementation of the Ui widget toolkit.

#include "home_widgets.h"
#include "cover_statusbar.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ── alarm timing ─────────────────────────────────────────────────────────────
#define BANNER_MS  12000
#define RING_MAX   5
#define RING_GAP   1300

void Ui::init() {
    auto* G  = cover::G;
    bg      = G->color565(0x0b, 0x0d, 0x11);
    bar     = G->color565(0x17, 0x1b, 0x22);
    ink     = G->color565(0xe8, 0xea, 0xed);
    muted   = G->color565(0x7d, 0x82, 0x8b);
    hair    = G->color565(0x24, 0x28, 0x30);
    badge   = G->color565(0xe5, 0x3e, 0x3e);
    sel     = G->color565(0x3a, 0x82, 0xf6);
    white   = G->color565(0xff, 0xff, 0xff);
    sun     = G->color565(0xff, 0xc1, 0x07);
    shadow  = G->color565(0x04, 0x05, 0x07);
}

uint16_t Ui::col(uint32_t rgb) const {
    return cover::G->color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ── chrome ───────────────────────────────────────────────────────────────────
void Ui::statusBar() {
    drawCoverStatusBar(cover::G, &cover::fMeta);
}

void Ui::appBar(const char* title) {
    auto* G = cover::G;
    G->fillScreen(bg);          // clear the whole app canvas first (bar + content area)
    statusBar();
    G->fillRect(0, UI_SB_H, SCREEN_WIDTH, UI_APPBAR_H, bg);
    G->drawFastHLine(0, UI_SB_H + UI_APPBAR_H - 1, SCREEN_WIDTH, hair);
    int chx = 14, chy = UI_SB_H + UI_APPBAR_H / 2;
    G->drawWideLine(chx + 7, chy - 7, chx, chy, 2, ink);
    G->drawWideLine(chx, chy, chx + 7, chy + 7, 2, ink);
    G->setFont(&cover::fTitle);
    G->setTextColor(ink);
    G->setTextDatum(textdatum_t::top_left);
    G->drawString(title, 34, UI_SB_H + 8);
}
bool Ui::hitAppBack(int x, int y) {
    return y >= UI_SB_H && y < UI_SB_H + UI_APPBAR_H && x <= 44;
}

// ── controls ──────────────────────────────────────────────────────────────────
void Ui::twoButtons(const char* a, const char* b, uint16_t aCol) {
    auto* G = cover::G;
    int by = SCREEN_HEIGHT - 44, bh = 34, bw = (SCREEN_WIDTH - 34) / 2;
    G->fillSmoothRoundRect(14, by, bw, bh, 10, aCol);
    G->fillSmoothRoundRect(20 + bw, by, bw, bh, 10, bar);
    G->setFont(&cover::fTitle);
    G->setTextDatum(textdatum_t::middle_center);
    G->setTextColor(bg);  G->drawString(a, 14 + bw / 2, by + bh / 2);
    G->setTextColor(ink); G->drawString(b, 20 + bw + bw / 2, by + bh / 2);
    G->setTextDatum(textdatum_t::top_left);
}
bool Ui::hitBtnA(int x, int y) { int by = SCREEN_HEIGHT - 44, bh = 34, bw = (SCREEN_WIDTH - 34) / 2; return y >= by && y <= by + bh && x >= 14 && x <= 14 + bw; }
bool Ui::hitBtnB(int x, int y) { int by = SCREEN_HEIGHT - 44, bh = 34, bw = (SCREEN_WIDTH - 34) / 2; return y >= by && y <= by + bh && x >= 20 + bw && x <= 20 + 2 * bw; }

void Ui::toggle(int x, int y, int w, int h, bool on) {
    auto* G = cover::G;
    G->fillSmoothRoundRect(x, y, w, h, h / 2, on ? col(0x34A853) : hair);
    G->fillSmoothCircle(on ? x + w - h / 2 : x + h / 2, y + h / 2, h / 2 - 2, white);
}

// ── misc widgets ──────────────────────────────────────────────────────────────
void Ui::signalBars(int x, int cy, int rssi) {
    auto* G = cover::G;
    int lvl = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        int hh = 4 + b * 3;
        G->fillRect(x + b * 5, cy + 6 - hh, 3, hh, b < lvl ? ink : hair);
    }
}
void Ui::lockGlyph(int x, int cy) {
    auto* G = cover::G;
    G->drawRoundRect(x + 2, cy - 4, 5, 6, 2, muted);   // shackle
    G->fillSmoothRoundRect(x, cy, 9, 7, 2, muted);     // body
}
void Ui::wifiGlyph(int cx, int cy, uint16_t c) {
    auto* G = cover::G;
    G->fillSmoothCircle(cx, cy + 7, 2, c);
    G->drawWideLine(cx - 4, cy + 3, cx, cy - 1, 2, c); G->drawWideLine(cx, cy - 1, cx + 4, cy + 3, 2, c);
    G->drawWideLine(cx - 8, cy,     cx, cy - 7, 2, c); G->drawWideLine(cx, cy - 7, cx + 8, cy,     2, c);
}

// White glyph centered at (cx,cy) on an accent tile (accent = the tile bg, used for
// negative-space cut-outs). All anti-aliased primitives — no font glyph dependency.
void Ui::tileIcon(int cx, int cy, char code, uint16_t accent) {
    auto* G = cover::G;
    uint16_t w = white;
    switch (code) {
        case 'K':                                        // calculator
            G->fillSmoothRoundRect(cx - 11, cy - 14, 22, 28, 3, w);
            G->fillRect(cx - 8, cy - 11, 16, 6, accent);
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                    G->fillRect(cx - 8 + c * 6, cy - 1 + r * 5, 3, 3, accent);
            break;
        case 'T':                                        // analog clock
            G->fillSmoothCircle(cx, cy, 13, w);
            G->drawCircle(cx, cy, 13, accent);
            G->drawWideLine(cx, cy, cx, cy - 8, 2, accent);
            G->drawWideLine(cx, cy, cx + 5, cy + 3, 2, accent);
            G->fillSmoothCircle(cx, cy, 2, accent);
            break;
        case 'R': {                                      // reminder bell
            G->fillSmoothCircle(cx, cy - 13, 2, w);
            G->fillSmoothRoundRect(cx - 7, cy - 11, 14, 12, 6, w);
            G->fillTriangle(cx - 7, cy - 1, cx - 11, cy + 5, cx + 11, cy + 5, w);
            G->fillTriangle(cx - 7, cy - 1, cx + 7, cy - 1, cx + 11, cy + 5, w);
            G->fillSmoothRoundRect(cx - 12, cy + 4, 24, 3, 1, w);
            G->fillSmoothCircle(cx, cy + 9, 2, w);
            break;
        }
        case 'W':                                        // weather sun
            G->fillSmoothCircle(cx, cy, 8, w);
            for (int a = 0; a < 8; a++) {
                float t = a * 3.14159f / 4.0f;
                G->drawWideLine(cx + (int)(cosf(t) * 11), cy + (int)(sinf(t) * 11),
                                cx + (int)(cosf(t) * 14), cy + (int)(sinf(t) * 14), 2, w);
            }
            break;
        case 'F': {                                      // flashlight
            G->fillSmoothRoundRect(cx - 8, cy - 13, 16, 4, 1, w);
            G->fillTriangle(cx - 8, cy - 9, cx + 8, cy - 9, cx + 6, cy - 3, w);
            G->fillTriangle(cx - 8, cy - 9, cx - 6, cy - 3, cx + 6, cy - 3, w);
            G->fillSmoothRoundRect(cx - 6, cy - 3, 12, 16, 2, w);
            G->fillRect(cx - 2, cy + 2, 4, 4, accent);
            G->drawWideLine(cx,     cy - 16, cx,     cy - 21, 2, w);
            G->drawWideLine(cx - 6, cy - 16, cx - 9, cy - 20, 2, w);
            G->drawWideLine(cx + 6, cy - 16, cx + 9, cy - 20, 2, w);
            break;
        }
        case 'N':                                        // notes document
            G->fillSmoothRoundRect(cx - 9, cy - 13, 18, 26, 2, w);
            for (int k = 0; k < 4; k++) G->drawFastHLine(cx - 5, cy - 7 + k * 4, 10, accent);
            break;
        case 'C': {                                      // calendar
            int W = 30, H = 27, x0 = cx - W / 2, y0 = cy - H / 2 + 2;
            uint16_t dk = G->color565(0x3a, 0x3f, 0x4a);
            G->fillSmoothRoundRect(x0, y0, W, H, 5, w);
            G->fillSmoothRoundRect(x0, y0, W, 9, 5, accent);
            G->fillRect(x0, y0 + 5, W, 4, accent);
            G->drawRoundRect(x0, y0, W, H, 5, dk);
            G->drawFastHLine(x0, y0 + 9, W, dk);
            G->fillSmoothRoundRect(cx - 8, cy - H / 2 - 3, 3, 8, 1, w);
            G->fillSmoothRoundRect(cx + 5, cy - H / 2 - 3, 3, 8, 1, w);
            int gx = x0 + 2, gy = y0 + 12, cw = 6, ch = 4;
            for (int c = 0; c <= 4; c++) G->drawFastVLine(gx + c * cw, gy, 3 * ch, dk);
            for (int r = 0; r <= 3; r++) G->drawFastHLine(gx, gy + r * ch, 4 * cw, dk);
            G->fillRect(gx + 2 * cw + 1, gy + ch + 1, cw - 1, ch - 1, accent);
            break;
        }
        case 'S':                                        // gear
            G->fillSmoothCircle(cx, cy, 11, w);
            for (int a = 0; a < 8; a++) {
                float t = a * 3.14159f / 4.0f;
                int ex = cx + (int)(cosf(t) * 13), ey = cy + (int)(sinf(t) * 13);
                G->fillRect(ex - 2, ey - 2, 4, 4, w);
            }
            G->fillSmoothCircle(cx, cy, 4, accent);
            break;
    }
}

// Cloud from three lobes + a flat base (anti-aliased).
static void wxCloud(lgfx::LovyanGFX* G, int cx, int cy, uint16_t c) {
    G->fillSmoothCircle(cx - 7, cy + 1, 6, c);
    G->fillSmoothCircle(cx + 6, cy + 1, 7, c);
    G->fillSmoothCircle(cx, cy - 3, 8, c);
    G->fillSmoothRoundRect(cx - 12, cy + 1, 25, 7, 3, c);
}
// Weather glyph by WMO weather-interpretation code (Open-Meteo).
void Ui::weatherIcon(int cx, int cy, int code) {
    auto* G = cover::G;
    if (code == 0) {                                     // clear → sun
        G->fillSmoothCircle(cx, cy, 8, sun);
        for (int a = 0; a < 8; a++) {
            float t = a * 3.14159f / 4.0f;
            G->drawWideLine(cx + (int)(cosf(t) * 11), cy + (int)(sinf(t) * 11),
                            cx + (int)(cosf(t) * 14), cy + (int)(sinf(t) * 14), 2, sun);
        }
        return;
    }
    if (code == 1 || code == 2) { G->fillSmoothCircle(cx + 7, cy - 7, 6, sun); wxCloud(G, cx, cy, white); return; }
    if (code == 3) { wxCloud(G, cx, cy, white); return; }
    if (code == 45 || code == 48) { wxCloud(G, cx, cy, muted); return; }
    if (code >= 95) { wxCloud(G, cx, cy - 2, muted); G->fillTriangle(cx - 2, cy + 5, cx + 4, cy + 5, cx - 1, cy + 13, sun); return; }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        wxCloud(G, cx, cy - 2, white);
        for (int i = -1; i <= 1; i++) G->fillSmoothCircle(cx + i * 6, cy + 11, 2, white);
        return;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        wxCloud(G, cx, cy - 2, white);
        for (int i = -1; i <= 1; i++) G->drawWideLine(cx + i * 6, cy + 8, cx + i * 6 - 2, cy + 14, 2, sel);
        return;
    }
    wxCloud(G, cx, cy, muted);
}

// ── shared alarm center (banner + repeating ring) ────────────────────────────
static void fireAlarm(NotifLevel lvl) {
    // force=true ignores a per-level mute; allowCovert=true rings under the cover.
    NotificationManager::getInstance().notify(lvl, true, true);
}

void Ui::raiseAlarm(NotifLevel lvl, const char* text) {
    _ringActive = true; _ringLevel = lvl; _ringCount = 0; _ringNextMs = millis();
    strncpy(_bannerText, text, sizeof(_bannerText) - 1);
    _bannerText[sizeof(_bannerText) - 1] = 0;
    _bannerMs = millis();
}
void Ui::tickAlarm() {
    if (!_ringActive) return;
    if ((int32_t)(millis() - _ringNextMs) < 0) return;
    if (_ringCount >= RING_MAX) { _ringActive = false; return; }
    fireAlarm(_ringLevel);
    _ringCount++;
    _ringNextMs = millis() + RING_GAP;
}
void Ui::dismissAlarm() { _ringActive = false; _bannerMs = 0; }
bool Ui::alarmShowing() const {
    return _ringActive || (_bannerMs && millis() - _bannerMs < BANNER_MS);
}
void Ui::drawBanner() {
    if (!alarmShowing()) return;
    auto* G = cover::G;
    int h = 26;
    G->fillSmoothRoundRect(10, UI_SB_H + 3, SCREEN_WIDTH - 20, h, 8, badge);
    G->setFont(&cover::fTitle);
    G->setTextColor(white);
    G->setTextDatum(textdatum_t::middle_center);
    G->drawString(_bannerText, SCREEN_WIDTH / 2, UI_SB_H + 3 + h / 2);
    G->setTextDatum(textdatum_t::top_left);
}
