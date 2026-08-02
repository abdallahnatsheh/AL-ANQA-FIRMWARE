// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared 802.11 beacon synthesizer — header-only, no state. PC crackers
// (aircrack-ng, hashcat via hcxpcapngtool) require a BEACON or PROBE-RESPONSE
// carrying the ESSID in the .cap before they will pair an EAPOL handshake/PMKID
// with a network: ESSID is mandatory to derive the PMK. Promiscuous EAPOL-only
// captures (ws / pm) never see the AP's own beacon, so we synthesize one here
// from the SSID/BSSID/channel we already know at capture time and prepend it to
// the .cap. The frame is a WPA2-PSK (RSN + Privacy) beacon so the tools classify
// the net correctly. Build into RAM, write to SD only after WiFi teardown
// (GDMA rule). Mirrors the beacon the karma rogue-AP engine already embeds.

#ifndef BEACON_BUILD_H
#define BEACON_BUILD_H

#include <Arduino.h>

namespace dot11 {

// Max synthesized beacon length: 24 (hdr) + 8 (ts) + 2 (interval) + 2 (cap)
//   + 2+32 (SSID IE) + 10 (rates) + 3 (DS) + 22 (RSN) = 105 bytes.
static const uint16_t BEACON_MAX_LEN = 128;

// Build a WPA2-PSK beacon for `ssid` advertised by `bssid` on `channel`.
// Returns the frame length, or 0 if `ssid` is empty (no usable ESSID → no
// point writing a beacon). Caller must provide a buffer of >= BEACON_MAX_LEN.
inline uint16_t buildBeacon(uint8_t* pkt, const char* ssid,
                            const uint8_t* bssid, uint8_t channel) {
    uint8_t sl = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
    if (sl == 0) return 0;                       // hidden/empty SSID is useless to crackers

    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    // Supported rates (1..54 Mbps), element id 0x01
    static const uint8_t RATES[] = { 0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C };
    // RSN IE (WPA2-PSK, CCMP), element id 0x30 — marks the net WPA2-PSK
    static const uint8_t RSN[] = {
        0x30, 0x14,
        0x01, 0x00,                          // version 1
        0x00, 0x0F, 0xAC, 0x04,              // group cipher CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,  // 1 pairwise cipher: CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,  // 1 akm: PSK
        0x00, 0x00,                          // RSN capabilities
    };

    uint8_t* p = pkt;
    // MAC header — mgmt/beacon (subtype 8), DA=broadcast, SA=BSSID=AP
    p[0] = 0x80; p[1] = 0x00; p[2] = 0x00; p[3] = 0x00;   // FC + duration
    memcpy(p + 4,  bcast, 6);                              // addr1 DA = broadcast
    memcpy(p + 10, bssid, 6);                              // addr2 SA = AP
    memcpy(p + 16, bssid, 6);                              // addr3 BSSID
    p[22] = 0x00; p[23] = 0x00;                            // seq/frag
    uint16_t o = 24;

    memset(p + o, 0, 8); o += 8;                  // timestamp
    p[o++] = 0x64; p[o++] = 0x00;                 // beacon interval 100 TU
    p[o++] = 0x11; p[o++] = 0x04;                 // capability: ESS + Privacy + short slot
    // SSID IE (id 0)
    p[o++] = 0x00; p[o++] = sl; memcpy(p + o, ssid, sl); o += sl;
    // Supported rates IE
    memcpy(p + o, RATES, sizeof(RATES)); o += sizeof(RATES);
    // DS Parameter Set (current channel)
    p[o++] = 0x03; p[o++] = 0x01; p[o++] = channel;
    // RSN IE → WPA2-PSK
    memcpy(p + o, RSN, sizeof(RSN)); o += sizeof(RSN);

    return o;
}

} // namespace dot11

#endif // BEACON_BUILD_H
