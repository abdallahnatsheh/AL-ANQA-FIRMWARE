// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// pwn / pw — autonomous "pwnagotchi" WiFi pet. Roams channels, captures WPA/WPA2
// handshakes + PMKIDs, and (the novel part) CRACKS them on-device during idle
// time with a per-capture resume cursor + smart priority ordering. Three modes:
//   pwn          active   (deauth-forced handshakes, loud)
//   pwn stealth  quiet    (PMKID/directed-deauth, low IDS signature)
//   pwn passive  silent   (zero TX, sniff-only)
// Whitelist subcommands: pwn wl [list|add <idx|bssid>|add ssid <name>|rm <n>|clear]
//
// Reuses the shared crack engine (wpa_crack.h), the shared .cap parser
// (cap_parse.h) and pcap writer (pcap_writer.h) — see docs/plans/pwnagotchi-pwn.md.
// Own / authorized networks only.

#ifndef PWN_H
#define PWN_H

void runPwn(char* args);

#endif // PWN_H
