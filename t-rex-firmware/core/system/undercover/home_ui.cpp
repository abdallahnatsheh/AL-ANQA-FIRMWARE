// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_ui — the HomeLauncher orchestrator (the "context" of the State pattern).
// It owns the Ui widget toolkit + one instance of each app (home_apps.h), draws
// the home grid + hero, runs the cover loop (covert plumbing via cover_kit), and
// dispatches input to the active app. Every app is tick()'d every loop so a timer
// keeps counting / a reminder can fire while you're in another app. The Notes tile
// hands the whole screen to runNotesUi() (its own sprite session).
//
//   standalone=true  (`home`/`hm` command): restores the CLI on exit.
//   standalone=false (undercover cover): leaves the screen blocked, no CLI touch.
// Returns true iff it exited via the secret passphrase.

#include "home_ui.h"
#include "home_app.h"
#include "home_widgets.h"
#include "home_apps.h"
#include "notes_ui.h"            // runNotesUi(false)
#include "cover_kit.h"           // shared cover shell (sprite/fonts/passphrase/touch-wake)
#include "undercover_config.h"   // ucHasPassphrase (home-grid q-exit)
#include "display_manager.h"
#include "input_handling.h"
#include "touch_manager.h"
#include "lockscreen_manager.h"
#include "powersave_manager.h"
#include "clock_manager.h"
#include "weather_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <string.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── Home-grid layout ─────────────────────────────────────────────────────────
#define HERO_Y    22
#define HERO_H    72
#define GRID_Y    (HERO_Y + HERO_H)   // 94
#define COLS      4
#define ROWS      2
#define NTILES    8
#define G_MARGIN  14
#define ICON_W    50

class HomeLauncher {
public:
    bool run(bool standalone);

private:
    void render();
    void drawHome();
    void drawHero();
    void drawGrid();
    void tileCell(int i, int& cx, int& ty);
    void openTile(int i);
    void homeTouch(const TouchEvent& te);
    void homeTrackball(TrackballEvent tb);

    Ui _ui;
    // one instance of each app (dependency-injected with the shared Ui)
    CalculatorApp _calc{_ui};
    ClockApp      _clock{_ui};
    RemindersApp  _reminders{_ui};
    WeatherApp    _weather{_ui};
    CalendarApp   _calendar{_ui};
    FlashlightApp _flashlight{_ui};
    SettingsApp   _settings{_ui};
    HomeApp*      _apps[7] = { &_calc, &_clock, &_reminders, &_weather, &_calendar, &_flashlight, &_settings };
    HomeApp*      _active  = nullptr;   // nullptr = home grid

    struct Tile { const char* label; uint32_t accent; char icon; HomeApp* app; bool notes; };
    Tile _tiles[NTILES];

    int      _sel = 5;                  // grid selection (start on Notes)
    uint32_t _lastTapMs = 0, _lastTbMs = 0;
    uint32_t _lastAnimMs = 0, _lastBannerMs = 0, _lastClockMs = 0;
    bool     _wasDimmed = false, _prevShowing = false;
    bool     _exit = false, _secretExit = false;
};

// ── grid geometry ─────────────────────────────────────────────────────────────
void HomeLauncher::tileCell(int i, int& cx, int& ty) {
    int c = i % COLS, r = i / COLS;
    int cellW = (SCREEN_WIDTH - 2 * G_MARGIN) / COLS;
    int cellH = (SCREEN_HEIGHT - GRID_Y) / ROWS;
    int cellX = G_MARGIN + c * cellW;
    int cellY = GRID_Y + r * cellH;
    cx = cellX + cellW / 2;
    ty = cellY + 6;
}

void HomeLauncher::drawGrid() {
    auto* G = cover::G;
    for (int i = 0; i < NTILES; i++) {
        int cx, ty; tileCell(i, cx, ty);
        uint16_t accent = _ui.col(_tiles[i].accent);
        int tx0 = cx - ICON_W / 2;
        G->fillSmoothRoundRect(tx0 + 1, ty + 3, ICON_W, ICON_W, 13, _ui.shadow);        // drop shadow
        if (i == _sel) G->fillSmoothRoundRect(tx0 - 3, ty - 3, ICON_W + 6, ICON_W + 6, 15, _ui.sel);
        G->fillSmoothRoundRect(tx0, ty, ICON_W, ICON_W, 13, accent);                    // tile body
        _ui.tileIcon(cx, ty + ICON_W / 2, _tiles[i].icon, accent);
        G->setFont(_ui.fMeta());
        G->setTextColor(i == _sel ? _ui.ink : _ui.muted);
        G->setTextDatum(textdatum_t::middle_center);
        G->drawString(_tiles[i].label, cx, ty + ICON_W + 9);
        G->setTextDatum(textdatum_t::top_left);
    }
}

