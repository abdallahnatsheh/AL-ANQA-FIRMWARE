// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_reminders.h"
#include "clock_manager.h"
#include "sdcard_manager.h"
#include <SD.h>
#include <time.h>
#include <string.h>

extern SDCardManager sdCardManager;

void RemindersApp::load() {
    _rem.clear();
    _lastMin = -1;
    if (!sdCardManager.canAccessSD()) return;
    File f = SD.open("/apps/home/reminders.csv", FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (!line.length()) continue;
        int c1 = line.indexOf(','); if (c1 < 0) continue;
        int c2 = line.indexOf(',', c1 + 1); if (c2 < 0) continue;
        String t = line.substring(0, c1);
        int cc = t.indexOf(':'); if (cc < 0) continue;
        Rem r;
        r.hour = t.substring(0, cc).toInt();
        r.minute = t.substring(cc + 1).toInt();
        r.on = line.substring(c1 + 1, c2).toInt() != 0;
        String lb = line.substring(c2 + 1); lb.trim();
        strncpy(r.label, lb.c_str(), sizeof(r.label) - 1); r.label[sizeof(r.label) - 1] = 0;
        if (r.hour >= 0 && r.hour < 24 && r.minute >= 0 && r.minute < 60) _rem.push_back(r);
    }
    f.close();
}
void RemindersApp::save() {
    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir("/apps/home");
    File f = SD.open("/apps/home/reminders.csv", FILE_WRITE);
    if (!f) return;
    for (auto& r : _rem) f.printf("%02d:%02d,%d,%s\n", r.hour, r.minute, r.on ? 1 : 0, r.label);
    f.close();
}
void RemindersApp::openEdit(int idx) {
    _editIdx = idx;
    if (idx < 0) { _eh = 8; _em = 0; _elabel[0] = 0; }
    else { _eh = _rem[idx].hour; _em = _rem[idx].minute;
           strncpy(_elabel, _rem[idx].label, sizeof(_elabel)); _elabel[sizeof(_elabel) - 1] = 0; }
    _ef = 0; _edit = true; _focus = 1;   // hour field
}
void RemindersApp::saveEdit() {
    if (_editIdx < 0) {
        Rem r; r.hour = _eh; r.minute = _em; r.on = true;
        strncpy(r.label, _elabel, sizeof(r.label)); r.label[sizeof(r.label) - 1] = 0;
        _rem.push_back(r);
    } else {
        _rem[_editIdx].hour = _eh; _rem[_editIdx].minute = _em;
        strncpy(_rem[_editIdx].label, _elabel, sizeof(_rem[_editIdx].label));
        _rem[_editIdx].label[sizeof(_rem[_editIdx].label) - 1] = 0;
    }
    save(); _edit = false; _focusFab = false;
    if (_sel >= (int)_rem.size()) _sel = (int)_rem.size() - 1;
    if (_sel < 0) _sel = 0;
}
void RemindersApp::del() {
    if (_editIdx >= 0 && _editIdx < (int)_rem.size()) _rem.erase(_rem.begin() + _editIdx);
    save(); _edit = false; _focusFab = false;
    if (_sel >= (int)_rem.size()) _sel = (int)_rem.size() - 1;
    if (_sel < 0) _sel = 0;
}
void RemindersApp::tick() {
    if (!ClockManager::instance().isValid()) return;
    time_t now = time(nullptr); struct tm lt = *localtime(&now);
    int cur = lt.tm_hour * 60 + lt.tm_min;
    if (_lastMin < 0) { _lastMin = cur; return; }
    if (cur == _lastMin) return;
    _lastMin = cur;
    for (auto& r : _rem) {
        if (r.on && r.hour * 60 + r.minute == cur) {
            char b[40]; snprintf(b, sizeof(b), "Reminder: %s", r.label[0] ? r.label : "(no title)");
            _ui.raiseAlarm(NOTIF_INFO, b);
        }
    }
}
void RemindersApp::drawEdit() {
    auto* G = _ui.g();
    _ui.appBar(_editIdx < 0 ? "New reminder" : "Edit reminder", _focus == 0);
    int cy0 = UI_CONTENT_Y + 16, hx = SCREEN_WIDTH / 2 - 42, mx = SCREEN_WIDTH / 2 + 42;
    for (int f = 0; f < 2; f++) {
        int px = (f == 0) ? hx : mx;
        bool focused = (_focus == 1 + f);
        uint16_t cc = focused ? _ui.sel : ((_ef == f) ? _ui.col(0xEA4335) : _ui.muted);
        if (focused) _ui.focusRing(px - 28, cy0 - 8, 56, 64, 10);
        G->fillTriangle(px - 8, cy0 + 4,  px + 8, cy0 + 4,  px, cy0 - 4,  cc);
        G->fillTriangle(px - 8, cy0 + 44, px + 8, cy0 + 44, px, cy0 + 52, cc);
    }
    char hb[4], mb[4]; snprintf(hb, 4, "%02d", _eh); snprintf(mb, 4, "%02d", _em);
    G->setFont(_ui.fBig()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_center);
    G->drawString(hb, hx, cy0 + 24); G->drawString(":", SCREEN_WIDTH / 2, cy0 + 24); G->drawString(mb, mx, cy0 + 24);
    int ly = cy0 + 70;
    if (_focus == 3) _ui.focusRing(20, ly, SCREEN_WIDTH - 40, 26, 8);
    G->fillSmoothRoundRect(20, ly, SCREEN_WIDTH - 40, 26, 8, _ui.bar);
    G->setFont(_ui.fBody()); G->setTextColor(_elabel[0] ? _ui.ink : _ui.muted);
    G->setTextDatum(textdatum_t::middle_left);
    G->drawString(_elabel[0] ? _elabel : "Type a label", 30, ly + 13);
    G->setTextDatum(textdatum_t::top_left);
    int bf = (_focus == 4) ? 0 : (_focus == 5) ? 1 : -1;
    _ui.twoButtons("Save", _editIdx < 0 ? "Cancel" : "Delete", _ui.col(0x34A853), bf);
}
void RemindersApp::draw() {
    if (_edit) { drawEdit(); return; }
    auto* G = _ui.g();
    _ui.appBar("Reminders", _focus == 0 && !_focusFab);
    int y0 = UI_CONTENT_Y + 8;
    if (_rem.empty()) {
        G->setFont(_ui.fBody()); G->setTextColor(_ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
#if BOARD_HAS_TOUCH
        G->drawString("No reminders - tap + to add", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);
#else
        G->drawString("No reminders — click + or press +", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);
#endif
        G->setTextDatum(textdatum_t::top_left);
    } else {
        int n = (int)_rem.size(); if (n > 5) n = 5;
        for (int i = 0; i < n; i++) {
            int ry = y0 + i * 34;
            bool rowFocus = !_focusFab && _focus != 0 && i == _sel;
            if (rowFocus) _ui.focusRing(10, ry, SCREEN_WIDTH - 20, 30, 8);
            G->fillSmoothRoundRect(10, ry, SCREEN_WIDTH - 20, 30, 8, _ui.bar);
            if (i == _sel && !_focusFab) G->drawRoundRect(10, ry, SCREEN_WIDTH - 20, 30, 8, _ui.sel);
            char tb[8]; snprintf(tb, sizeof(tb), "%02d:%02d", _rem[i].hour, _rem[i].minute);
            G->setFont(_ui.fTitle()); G->setTextColor(_ui.ink);
            G->setTextDatum(textdatum_t::middle_left);
            G->drawString(tb, 20, ry + 15);
            G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
            G->drawString(_rem[i].label[0] ? _rem[i].label : "(no title)", 74, ry + 15);
            G->fillSmoothCircle(SCREEN_WIDTH - 26, ry + 15, 7, _rem[i].on ? _ui.col(0x34A853) : _ui.hair);
            G->setTextDatum(textdatum_t::top_left);
        }
    }
    if (_focusFab) _ui.focusCircle(UI_FAB_CX, UI_FAB_CY, UI_FAB_R);
    G->fillSmoothCircle(UI_FAB_CX, UI_FAB_CY, UI_FAB_R, _ui.col(0xEA4335));
    G->drawWideLine(UI_FAB_CX - 8, UI_FAB_CY, UI_FAB_CX + 8, UI_FAB_CY, 3, _ui.white);
    G->drawWideLine(UI_FAB_CX, UI_FAB_CY - 8, UI_FAB_CX, UI_FAB_CY + 8, 3, _ui.white);
}
Nav RemindersApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (_edit) {
        if (Ui::hitAppBack(te.x, te.y)) { _edit = false; return Nav::Stay; }
        int cy0 = UI_CONTENT_Y + 16, hx = SCREEN_WIDTH / 2 - 42, mx = SCREEN_WIDTH / 2 + 42;
        if (te.y >= cy0 - 8 && te.y <= cy0 + 10) {
            if (abs(te.x - hx) < 16) { _eh = (_eh + 1) % 24; _ef = 0; return Nav::Stay; }
            if (abs(te.x - mx) < 16) { _em = (_em + 1) % 60; _ef = 1; return Nav::Stay; }
        }
        if (te.y >= cy0 + 38 && te.y <= cy0 + 56) {
            if (abs(te.x - hx) < 16) { _eh = (_eh + 23) % 24; _ef = 0; return Nav::Stay; }
            if (abs(te.x - mx) < 16) { _em = (_em + 59) % 60; _ef = 1; return Nav::Stay; }
        }
        if (Ui::hitBtnA(te.x, te.y)) { saveEdit(); return Nav::Stay; }
        if (Ui::hitBtnB(te.x, te.y)) { if (_editIdx < 0) _edit = false; else del(); return Nav::Stay; }
        return Nav::Stay;
    }
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    int dx = te.x - UI_FAB_CX, dy = te.y - UI_FAB_CY;
    if (dx * dx + dy * dy <= (UI_FAB_R + 4) * (UI_FAB_R + 4)) { openEdit(-1); return Nav::Stay; }
    int y0 = UI_CONTENT_Y + 8, n = (int)_rem.size(); if (n > 5) n = 5;
    for (int i = 0; i < n; i++) {
        int ry = y0 + i * 34;
        if (te.y >= ry && te.y <= ry + 30) {
            if (te.x >= SCREEN_WIDTH - 40) { _rem[i].on = !_rem[i].on; save(); }
            else { _sel = i; openEdit(i); }
            return Nav::Stay;
        }
    }
    return Nav::Stay;
}
Nav RemindersApp::onTrackball(TrackballEvent tb) {
    if (_edit) {
        // 0=back 1=hour 2=min 3=label 4=Save 5=Cancel/Delete
        if (tb == TBALL_UP)   { _focus = (_focus + 5) % 6; if (_focus == 1 || _focus == 2) _ef = _focus - 1; return Nav::Stay; }
        if (tb == TBALL_DOWN) { _focus = (_focus + 1) % 6; if (_focus == 1 || _focus == 2) _ef = _focus - 1; return Nav::Stay; }
        if (tb == TBALL_LEFT || tb == TBALL_RIGHT) {
            if (_focus == 1 || _focus == 2) { _ef ^= 1; _focus = 1 + _ef; }
            return Nav::Stay;
        }
        if (tb == TBALL_CLICK) {
            if (_focus == 0) { _edit = false; return Nav::Stay; }
            if (_focus == 1) { _eh = (_eh + 1) % 24; return Nav::Stay; }
            if (_focus == 2) { _em = (_em + 1) % 60; return Nav::Stay; }
            if (_focus == 3) return Nav::Stay;   // label = keyboard
            if (_focus == 4) { saveEdit(); return Nav::Stay; }
            if (_focus == 5) { if (_editIdx < 0) _edit = false; else del(); return Nav::Stay; }
        }
        return Nav::Stay;
    }
    int n = (int)_rem.size();
    if (tb == TBALL_UP) {
        if (_focusFab) { _focusFab = false; if (n > 0) _sel = n - 1; }
        else if (_focus == 0) { /* stay */ }
        else if (_sel > 0) _sel--;
        else { _focus = 0; }
        return Nav::Stay;
    }
    if (tb == TBALL_DOWN) {
        if (_focus == 0) { _focus = 1; if (n == 0) _focusFab = true; else _sel = 0; }
        else if (_focusFab) { /* stay */ }
        else if (_sel < n - 1) _sel++;
        else { _focusFab = true; }
        return Nav::Stay;
    }
    if (tb == TBALL_LEFT && n > 0 && !_focusFab && _focus != 0) {
        _rem[_sel].on = !_rem[_sel].on; save();
        return Nav::Stay;
    }
    if (tb == TBALL_RIGHT) { _focusFab = true; _focus = 1; return Nav::Stay; }
    if (tb == TBALL_CLICK) {
        if (_focus == 0) return Nav::Back;
        if (_focusFab || n == 0) { openEdit(-1); return Nav::Stay; }
        openEdit(_sel);
    }
    return Nav::Stay;
}
Nav RemindersApp::onKey(char k) {
    if (!_edit) {
        if (k == 'q' || k == 'Q') return Nav::Back;
        if (k == '+' || k == '=') { openEdit(-1); return Nav::Stay; }
        return Nav::Stay;
    }
    if (k == '\r' || k == '\n') { saveEdit(); return Nav::Stay; }
    if (k == '\x08' || k == '\x7F') { int l = strlen(_elabel); if (l > 0) _elabel[l - 1] = 0; return Nav::Stay; }
    if (k >= 0x20 && k < 0x7F) {
        int l = strlen(_elabel);
        if (l < (int)sizeof(_elabel) - 1) { _elabel[l] = k; _elabel[l + 1] = 0; }
        _focus = 3;
    }
    return Nav::Stay;
}
