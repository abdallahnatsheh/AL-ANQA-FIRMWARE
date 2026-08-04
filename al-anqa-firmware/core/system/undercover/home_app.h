// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_app — the abstract base class for every home-launcher app (a State/Strategy
// design: the HomeLauncher holds one active HomeApp* and forwards lifecycle + input
// to it). Each concrete app (Calculator, Clock, Reminders, Weather, Calendar,
// Flashlight, Settings) subclasses this and draws through the injected Ui widget
// toolkit. Input handlers return a Nav telling the launcher what to do next.

#ifndef HOME_APP_H
#define HOME_APP_H

#include "touch_manager.h"     // TouchEvent
#include "input_handling.h"    // TrackballEvent

class Ui;   // widget toolkit (home_widgets.h) — injected into every app

// What an app wants the launcher to do after handling an event.
enum class Nav {
    Stay,        // remain in this app (launcher re-renders)
    Back,        // return to the home grid (or a parent, for multi-page apps)
    ExitCover    // leave the whole cover (secret-passphrase-style exit)
};

class HomeApp {
public:
    explicit HomeApp(Ui& ui) : _ui(ui) {}
    virtual ~HomeApp() = default;

    virtual const char* title() const = 0;    // shown in the app bar / tile label
    virtual void onEnter() {}                  // reset per-open state when opened
    virtual void draw() = 0;                   // compose the screen via _ui

    virtual Nav  onTouch(const TouchEvent&)  { return Nav::Stay; }
    virtual Nav  onTrackball(TrackballEvent) { return Nav::Stay; }
    virtual Nav  onKey(char)                 { return Nav::Stay; }

    virtual void tick()            {}          // background work, runs every loop on ALL apps
    virtual bool animating() const { return false; }  // true → launcher repaints at ~10fps
    virtual bool keepAwake() const { return false; }  // true → hold the screen awake while active (flashlight)

protected:
    Ui& _ui;   // shared widget toolkit (dependency-injected by the launcher)
};

#endif // HOME_APP_H
