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
