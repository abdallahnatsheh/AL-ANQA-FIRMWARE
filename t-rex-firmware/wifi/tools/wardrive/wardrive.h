// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef WARDRIVE_H
#define WARDRIVE_H

// wardrive / wd — continuous WiFi scan + GPS → WiGLE WiFi-1.4 CSV.
// T-Deck Plus only (needs GPS); on the base board it prints a notice.
// Logs each BSSID once per session to /apps/wardrive/NNN.csv (never overwrites),
// only while a GPS fix is valid. [q] quits.
void runWardrive(char* args);

#endif // WARDRIVE_H
