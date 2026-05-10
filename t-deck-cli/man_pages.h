// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2024 Abdallah Natsheh

#ifndef MAN_PAGES_H
#define MAN_PAGES_H

#include <Arduino.h>
#include "display_manager.h"
#include "input_handling.h"

class ManPages {
public:
    ManPages(DisplayManager& dm);
    void show(char* args);
private:
    DisplayManager& _dm;
    void renderPage(int idx);
};

#endif // MAN_PAGES_H
