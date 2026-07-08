// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_flashlight.h"

void FlashlightApp::draw() {
    auto* G = _ui.g();
    G->fillScreen(_ui.white);
    G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
    G->setTextDatum(textdatum_t::middle_center);
    G->drawString("Tap to exit", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 16);
    G->setTextDatum(textdatum_t::top_left);
}
