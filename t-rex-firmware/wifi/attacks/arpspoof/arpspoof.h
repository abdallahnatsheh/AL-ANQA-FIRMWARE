// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// arpspoof / as — L2 ARP cache poisoning (Network, [EXP]).
//
// Poisons a victim's + the gateway's ARP caches (bidirectional) so both map the
// other's IP to OUR MAC. On a normal switched WiFi this redirects the victim's
// traffic to us — but with no IP forwarding on a single radio it is a
// redirect/blackhole (DoS), NOT a transparent interceptor (honest on-screen).
// Heals both caches with the real MACs on exit.
//
// Own networks only. Free-function entry (wardrive/isoscan pattern).
#pragma once

void runArpSpoof(char* args);
