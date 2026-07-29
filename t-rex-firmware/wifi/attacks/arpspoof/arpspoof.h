// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// arpspoof / as — L2 ARP cache poisoning (Network).
//
// Poisons a victim's + the gateway's ARP caches (bidirectional) so both map the
// other's IP to OUR MAC. On a normal switched WiFi this redirects the victim's
// traffic to us — but with no IP forwarding on a single radio it is a
// redirect/blackhole (DoS), NOT a transparent interceptor (honest on-screen).
//
// While poisoning it ALSO sniffs the redirected uplink (the AP relays those
// frames to us decrypted) and logs what the victim is trying to reach — dst IP +
// DNS domain / HTTP host / HTTPS domain (TLS SNI) — live + to /apps/arpspoof/NNN.csv. So even
// though traffic is blackholed, you see the victim's requests. Heals on exit.
//
// Own networks only. Free-function entry (wardrive/isoscan pattern).
#pragma once

void runArpSpoof(char* args);
