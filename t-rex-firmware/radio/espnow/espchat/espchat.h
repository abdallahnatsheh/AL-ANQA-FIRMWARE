// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#pragma once
#include <Arduino.h>

// Public API — included by command_manager.cpp and input_handling.cpp
void runEspchat(char* args);

// Background mode — loads contacts.csv + config.conf, auto-picks channel
// (most common contact channel, else stored public_channel, else ch1).
// Pass ch > 0 to override the auto-detected channel.
void startEspchatBg(uint8_t ch = 0);
void stopEspchatBg();
void pollEspchatBg();
bool isEspchatBgActive();
