// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// netspy / ns — client-isolation bypass network intelligence.
// Free-function entry (wardrive/editor pattern).
#pragma once

#include <stdint.h>

void runNetspy(char* args);

// Discovered-device accessors so portscan/ping can target the netspy list
// (e.g. `ps ns3`, `pg ns0`). The table persists after `ns` exits, like the
// netdiscover ARP cache. netspyDeviceIp returns host-order IPv4, 0 if invalid.
int      netspyDeviceCount();
uint32_t netspyDeviceIp(int idx);

// Extra accessors for isoscan/is (Stage 2 active attacks) — it needs the victim
// MAC (spoof / inject target) and a display name for the confirm-before-fire
// prompt. netspyDeviceMac copies the 6-byte MAC into out and returns true when
// idx is valid; netspyDeviceName returns the hostname (or vendor fallback), or
// nullptr when idx is out of range. Read the same persistent s_dev[] table.
bool        netspyDeviceMac(int idx, uint8_t out[6]);
const char* netspyDeviceName(int idx);

// Copy the live group temporal key (GTK) for isoscan's inject attacks. Reads
// gWpaSm+0x174 (same source as `ns gtk`). out must hold >=32 bytes; the real
// length (16 for CCMP / AES-128) is returned via lenOut. Returns true only when
// a plausible key (len 16 or 32) is present — false if the offset looks wrong
// (not associated / toolchain drift), so the caller can bail before injecting.
bool netspyGetGtk(uint8_t out[32], int* lenOut);
