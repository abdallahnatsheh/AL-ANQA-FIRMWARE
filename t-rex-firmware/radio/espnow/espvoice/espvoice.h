// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef ESPVOICE_H
#define ESPVOICE_H

// ESP-NOW half-duplex walkie-talkie (push-to-talk toggle).
// `ev [ch]` — broadcast voice on WiFi channel ch (default 1).
// T-Deck Plus only (requires ES7210 microphone).
void runEspVoice(char* args);

#endif // ESPVOICE_H
