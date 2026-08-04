// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// responder / rsp — LLMNR/NBT-NS poisoner + NetNTLMv2 capture (Network, [EXP]).
//
// Phase 2 of the Network MITM suite (docs/plans/network-mitm-suite.md).
// Answers LLMNR (UDP 5355) and NBT-NS (UDP 137) name queries with OUR IP, then a
// fake HTTP listener (:80) issues an NTLM challenge and captures the victim's
// NetNTLMv2 response to SD for OFFLINE cracking (hashcat -m 5600). No ARP/
// forwarding needed. Methodology follows lgandx/Responder. Own networks only.
//
// Free-function entry (isoscan/wardrive pattern). Requires cw (associated STA).
#pragma once

void runResponder(char* args);
