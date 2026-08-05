// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef DPWO_H
#define DPWO_H

// dpwo / dw — default-password checker. Probes a discovered host's common
// services and tries a small curated list of factory/default credentials.
// Free-function entry (arpspoof/wpa3down pattern). See dpwo.cpp header.
void runDpwo(char* args);

#endif // DPWO_H
