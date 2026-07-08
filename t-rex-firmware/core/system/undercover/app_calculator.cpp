// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_calculator.h"
#include <math.h>
#include <string.h>

static const char* kCalcKeys[5][4] = {
    { "C",   "<", "%", "/" },
    { "7",   "8", "9", "x" },
    { "4",   "5", "6", "-" },
    { "1",   "2", "3", "+" },
    { "+/-", "0", ".", "=" },
};
static bool calcIsOp(const char* k) {
    return !strcmp(k, "/") || !strcmp(k, "x") || !strcmp(k, "-") || !strcmp(k, "+") || !strcmp(k, "=");
}
static String calcFmt(double v) {
    if (isnan(v) || isinf(v)) return String("Error");
    char b[24];
    if (v == (double)(long long)v && fabs(v) < 1e15) snprintf(b, sizeof(b), "%lld", (long long)v);
    else                                             snprintf(b, sizeof(b), "%.6g", v);
    return String(b);
}
static double calcApply(double a, char op, double b) {
    switch (op) { case '+': return a + b; case '-': return a - b;
                  case 'x': return a * b; case '/': return b != 0 ? a / b : NAN; }
    return b;
}

void CalculatorApp::onEnter() { _cur = "0"; _acc = 0; _op = 0; _fresh = true; }

void CalculatorApp::input(const char* key) {
    char k = key[0];
    if (key[1] == 0 && k >= '0' && k <= '9') {
        if (_cur == "Error") { _cur = "0"; _fresh = true; }
        if (_fresh || _cur == "0") { _cur = String(k); _fresh = false; }
        else if (_cur.length() < 12) _cur += k;
    } else if (!strcmp(key, ".")) {
        if (_fresh || _cur == "Error") { _cur = "0."; _fresh = false; }
        else if (_cur.indexOf('.') < 0) _cur += ".";
    } else if (!strcmp(key, "C")) { _cur = "0"; _acc = 0; _op = 0; _fresh = true; }
    else if (!strcmp(key, "<")) {
        if (!_fresh && _cur.length() > 1 && _cur != "Error") _cur.remove(_cur.length() - 1);
        else { _cur = "0"; _fresh = true; }
    } else if (!strcmp(key, "+/-")) {
        if (_cur != "0" && _cur != "Error") { if (_cur.startsWith("-")) _cur.remove(0, 1); else _cur = "-" + _cur; }
    } else if (!strcmp(key, "%")) { _cur = calcFmt(_cur.toDouble() / 100.0); _fresh = true; }
    else if (calcIsOp(key) && strcmp(key, "=")) {
        double cv = _cur.toDouble();
        if (_op && !_fresh) { _acc = calcApply(_acc, _op, cv); _cur = calcFmt(_acc); }
        else                  _acc = cv;
        _op = key[0]; _fresh = true;
    } else if (!strcmp(key, "=")) {
        if (_op) { double cv = _cur.toDouble(); _acc = calcApply(_acc, _op, cv); _cur = calcFmt(_acc); _op = 0; _fresh = true; }
    }
}

void CalculatorApp::draw() {
    auto* G = _ui.g();
    _ui.appBar("Calculator");
    int dy = UI_CONTENT_Y;
    G->fillRect(0, dy, SCREEN_WIDTH, 46, _ui.bg);
    G->setFont(_ui.fBig()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_right);
    G->drawString(_cur.c_str(), SCREEN_WIDTH - 14, dy + 26);
    G->setTextDatum(textdatum_t::top_left);
    int gy = dy + 48, gx = 6, gw = SCREEN_WIDTH - 12, gh = SCREEN_HEIGHT - gy - 6, cw = gw / 4, chh = gh / 5;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            int x = gx + c * cw, y = gy + r * chh;
            const char* lbl = kCalcKeys[r][c]; bool op = calcIsOp(lbl);
            G->fillSmoothRoundRect(x + 2, y + 2, cw - 4, chh - 4, 8, op ? _ui.col(0x00ACC1) : _ui.bar);
            G->setFont(_ui.fTitle()); G->setTextColor(op ? _ui.bg : _ui.ink);
            G->setTextDatum(textdatum_t::middle_center);
            G->drawString(lbl, x + cw / 2, y + chh / 2);
        }
    }
    G->setTextDatum(textdatum_t::top_left);
}

Nav CalculatorApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
    int dy = UI_CONTENT_Y, gy = dy + 48, gx = 6, gw = SCREEN_WIDTH - 12, gh = SCREEN_HEIGHT - gy - 6, cw = gw / 4, chh = gh / 5;
    if (te.y < gy) return Nav::Stay;
    int c = (te.x - gx) / cw, r = (te.y - gy) / chh;
    if (c >= 0 && c <= 3 && r >= 0 && r <= 4) input(kCalcKeys[r][c]);
    return Nav::Stay;
}

Nav CalculatorApp::onKey(char k) {
    char kb[2] = { 0, 0 };
    if      (k >= '0' && k <= '9') { kb[0] = k; input(kb); }
    else if (k == '.')             input(".");
    else if (k == '+')             input("+");
    else if (k == '-')             input("-");
    else if (k == '*' || k == 'x' || k == 'X') input("x");
    else if (k == '/')             input("/");
    else if (k == '=' || k == '\r' || k == '\n') input("=");
    else if (k == '\x08' || k == '\x7F')         input("<");
    else if (k == '%')             input("%");
    else if (k == 'c' || k == 'C') input("C");
    else if (k == 'q' || k == 'Q') return Nav::Back;
    return Nav::Stay;
}
