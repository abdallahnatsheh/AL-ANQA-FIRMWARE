// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// isoscan / is — ACTIVE client-isolation bypass attacks (AirSnitch Stage 2).
//
// The offensive counterpart to netspy/ns. Where `ns` is 100% PASSIVE (only
// listens to AP-relayed group frames), `is` TRANSMITS: it forges frames at a
// chosen victim to bypass client isolation (GTK-encrypted broadcast inject,
// gateway-bounce, MAC-spoof port stealing, ICMPv6 RA DNS poison).
//
// Deliberately a SEPARATE command from `ns` so passive recon can never
// accidentally put a frame on the air — running `is` is an explicit opt-in.
//
// Victims are picked from the netspy discovered-device list (run `ns` first):
//   is ns3 <attack>   attack netspy device #3 directly (like `ps ns3`)
//   is                interactive victim picker + attack menu
//
// Free-function entry (wardrive/netspy pattern).
#pragma once

void runIsoscan(char* args);
