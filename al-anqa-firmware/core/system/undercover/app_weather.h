// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// WeatherApp — current conditions via WeatherManager (Open-Meteo). See home_app.h.

#ifndef APP_WEATHER_H
#define APP_WEATHER_H

#include "home_app.h"
#include "home_widgets.h"

class WeatherApp : public HomeApp {
public:
    explicit WeatherApp(Ui& ui) : HomeApp(ui) {}
    const char* title() const override { return "Weather"; }
    void onEnter() override;                    // auto-refresh if stale (with indicator)
    void draw() override;
    Nav  onTouch(const TouchEvent&) override;   // tap = refresh
    Nav  onTrackball(TrackballEvent) override;
private:
    void refresh();                             // blocking fetch, self-renders a loading frame first
    bool _loading = false;
};

#endif // APP_WEATHER_H
