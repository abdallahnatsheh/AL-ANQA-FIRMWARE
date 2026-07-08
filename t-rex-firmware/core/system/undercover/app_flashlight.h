// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// FlashlightApp — a full-white screen that stays awake. See home_app.h.

#ifndef APP_FLASHLIGHT_H
#define APP_FLASHLIGHT_H

#include "home_app.h"
#include "home_widgets.h"

class FlashlightApp : public HomeApp {
public:
    explicit FlashlightApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Flashlight"; }
    void draw() override;
    Nav  onTouch(const TouchEvent&) override    { return Nav::Back; }
    Nav  onTrackball(TrackballEvent) override   { return Nav::Back; }
    bool keepAwake() const override             { return true; }
};

#endif // APP_FLASHLIGHT_H
