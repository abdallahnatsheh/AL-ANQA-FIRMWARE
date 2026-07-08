// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// CalendarApp — month grid with today highlighted (ClockManager time). See home_app.h.

#ifndef APP_CALENDAR_H
#define APP_CALENDAR_H

#include "home_app.h"
#include "home_widgets.h"

class CalendarApp : public HomeApp {
public:
    explicit CalendarApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Calendar"; }
    void onEnter() override { _monthOffset = 0; }
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;
    Nav  onTrackball(TrackballEvent) override;
private:
    int _monthOffset = 0;
};

#endif // APP_CALENDAR_H
