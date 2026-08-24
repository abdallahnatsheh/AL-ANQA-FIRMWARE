// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef CHROMECAST_H
#define CHROMECAST_H

// cast / ca — Google Cast (Chromecast / Google TV / Nest) control (Network).
// Discovers casts via mDNS, launches / rickrolls via DIAL (HTTP :8008), and
// controls playback via Cast v2 (protobuf over TLS :8009). Own networks only.
// Free-function entry (dpwo/arpspoof pattern). See chromecast.cpp header.
void runCast(char* args);

#endif // CHROMECAST_H
