// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_calendar.h"
#include "clock_manager.h"
#include <time.h>

void CalendarApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Calendar", _focus == 0);
    int cy0 = UI_CONTENT_Y + 6;
    if (!ClockManager::instance().isValid()) {
        G->setFont(_ui.fBody()); G->setTextColor(_ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString("Set date & time", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
        G->setTextDatum(textdatum_t::top_left);
        return;
    }
    time_t now = time(nullptr); struct tm lt = *localtime(&now);
    int m = lt.tm_mon + _monthOffset, y = lt.tm_year + 1900;
    while (m < 0)  { m += 12; y--; }
    while (m > 11) { m -= 12; y++; }
    static const char* mon[] = { "January","February","March","April","May","June",
                                 "July","August","September","October","November","December" };
    char hdr[24]; snprintf(hdr, sizeof(hdr), "%s %d", mon[m], y);
    G->setFont(_ui.fTitle()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_center);
    G->drawString(hdr, SCREEN_WIDTH / 2, cy0 + 8);
    // Prev / next month controls with focus rings
    if (_focus == 1) _ui.focusRing(8, cy0 - 2, 28, 20, 6);
    if (_focus == 2) _ui.focusRing(SCREEN_WIDTH - 36, cy0 - 2, 28, 20, 6);
    uint16_t prevC = (_focus == 1) ? _ui.sel : _ui.muted;
    uint16_t nextC = (_focus == 2) ? _ui.sel : _ui.muted;
    G->fillTriangle(16, cy0 + 8, 24, cy0 + 3, 24, cy0 + 13, prevC);
    G->fillTriangle(SCREEN_WIDTH - 16, cy0 + 8, SCREEN_WIDTH - 24, cy0 + 3, SCREEN_WIDTH - 24, cy0 + 13, nextC);
    G->setTextDatum(textdatum_t::top_left);
    static const char* wd[] = { "S","M","T","W","T","F","S" };
    int cellW = SCREEN_WIDTH / 7, wy = cy0 + 24;
    G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
    G->setTextDatum(textdatum_t::middle_center);
    for (int i = 0; i < 7; i++) G->drawString(wd[i], i * cellW + cellW / 2, wy + 6);
    static const int dim[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    int ndays = dim[m];
    if (m == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ndays = 29;
    struct tm f = {}; f.tm_year = y - 1900; f.tm_mon = m; f.tm_mday = 1; f.tm_hour = 12;
    mktime(&f);
    int firstWd = f.tm_wday, gridY = wy + 20, rowH = UI_CAL_ROW_H;
    bool thisMonth = (_monthOffset == 0);
    for (int d = 1; d <= ndays; d++) {
        int idx = firstWd + d - 1, r = idx / 7, c = idx % 7;
        int px = c * cellW + cellW / 2, py = gridY + r * rowH + rowH / 2;
        bool today = thisMonth && d == lt.tm_mday;
        if (today) { G->fillSmoothCircle(px, py, 11, _ui.sel); G->setTextColor(_ui.white); }
        else       { G->setTextColor(_ui.ink); }
        char db[4]; snprintf(db, sizeof(db), "%d", d);
        G->setFont(_ui.fBody()); G->drawString(db, px, py);
    }
    G->setTextDatum(textdatum_t::top_left);
}
Nav CalendarApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    if (te.y >= UI_CONTENT_Y && te.y < UI_CONTENT_Y + 24) {
        if (te.x < 60)                { _monthOffset--; _focus = 1; return Nav::Stay; }
        if (te.x > SCREEN_WIDTH - 60) { _monthOffset++; _focus = 2; return Nav::Stay; }
    }
    return Nav::Stay;
}
Nav CalendarApp::onTrackball(TrackballEvent tb) {
    if (tb == TBALL_UP)   { _focus = (_focus + 2) % 3; return Nav::Stay; }
    if (tb == TBALL_DOWN) { _focus = (_focus + 1) % 3; return Nav::Stay; }
    if (tb == TBALL_LEFT)  { _monthOffset--; _focus = 1; return Nav::Stay; }
    if (tb == TBALL_RIGHT) { _monthOffset++; _focus = 2; return Nav::Stay; }
    if (tb == TBALL_CLICK) {
        if (_focus == 0) return Nav::Back;
        if (_focus == 1) { _monthOffset--; return Nav::Stay; }
        if (_focus == 2) { _monthOffset++; return Nav::Stay; }
    }
    return Nav::Stay;
}
Nav CalendarApp::onKey(char k) {
    if (k == 'q' || k == 'Q') return Nav::Back;
    if (k == ',' || k == '<') { _monthOffset--; return Nav::Stay; }
    if (k == '.' || k == '>') { _monthOffset++; return Nav::Stay; }
    return Nav::Stay;
}
