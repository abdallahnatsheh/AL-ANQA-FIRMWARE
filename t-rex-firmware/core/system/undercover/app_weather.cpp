// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_weather.h"
#include "weather_manager.h"
#include <WiFi.h>

void WeatherApp::refresh() {
    WeatherManager& wx = WeatherManager::instance();
    if (!(wx.configured() && WiFi.status() == WL_CONNECTED)) return;
    _loading = true; draw(); _ui.present();   // show the indicator before the ~1-3s blocking GET
    wx.forceFetch();
    _loading = false;
}
void WeatherApp::onEnter() {
    _loading = false;
    if (WeatherManager::instance().stale()) refresh();
}
void WeatherApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Weather");
    WeatherManager& wx = WeatherManager::instance();
    int cy0 = UI_CONTENT_Y;
    if (wx.hasReading()) {
        _ui.weatherIcon(SCREEN_WIDTH / 2, cy0 + 40, wx.code());
        char nb[8]; snprintf(nb, sizeof(nb), "%d", wx.temp());
        G->setFont(_ui.fBig()); int nw = G->textWidth(nb);
        int startX = SCREEN_WIDTH / 2 - (nw + 16) / 2;
        G->setTextColor(_ui.ink); G->setTextDatum(textdatum_t::middle_left);
        G->drawString(nb, startX, cy0 + 92);
        G->drawCircle(startX + nw + 5, cy0 + 82, 3, _ui.ink);
        char ub[2] = { (char)(wx.imperial() ? 'F' : 'C'), 0 };
        G->setFont(_ui.fTitle()); G->drawString(ub, startX + nw + 11, cy0 + 92);
        G->setTextColor(_ui.muted); G->setTextDatum(textdatum_t::middle_center);
        G->drawString(wx.condition(), SCREEN_WIDTH / 2, cy0 + 124);
    } else if (!_loading) {
        G->setFont(_ui.fBody()); G->setTextColor(_ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString("No weather data", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);
        G->setFont(_ui.fMeta());
        G->drawString(WiFi.status() == WL_CONNECTED ? "Tap to fetch" : "Connect WiFi in Settings",
                      SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 12);
    }
    G->setTextDatum(textdatum_t::middle_center);
    if (_loading) {
        G->fillSmoothRoundRect(SCREEN_WIDTH / 2 - 56, SCREEN_HEIGHT - 30, 112, 22, 11, _ui.bar);
        G->fillArc(SCREEN_WIDTH / 2 - 34, SCREEN_HEIGHT - 19, 6, 4, 0, 270, _ui.col(0x1DA1F2));
        G->setFont(_ui.fMeta()); G->setTextColor(_ui.ink);
        G->drawString("Updating...", SCREEN_WIDTH / 2 + 6, SCREEN_HEIGHT - 19);
    } else if (wx.hasReading()) {
        G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
        G->drawString("Tap to refresh", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 18);
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
    if (tb == TBALL_CLICK) return Nav::Back;
    return Nav::Stay;
}
