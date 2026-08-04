// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// ClockApp — stopwatch + countdown timer (timer fires a chime via the Ui alarm
// center). See home_app.h.

#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include "home_app.h"
#include "home_widgets.h"

class ClockApp : public HomeApp {
public:
    explicit ClockApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Clock"; }
    void onEnter() override {}                  // keep running state across opens
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;
    Nav  onTrackball(TrackballEvent) override;
    Nav  onKey(char) override;
    void tick() override;                       // timer countdown + expiry alarm (any screen)
    bool animating() const override { return _swRun || (_tmRun && !_tmDone); }
    void reset();                               // clear transient state at cover entry
private:
    enum Mode { STOPWATCH, TIMER };
    void btnA();                                // start/stop
    void btnB();                                // reset
    uint32_t swElapsedMs() const;
    Mode     _mode    = STOPWATCH;
    bool     _swRun   = false;
    uint32_t _swStart = 0, _swAccum = 0;
    int      _tmSet   = 300, _tmConfig = 300, _tmRemain = 300;
    bool     _tmRun   = false, _tmDone = false;
    uint32_t _tmEnd   = 0;
};

#endif // APP_CLOCK_H