void HomeLauncher::drawHero() {
    auto* G = cover::G;
    bool valid = ClockManager::instance().isValid();
    char t[8]; ClockManager::instance().getShortTime(t, sizeof(t));
    char d[28];
    if (valid) {
        time_t now = time(nullptr); struct tm* lt = localtime(&now);
        if (lt) strftime(d, sizeof(d), "%a, %b %d", lt); else strcpy(d, "");
    } else strcpy(d, "Set date & time");
    G->setFont(_ui.fBig()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_left);
    G->drawString(t, 18, HERO_Y + 30);
    G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
    G->setTextDatum(textdatum_t::top_left);
    G->drawString(d, 20, HERO_Y + 48);

    WeatherManager& wx = WeatherManager::instance();
    int sx = SCREEN_WIDTH - 92, sy = HERO_Y + 34;
    if (wx.hasReading()) {
        _ui.weatherIcon(sx, sy, wx.code());
        char nb[6]; snprintf(nb, sizeof(nb), "%d", wx.temp());
        char ub[2] = { (char)(wx.imperial() ? 'F' : 'C'), 0 };
        G->setFont(_ui.fTitle()); int nw = G->textWidth(nb);
        int total = nw + 5, x = SCREEN_WIDTH - 14 - total - 8;
        G->setTextColor(_ui.ink); G->setTextDatum(textdatum_t::middle_left);
        G->drawString(nb, x, sy);
        G->drawCircle(x + nw + 2, sy - 7, 2, _ui.ink);
        G->setFont(_ui.fMeta()); G->drawString(ub, x + nw + 5, sy);
        G->setTextColor(_ui.muted); G->setTextDatum(textdatum_t::middle_right);
        G->drawString(wx.condition(), SCREEN_WIDTH - 14, HERO_Y + 54);
        G->setTextDatum(textdatum_t::top_left);
    } else {
        G->fillSmoothCircle(sx + 14, sy, 7, _ui.hair);
        G->setFont(_ui.fTitle()); G->setTextColor(_ui.muted);
        G->setTextDatum(textdatum_t::middle_right);
        G->drawString("--", SCREEN_WIDTH - 16, HERO_Y + 34);
        G->setTextDatum(textdatum_t::top_left);
    }
}

void HomeLauncher::drawHome() {
    cover::G->fillScreen(_ui.bg);
    _ui.statusBar();
    drawHero();
    drawGrid();
}

// Compose the active app (or the home grid) + the alarm banner overlay, then blit.
void HomeLauncher::render() {
    if (_active) _active->draw();
    else         drawHome();
    if (_active != &_flashlight) _ui.drawBanner();   // banner floats over any app except the torch
    cover::flush();
}

// ── open a tile: an in-launcher app, or hand off to Notes ────────────────────
void HomeLauncher::openTile(int i) {
    _sel = i;
    if (_tiles[i].notes) {
        cover::teardownCanvas();             // free sprite+fonts; Notes takes them
        bool secret = runNotesUi(false);
        if (secret) { _exit = true; _secretExit = true; return; }
        cover::setupCanvas(); _ui.init();    // recreate + re-init palette
        render();
    } else if (_tiles[i].app) {
        _active = _tiles[i].app;
        _active->onEnter();
        render();
    }
}

void HomeLauncher::homeTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return;
    for (int i = 0; i < NTILES; i++) {
        int cx, ty; tileCell(i, cx, ty);
        if (te.x >= cx - ICON_W / 2 - 4 && te.x <= cx + ICON_W / 2 + 4 &&
            te.y >= ty - 4 && te.y <= ty + ICON_W + 4) { openTile(i); return; }
    }
}

void HomeLauncher::homeTrackball(TrackballEvent tb) {
    int prev = _sel;
    if      (tb == TBALL_LEFT  && _sel > 0)           _sel--;
    else if (tb == TBALL_RIGHT && _sel < NTILES - 1)  _sel++;
    else if (tb == TBALL_UP    && _sel >= COLS)       _sel -= COLS;
    else if (tb == TBALL_DOWN  && _sel <  COLS)       _sel += COLS;
    else if (tb == TBALL_CLICK) { openTile(_sel); return; }
    if (_sel != prev) render();
}

