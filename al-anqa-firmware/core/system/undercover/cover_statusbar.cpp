// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// cover_statusbar — see cover_statusbar.h.

#include "cover_statusbar.h"
#include "clock_manager.h"
#include "battery_manager.h"
#include <Arduino.h>
#include <WiFi.h>

extern BatteryManager batteryManager;

// Cached battery read — getPct() samples the ADC 20x and the bar repaints often.
static int      s_batPct = -1;
static bool     s_batChg = false;
static uint32_t s_batMs  = 0;
static void refreshBattery() {
    uint32_t now = millis();
    if (s_batPct < 0 || now - s_batMs >= 10000) {
        int p = batteryManager.getPct();
        s_batPct = p < 0 ? 0 : (p > 100 ? 100 : p);
        s_batChg = batteryManager.isCharging();
        s_batMs  = now;
    }
}

void drawCoverStatusBar(lgfx::LovyanGFX* G, lgfx::VLWfont* metaFont) {
    const int H = COVER_SB_H;
    uint16_t C_BAR   = G->color565(0x17, 0x1b, 0x22);
    uint16_t C_INK   = G->color565(0xe8, 0xea, 0xed);
    uint16_t C_MUTED = G->color565(0x7d, 0x82, 0x8b);
    uint16_t C_GREEN = G->color565(0x4c, 0xaf, 0x50);
    uint16_t C_RED   = G->color565(0xe5, 0x3e, 0x3e);

    G->fillRect(0, 0, SCREEN_WIDTH, H, C_BAR);
    G->setFont(metaFont);

    // real clock (live via ClockManager)
    char t[8]; ClockManager::instance().getShortTime(t, sizeof(t));
    G->setTextColor(C_INK);
    G->setTextDatum(textdatum_t::middle_left);
    G->drawString(t, 12, H / 2);

    // fake carrier
    G->setTextColor(C_MUTED);
    G->setTextDatum(textdatum_t::middle_center);
    G->drawString("CRIMSON MOBILE", SCREEN_WIDTH / 2, H / 2);
    G->setTextDatum(textdatum_t::top_left);

    // signal bars — reflect real WiFi connectivity/strength (like the CLI bar);
    // filled bars = current RSSI level, dim bars = the rest. Disconnected = all dim.
    uint16_t C_DIM = G->color565(0x3a, 0x40, 0x4a);
    int bars = 0;
    if (WiFi.status() == WL_CONNECTED) {
        int r = WiFi.RSSI();
        bars = (r >= -55) ? 4 : (r >= -65) ? 3 : (r >= -75) ? 2 : 1;
    }
    int bx = SCREEN_WIDTH - 58, by = H - 5;
    for (int i = 0; i < 4; i++) {
        int h = 3 + i * 3;
        G->fillRect(bx + i * 5, by - h, 3, h, i < bars ? C_INK : C_DIM);
    }

    // battery (real % + charging bolt)
    refreshBattery();
    int btx = SCREEN_WIDTH - 30, bty = (H - 11) / 2, btw = 22, bth = 11;
    G->drawRoundRect(btx, bty, btw, bth, 2, C_INK);
    G->fillRect(btx + btw, bty + 3, 2, bth - 6, C_INK);                  // nub
    if (s_batChg) {
        G->fillRect(btx + 2, bty + 2, btw - 4, bth - 4, C_GREEN);       // green body
        int cx = btx + btw / 2, cy = bty + bth / 2;
        G->fillTriangle(cx + 2, cy - 4, cx - 3, cy + 1, cx + 1, cy + 1, C_BAR);  // bolt
        G->fillTriangle(cx - 1, cy - 1, cx + 3, cy - 1, cx - 2, cy + 4, C_BAR);
    } else {
        int fw = (btw - 4) * s_batPct / 100;
        uint16_t fill = (s_batPct <= 15) ? C_RED : C_INK;               // low = red
        if (fw > 0) G->fillRect(btx + 2, bty + 2, fw, bth - 4, fill);
    }
}
