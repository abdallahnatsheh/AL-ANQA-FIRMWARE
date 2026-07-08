// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_clock.h"
#include <Arduino.h>

uint32_t ClockApp::swElapsedMs() const { return _swAccum + (_swRun ? millis() - _swStart : 0); }
void ClockApp::reset() { _swRun = false; _swAccum = 0; _tmRun = false; _tmDone = false; }

void ClockApp::btnA() {
    if (_mode == STOPWATCH) {
        if (_swRun) { _swAccum = swElapsedMs(); _swRun = false; }
        else        { _swStart = millis(); _swRun = true; }
    } else {
        if (_tmRun) { _tmRun = false; _tmSet = _tmRemain; }          // pause → resume from remaining
        else {
            _tmDone = false;
            if (_tmSet <= 0) _tmSet = 60;
            _tmConfig = _tmSet;
            _tmEnd    = millis() + (uint32_t)_tmSet * 1000;
            _tmRemain = _tmSet; _tmRun = true;
        }
    }
}
void ClockApp::btnB() {
    if (_mode == STOPWATCH) { _swRun = false; _swAccum = 0; _swStart = 0; }
    else { _tmRun = false; _tmDone = false; _tmSet = _tmConfig; _tmRemain = _tmSet; }
}

void ClockApp::tick() {
    if (_tmRun && !_tmDone) {
        if ((int32_t)(millis() - _tmEnd) >= 0) {
            _tmRun = false; _tmDone = true; _tmRemain = 0;
            _ui.raiseAlarm(NOTIF_SUCCESS, "Timer done");
        } else {
            _tmRemain = (int)((_tmEnd - millis() + 999) / 1000);
            if (_tmRemain < 0) _tmRemain = 0;
        }
    }
}

void ClockApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Clock");
    int tw = (SCREEN_WIDTH - 28) / 2, ty = UI_CONTENT_Y + 6, th = 22;
    const char* tabs[2] = { "Stopwatch", "Timer" };
    for (int i = 0; i < 2; i++) {
        int x = 14 + i * tw; bool sel = (i == (int)_mode);
        G->fillSmoothRoundRect(x, ty, tw - 4, th, 8, sel ? _ui.col(0x1DA1F2) : _ui.bar);
        G->setFont(_ui.fMeta()); G->setTextColor(sel ? _ui.bg : _ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(tabs[i], x + (tw - 4) / 2, ty + th / 2);
    }
    G->setTextDatum(textdatum_t::top_left);
    int midY = UI_CONTENT_Y + 78;
    if (_mode == STOPWATCH) {
        uint32_t ms = swElapsedMs(); int t = ms / 100;
        char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d.%d", (t / 600) % 100, (t / 10) % 60, t % 10);
        G->setFont(_ui.fBig()); G->setTextColor(_ui.ink);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(buf, SCREEN_WIDTH / 2, midY);
        G->setTextDatum(textdatum_t::top_left);
        _ui.twoButtons(_swRun ? "Stop" : "Start", "Reset", _swRun ? _ui.col(0xEA4335) : _ui.col(0x34A853));
    } else {
        int secs = _tmDone ? 0 : (_tmRun ? _tmRemain : _tmSet);
        char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
        G->setFont(_ui.fBig()); G->setTextColor(_tmDone ? _ui.col(0xEA4335) : _ui.ink);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(buf, SCREEN_WIDTH / 2, midY);
        G->setTextDatum(textdatum_t::top_left);
        if (_tmDone) {
            G->setFont(_ui.fTitle()); G->setTextColor(_ui.col(0xEA4335));
            G->setTextDatum(textdatum_t::middle_center);
            G->drawString("Time's up", SCREEN_WIDTH / 2, midY + 26);
            G->setTextDatum(textdatum_t::top_left);
        } else if (!_tmRun) {
            int ay = UI_CONTENT_Y + 102;
            const int bx[4] = { 40, 96, SCREEN_WIDTH - 96, SCREEN_WIDTH - 40 };
            const char* bl[4] = { "-", "+", "-", "+" };
            for (int i = 0; i < 4; i++) {
                G->fillSmoothCircle(bx[i], ay, 13, _ui.bar);
                G->setFont(_ui.fTitle()); G->setTextColor(_ui.ink);
                G->setTextDatum(textdatum_t::middle_center);
                G->drawString(bl[i], bx[i], ay - 1);
            }
            G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
            G->drawString("min", (bx[0] + bx[1]) / 2, ay + 22);
            G->drawString("sec", (bx[2] + bx[3]) / 2, ay + 22);
            G->setTextDatum(textdatum_t::top_left);
        }
        _ui.twoButtons(_tmRun ? "Pause" : "Start", "Reset", _tmRun ? _ui.col(0xFB8C00) : _ui.col(0x34A853));
    }
}

Nav ClockApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    int tw = (SCREEN_WIDTH - 28) / 2, ty = UI_CONTENT_Y + 6, th = 22;
    if (te.y >= ty && te.y <= ty + th) {
        if (te.x >= 14 && te.x < 14 + tw)          { _mode = STOPWATCH; return Nav::Stay; }
        if (te.x >= 14 + tw && te.x < 14 + 2 * tw) { _mode = TIMER;     return Nav::Stay; }
    }
    if (_mode == TIMER && !_tmRun && !_tmDone) {
        int ay = UI_CONTENT_Y + 102;
        const int bx[4] = { 40, 96, SCREEN_WIDTH - 96, SCREEN_WIDTH - 40 };
        for (int i = 0; i < 4; i++) {
            int dx = te.x - bx[i], dy = te.y - ay;
            if (dx * dx + dy * dy <= 16 * 16) {
                if (i == 0) _tmSet -= 60; if (i == 1) _tmSet += 60;
                if (i == 2) _tmSet -= 5;  if (i == 3) _tmSet += 5;
                if (_tmSet < 0) _tmSet = 0; if (_tmSet > 5999) _tmSet = 5999;
                return Nav::Stay;
            }
        }
    }
    if (Ui::hitBtnA(te.x, te.y)) { btnA(); return Nav::Stay; }
    if (Ui::hitBtnB(te.x, te.y)) { btnB(); return Nav::Stay; }
    return Nav::Stay;
}

Nav ClockApp::onTrackball(TrackballEvent tb) {
    if (tb == TBALL_LEFT)       { _mode = STOPWATCH; }
    else if (tb == TBALL_RIGHT) { _mode = TIMER; }
    else if (tb == TBALL_CLICK) { btnA(); }
    else if (_mode == TIMER && !_tmRun && !_tmDone) {
        if (tb == TBALL_UP)   { _tmSet += 60; if (_tmSet > 5999) _tmSet = 5999; }
        if (tb == TBALL_DOWN) { _tmSet -= 60; if (_tmSet < 0)    _tmSet = 0; }
    }
    return Nav::Stay;
}

Nav ClockApp::onKey(char k) {
    if (k == ' ')             { btnA(); return Nav::Stay; }
    if (k == 'r' || k == 'R') { btnB(); return Nav::Stay; }
    if (k == 'q' || k == 'Q') return Nav::Back;
    return Nav::Stay;
}
