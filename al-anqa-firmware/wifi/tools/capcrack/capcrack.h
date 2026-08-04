// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// crack / cc — offline WPA/WPA2 cracker for captured .cap files.
// Parses a libpcap (.cap/.pcap, linktype 105) capture, extracts a 4-way
// handshake (M1 ANonce + M2 SNonce/MIC) OR a PMKID, then dictionary-attacks it
// with the shared wpacrack engine over one or more wordlists (SD files, a whole
// directory of *.txt, or the built-in list). Works on captures from karma, ws,
// pm, or any external tool. Paths resolve against the current `cd` directory, so
// after `cd`-ing into the folder you can pass just the filenames.

#ifndef CAPCRACK_H
#define CAPCRACK_H

void runCapCrack(char* args);

#endif // CAPCRACK_H
