// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared MAC-address helpers — header-only. The random locally-administered MAC
// recipe was duplicated in mac_changer, karma (rogue AP BSSID), beacon_flood and
// eviltwin; this is the single source of truth.

#ifndef MAC_UTIL_H
#define MAC_UTIL_H

#include <stdint.h>
#include <esp_random.h>

namespace macutil {

// Random locally-administered, unicast MAC: LA bit (0x02) set, multicast bit (0x01)
// clear. Used for spoofed STA identities and rogue-AP BSSIDs.
inline void randomLaMac(uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(esp_random() & 0xFF);
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

// Random BLE static random address: the two most-significant bits of the
// most-significant byte must be 11.
inline void randomBleMac(uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(esp_random() & 0xFF);
    mac[5] |= 0xC0;
}

}  // namespace macutil

#endif  // MAC_UTIL_H
