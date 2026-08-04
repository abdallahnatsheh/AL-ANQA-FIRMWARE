// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// CalculatorApp — a simple four-function calculator. See home_app.h.

#ifndef APP_CALCULATOR_H
#define APP_CALCULATOR_H

#include "home_app.h"
#include "home_widgets.h"
#include <Arduino.h>

class CalculatorApp : public HomeApp {
public:
    explicit CalculatorApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Calculator"; }
    void onEnter() override;
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;
    Nav  onKey(char) override;
private:
    void input(const char* key);
    String _cur   = "0";
    double _acc   = 0;
    char   _op    = 0;
    bool   _fresh = true;
};

#endif // APP_CALCULATOR_H
