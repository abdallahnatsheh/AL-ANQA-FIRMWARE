// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_weather.h"
#include "weather_manager.h"
#include <WiFi.h>

void WeatherApp::refresh() {
    WeatherManager& wx = WeatherManager::instance();
    if (!(wx.configured() && WiFi.status() == WL_CONNECTED)) return;
    _loading = true; draw(); _ui.present();
    wx.forceFetch();
    _loading = false;
}
void WeatherApp::onEnter() {
    _loading = false; _focus = 1;
    if (WeatherManager::instance().stale()) refresh();
}
void WeatherApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Weather", _focus == 0);
    WeatherManager& wx = WeatherManager::instance();
    int cy0 = UI_CONTENT_Y;
    const int iconY = cy0 + (SCREEN_HEIGHT < 230 ? 28 : 40);
    const int tempY = cy0 + (SCREEN_HEIGHT < 230 ? 72 : 92);
    const int condY = cy0 + (SCREEN_HEIGHT < 230 ? 98 : 124);
    if (wx.hasReading()) {
        _ui.weatherIcon(SCREEN_WIDTH / 2, iconY, wx.code());
        char nb[8]; snprintf(nb, sizeof(nb), "%d", wx.temp());
        G->setFont(_ui.fBig()); int nw = G->textWidth(nb);
        int startX = SCREEN_WIDTH / 2 - (nw + 16) / 2;
        G->setTextColor(_ui.ink); G->setTextDatum(textdatum_t::middle_left);
        G->drawString(nb, startX, tempY);
        G->drawCircle(startX + nw + 5, tempY - 10, 3, _ui.ink);
        char ub[2] = { (char)(wx.imperial() ? 'F' : 'C'), 0 };
        G->setFont(_ui.fTitle()); G->drawString(ub, startX + nw + 11, tempY);
        G->setTextColor(_ui.muted); G->setTextDatum(textdatum_t::middle_center);
        G->drawString(wx.condition(), SCREEN_WIDTH / 2, condY);
    } else if (!_loading) {
        G->setFont(_ui.fBody()); G->setTextColor(_ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString("No weather data", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);
        G->setFont(_ui.fMeta());
        const char* hint = WiFi.status() != WL_CONNECTED ? "Connect WiFi in Settings"
#if BOARD_HAS_TOUCH
                          : "Tap to fetch";
#else
                          : "Click to fetch";
#endif
        G->drawString(hint, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 12);
    }
    G->setTextDatum(textdatum_t::middle_center);
    // Refresh control — always focusable
    int bx = SCREEN_WIDTH / 2 - 70, by = SCREEN_HEIGHT - 34, bw = 140, bh = 24;
    if (_focus == 1 && !_loading) _ui.focusRing(bx, by, bw, bh, 11);
    if (_loading) {
        G->fillSmoothRoundRect(bx, by, bw, bh, 11, _ui.bar);
        G->fillArc(bx + 22, by + bh / 2, 6, 4, 0, 270, _ui.col(0x1DA1F2));
        G->setFont(_ui.fMeta()); G->setTextColor(_ui.ink);
        G->drawString("Updating...", bx + bw / 2 + 8, by + bh / 2);
    } else {
        G->fillSmoothRoundRect(bx, by, bw, bh, 11, _focus == 1 ? _ui.sel : _ui.bar);
        G->setFont(_ui.fMeta()); G->setTextColor(_focus == 1 ? _ui.white : _ui.ink);
        G->drawString(wx.hasReading() ? "Refresh" : "Fetch", bx + bw / 2, by + bh / 2);
    }
    G->setTextDatum(textdatum_t::top_left);
}
Nav WeatherApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    refresh();
    return Nav::Stay;
}
Nav WeatherApp::onTrackball(TrackballEvent tb) {
    if (tb == TBALL_UP || tb == TBALL_DOWN || tb == TBALL_LEFT || tb == TBALL_RIGHT) {
        _focus ^= 1;   // toggle back <-> Refresh
        return Nav::Stay;
    }
    if (tb == TBALL_CLICK) {
        if (_focus == 0) return Nav::Back;
        refresh();
    }
    return Nav::Stay;
}
Nav WeatherApp::onKey(char k) {
    if (k == 'q' || k == 'Q') return Nav::Back;
    if (k == 'r' || k == 'R' || k == ' ') { refresh(); return Nav::Stay; }
    return Nav::Stay;
}
