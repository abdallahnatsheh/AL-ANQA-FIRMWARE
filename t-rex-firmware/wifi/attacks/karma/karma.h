// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// karma / km — anti-client rogue-AP suite.
// Phase 1: probe-request harvest + live network table. Later phases add PNL
// fingerprinting, device intel cards, captive-portal + WPA2 handshake bait,
// and the Auto / Interactive top-level modes. See .claude/memory project_karma_plan.

#ifndef KARMA_H
#define KARMA_H

void runKarma(char* args);

#endif // KARMA_H
