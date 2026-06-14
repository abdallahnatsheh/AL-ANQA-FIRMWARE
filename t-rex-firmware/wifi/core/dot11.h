// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared 802.11 frame-parse helpers — header-only, no state. Replaces the copies
// previously hand-rolled in wifimon, hidden_ssid, beacon_flood, wguard, handshake,
// pmkid and karma. Small inline funcs so they fold into IRAM promiscuous callbacks.

#ifndef DOT11_H
#define DOT11_H

#include <Arduino.h>

namespace dot11 {

inline uint8_t fType(const uint8_t* d)    { return (d[0] >> 2) & 0x03; }  // 0 mgmt 1 ctrl 2 data
inline uint8_t fSubtype(const uint8_t* d) { return (d[0] >> 4) & 0x0F; }
inline bool    toDS(const uint8_t* d)     { return  d[1] & 0x01; }
inline bool    fromDS(const uint8_t* d)   { return (d[1] >> 1) & 0x01; }

// management subtypes
enum { ST_ASSOC_REQ = 0, ST_PROBE_REQ = 4, ST_PROBE_RESP = 5, ST_BEACON = 8,
       ST_DISASSOC = 10, ST_DEAUTH = 12 };

// Extract the SSID IE (element id 0). Returns the SSID length (0 = none/wildcard).
// `out` gets a NUL-terminated copy of up to outMax-1 chars. `subtype` selects the
// fixed-field offset (beacon/probe-resp carry 12 extra bytes, assoc-req 4).
inline uint8_t extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype,
                           char* out, uint8_t outMax) {
    uint16_t off = 24;
    if (subtype == ST_BEACON || subtype == ST_PROBE_RESP) off += 12;
    else if (subtype == ST_ASSOC_REQ)                     off += 4;
    while (off + 2u <= len) {
        uint8_t id = d[off], il = d[off + 1];
        if (off + 2u + il > len) break;
        if (id == 0) {
            uint8_t sl = il > (uint8_t)(outMax - 1) ? (uint8_t)(outMax - 1) : il;
            if (sl) memcpy(out, d + off + 2, sl);
            out[sl] = '\0';
            return sl;
        }
        off += 2u + il;
    }
    out[0] = '\0';
    return 0;
}

// BSSID of a DATA frame (toDS → addr1, else addr2).
inline const uint8_t* dataBssid(const uint8_t* d) { return toDS(d) ? d + 4 : d + 10; }

// EAPOL-Key view extracted from a DATA frame.
struct Eapol { const uint8_t* p; int len; uint8_t msg; bool fromSta; };

// True if `d`/`len` is an EAPOL-Key DATA frame; fills `out` (msg = 1..4, the
// 4-way-handshake message number; fromSta = client→AP direction).
inline bool parseEapol(const uint8_t* d, uint16_t len, Eapol& out) {
    if (len < 36) return false;
    if (fType(d) != 2) return false;                       // DATA only
    if (toDS(d) && fromDS(d)) return false;                // skip 4-addr WDS
    int hdrLen = (fSubtype(d) & 0x08) ? 26 : 24;           // QoS data adds 2
    if (len < (uint16_t)(hdrLen + 8 + 8)) return false;    // need >=8 EAPOL bytes (key-info at e[1,5,6])
    const uint8_t* llc = d + hdrLen;                        // LLC/SNAP + EtherType
    if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03 ||
        llc[6] != 0x88 || llc[7] != 0x8E) return false;    // 0x888E = EAPOL
    const uint8_t* e = d + hdrLen + 8;
    if (e[1] != 0x03) return false;                        // EAPOL-Key
    uint8_t kiHi = e[5], kiLo = e[6];
    bool ack = (kiLo & 0x80), mic = (kiHi & 0x01), secure = (kiHi & 0x02);
    uint8_t m = 0;
    if      ( ack && !mic)            m = 1;
    else if (!ack &&  mic && !secure) m = 2;
    else if ( ack &&  mic)            m = 3;
    else if (!ack &&  mic &&  secure) m = 4;
    if (!m) return false;
    out.p = e; out.len = len - hdrLen - 8; out.msg = m; out.fromSta = toDS(d);
    return true;
}

} // namespace dot11

#endif // DOT11_H