// ── main loop ────────────────────────────────────────────────────────────────
bool HomeLauncher::run(bool standalone) {
    _tiles[0] = { "Calculator", 0x34A853, 'K', &_calc,       false };
    _tiles[1] = { "Clock",      0x1DA1F2, 'T', &_clock,      false };
    _tiles[2] = { "Reminders",  0xEA4335, 'R', &_reminders,  false };
    _tiles[3] = { "Weather",    0x00ACC1, 'W', &_weather,    false };
    _tiles[4] = { "Flashlight", 0xFB8C00, 'F', &_flashlight, false };
    _tiles[5] = { "Notes",      0xF4B740, 'N', nullptr,      true  };
    _tiles[6] = { "Calendar",   0x5C6BC0, 'C', &_calendar,   false };
    _tiles[7] = { "Settings",   0x78909C, 'S', &_settings,   false };

    cover::setupCanvas();
    _ui.init();
    displayManager.setBlocked(true);
    cover::resetPassphrase();
    _ui.dismissAlarm();
    _sel = 5; _active = nullptr; _exit = false; _secretExit = false;
    _wasDimmed = false; _prevShowing = false; _lastTapMs = 0;
    uint32_t now0 = millis();
    _lastAnimMs = _lastBannerMs = _lastClockMs = _lastTbMs = now0;

    _clock.reset();       // fresh stopwatch/timer each session
    _reminders.load();    // reload SD reminders + re-baseline the minute check

    render();

    // Hero weather: one fetch on entry if online + stale (cover screen, WiFi idle).
    {
        WeatherManager& wx = WeatherManager::instance();
        if (wx.configured() && wx.stale() && WiFi.status() == WL_CONNECTED) { if (wx.forceFetch()) render(); }
    }

    TouchManager& tm = TouchManager::instance();

    while (true) {
        // Unlock repaint.
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { displayManager.setBlocked(true); render(); }
        // Lock stand-down: let LockScreenManager own the panel; keep pumping keys.
        if (LockScreenManager::getInstance().isLocked()) {
            inputHandler.getKeyboardInput(); vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        // Flashlight stays bright.
        if (_active && _active->keepAwake()) inputHandler.updateActivity();
        // Dim→wake repaint (terminal must never bleed through the cover).
        {
            bool dimmed = PowerSaveManager::getInstance().isDimmed();
            if (_wasDimmed && !dimmed) { displayManager.setBlocked(true); render(); }
            _wasDimmed = dimmed;
        }

        // Background: tick every app (timers/reminders run on any screen) + alarm ring.
        for (auto* a : _apps) a->tick();
        _ui.tickAlarm();

        // Repaint triggers: animating app (~10fps), alarm banner (~10fps + falling edge),
        // home hero clock (20s).
        uint32_t nowMs = millis();
        bool needR = false;
        if (_active && _active->animating() && nowMs - _lastAnimMs >= 100) { _lastAnimMs = nowMs; needR = true; }
        bool showing = _ui.alarmShowing();
        if (showing && nowMs - _lastBannerMs >= 100) { _lastBannerMs = nowMs; needR = true; }
        if (!showing && _prevShowing) needR = true;
        _prevShowing = showing;
        if (!_active && nowMs - _lastClockMs >= 20000) { _lastClockMs = nowMs; needR = true; }
        if (needR) render();

        // ── touch ──
        TouchEvent te = tm.poll();
        cover::handleTouchWake(te, _lastTapMs);
        if (te.type != TouchEvent::NONE && _ui.alarmShowing()) {   // any touch dismisses the alarm
            _ui.dismissAlarm(); render(); te.type = TouchEvent::NONE;
        }
        if (te.type != TouchEvent::NONE) {
            if (_active) { Nav n = _active->onTouch(te); if (n == Nav::Back) _active = nullptr; render(); }
            else         homeTouch(te);
            if (_exit) break;
        }

        // ── trackball (300ms throttle) ──
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb != TBALL_NONE) {
            if (_ui.alarmShowing()) { _ui.dismissAlarm(); render(); }
            else if (nowMs - _lastTbMs >= 300) {
                _lastTbMs = nowMs;
                if (_active) { Nav n = _active->onTrackball(tb); if (n == Nav::Back) _active = nullptr; render(); }
                else         homeTrackball(tb);
            }
            if (_exit) break;
        }

        // ── keyboard ──
        char k = inputHandler.getKeyboardInput();
        if (cover::feedPassphrase(k)) { _secretExit = true; break; }   // secret exit from any screen
        if (k != 0 && _ui.alarmShowing()) { _ui.dismissAlarm(); render(); k = 0; }
        if (k != 0) {
            if (_active) {
                Nav n = _active->onKey(k);
                if (n == Nav::Back) _active = nullptr;
                render();
            } else {
                // home grid: q exits the cover when no passphrase is configured
                if ((k == 'q' || k == 'Q') && !ucHasPassphrase()) break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    cover::teardownCanvas();

    if (standalone) {
        displayManager.setBlocked(false);
        displayManager.tdeck_begin();   // full clean slate: black + real status bar + prompt
    }
    return _secretExit;
}

// ── public entry point ───────────────────────────────────────────────────────
static HomeLauncher g_launcher;   // persistent so app state survives across sessions
bool runHomeUi(bool standalone) { return g_launcher.run(standalone); }
