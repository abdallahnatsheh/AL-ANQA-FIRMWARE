// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_clock.h"
#include <Arduino.h>

uint32_t ClockApp::swElapsedMs() const { return _swAccum + (_swRun ? millis() - _swStart : 0); }
void ClockApp::reset() { _swRun = false; _swAccum = 0; _tmRun = false; _tmDone = false; _focus = 2; }

void ClockApp::btnA() {
    if (_mode == STOPWATCH) {
        if (_swRun) { _swAccum = swElapsedMs(); _swRun = false; }
        else        { _swStart = millis(); _swRun = true; }
    } else {
        if (_tmRun) { _tmRun = false; _tmSet = _tmRemain; }
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

// Focus slots: 0=back, 1=tab SW, 2=tab Timer, 3=Start/Pause, 4=Reset,
//              5..8 = timer ±min/±sec (only when timer idle).
int ClockApp::focusCount() const {
    if (_mode == TIMER && !_tmRun && !_tmDone) return 9;
    return 5;
}

void ClockApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Clock", _focus == 0);
    int tw = (SCREEN_WIDTH - 28) / 2, ty = UI_CONTENT_Y + 6, th = 22;
    const char* tabs[2] = { "Stopwatch", "Timer" };
    for (int i = 0; i < 2; i++) {
        int x = 14 + i * tw; bool sel = (i == (int)_mode);
        if (_focus == 1 + i) _ui.focusRing(x, ty, tw - 4, th, 8);
        G->fillSmoothRoundRect(x, ty, tw - 4, th, 8, sel ? _ui.col(0x1DA1F2) : _ui.bar);
        G->setFont(_ui.fMeta()); G->setTextColor(sel ? _ui.bg : _ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(tabs[i], x + (tw - 4) / 2, ty + th / 2);
    }
    G->setTextDatum(textdatum_t::top_left);
    int midY = UI_CLOCK_MID_Y;
    if (_mode == STOPWATCH) {
        uint32_t ms = swElapsedMs(); int t = ms / 100;
        char buf[16]; snprintf(buf, sizeof(buf), "%02d:%02d.%d", (t / 600) % 100, (t / 10) % 60, t % 10);
        G->setFont(_ui.fBig()); G->setTextColor(_ui.ink);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(buf, SCREEN_WIDTH / 2, midY);
        G->setTextDatum(textdatum_t::top_left);
        int bf = (_focus == 3) ? 0 : (_focus == 4) ? 1 : -1;
        _ui.twoButtons(_swRun ? "Stop" : "Start", "Reset",
                       _swRun ? _ui.col(0xEA4335) : _ui.col(0x34A853), bf);
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
            int ay = UI_CLOCK_ADJ_Y;
            const int bx[4] = { 40, 96, SCREEN_WIDTH - 96, SCREEN_WIDTH - 40 };
            const char* bl[4] = { "-", "+", "-", "+" };
            for (int i = 0; i < 4; i++) {
                if (_focus == 5 + i) _ui.focusCircle(bx[i], ay, 13);
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
        int bf = (_focus == 3) ? 0 : (_focus == 4) ? 1 : -1;
        _ui.twoButtons(_tmRun ? "Pause" : "Start", "Reset",
                       _tmRun ? _ui.col(0xFB8C00) : _ui.col(0x34A853), bf);
    }
}

Nav ClockApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    int tw = (SCREEN_WIDTH - 28) / 2, ty = UI_CONTENT_Y + 6, th = 22;
    if (te.y >= ty && te.y <= ty + th) {
        if (te.x >= 14 && te.x < 14 + tw)          { _mode = STOPWATCH; _focus = 1; return Nav::Stay; }
        if (te.x >= 14 + tw && te.x < 14 + 2 * tw) { _mode = TIMER;     _focus = 2; return Nav::Stay; }
    }
    if (_mode == TIMER && !_tmRun && !_tmDone) {
        int ay = UI_CLOCK_ADJ_Y;
        const int bx[4] = { 40, 96, SCREEN_WIDTH - 96, SCREEN_WIDTH - 40 };
        for (int i = 0; i < 4; i++) {
            int dx = te.x - bx[i], dy = te.y - ay;
            if (dx * dx + dy * dy <= 16 * 16) {
                if (i == 0) _tmSet -= 60; if (i == 1) _tmSet += 60;
                if (i == 2) _tmSet -= 5;  if (i == 3) _tmSet += 5;
                if (_tmSet < 0) _tmSet = 0; if (_tmSet > 5999) _tmSet = 5999;
                _focus = 5 + i;
                return Nav::Stay;
            }
        }
    }
    if (Ui::hitBtnA(te.x, te.y)) { _focus = 3; btnA(); return Nav::Stay; }
    if (Ui::hitBtnB(te.x, te.y)) { _focus = 4; btnB(); return Nav::Stay; }
    return Nav::Stay;
}

Nav ClockApp::onTrackball(TrackballEvent tb) {
    int n = focusCount();
    if (tb == TBALL_UP) {
        _focus = (_focus + n - 1) % n;
        return Nav::Stay;
    }
    if (tb == TBALL_DOWN) {
        _focus = (_focus + 1) % n;
        return Nav::Stay;
    }
    if (tb == TBALL_LEFT || tb == TBALL_RIGHT) {
        // Sym+rotate jumps between the two tabs
        _mode = (tb == TBALL_LEFT) ? STOPWATCH : TIMER;
        _focus = 1 + (int)_mode;
        if (_focus >= focusCount()) _focus = 2;
        return Nav::Stay;
    }
    if (tb == TBALL_CLICK) {
        if (_focus == 0) return Nav::Back;
        if (_focus == 1) { _mode = STOPWATCH; return Nav::Stay; }
        if (_focus == 2) { _mode = TIMER; return Nav::Stay; }
        if (_focus == 3) { btnA(); return Nav::Stay; }
        if (_focus == 4) { btnB(); return Nav::Stay; }
        if (_mode == TIMER && !_tmRun && !_tmDone && _focus >= 5 && _focus <= 8) {
            int i = _focus - 5;
            if (i == 0) _tmSet -= 60; if (i == 1) _tmSet += 60;
            if (i == 2) _tmSet -= 5;  if (i == 3) _tmSet += 5;
            if (_tmSet < 0) _tmSet = 0; if (_tmSet > 5999) _tmSet = 5999;
        }
    }
    return Nav::Stay;
}

Nav ClockApp::onKey(char k) {
    if (k == ' ')             { btnA(); return Nav::Stay; }
    if (k == 'r' || k == 'R') { btnB(); return Nav::Stay; }
    if (k == 'q' || k == 'Q') return Nav::Back;
    return Nav::Stay;
}
