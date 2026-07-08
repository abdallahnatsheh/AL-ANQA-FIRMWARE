// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// RemindersApp — SD-persisted time reminders (/apps/home/reminders.csv). A due
// reminder fires a soft ding via the Ui alarm center from any screen. See home_app.h.

#ifndef APP_REMINDERS_H
#define APP_REMINDERS_H

#include "home_app.h"
#include "home_widgets.h"
#include <vector>

class RemindersApp : public HomeApp {
public:
    explicit RemindersApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Reminders"; }
    void onEnter() override { _edit = false; _sel = 0; }
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;
    Nav  onTrackball(TrackballEvent) override;
    Nav  onKey(char) override;
    void tick() override;                       // per-minute due check (any screen)
    void load();                                // read the CSV (call at cover entry)
private:
    struct Rem { int hour; int minute; char label[24]; bool on; };
    void drawEdit();
    void save();
    void openEdit(int idx);
    void saveEdit();
    void del();
    std::vector<Rem> _rem;
    int  _sel      = 0;
    bool _edit     = false;
    int  _editIdx  = -1;
    int  _eh = 8, _em = 0, _ef = 0;
    char _elabel[24] = {};
    int  _lastMin  = -1;
};

#endif // APP_REMINDERS_H
