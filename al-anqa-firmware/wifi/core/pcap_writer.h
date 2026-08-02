// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared libpcap writer — LINKTYPE_IEEE802_11 (105), Wireshark / aircrack-ng /
// hashcat compatible. Replaces the global+record header structs hand-rolled in
// handshake, pmkid, wifimon and karma. Open the File before WiFi (GDMA rule),
// write the global header once, then a record per frame.

#ifndef PCAP_WRITER_H
#define PCAP_WRITER_H

#include <FS.h>

namespace pcap {

inline void writeGlobalHeader(fs::File& f) {
    struct __attribute__((packed)) {
        uint32_t magic = 0xa1b2c3d4; uint16_t vmaj = 2, vmin = 4;
        int32_t  tz = 0; uint32_t sig = 0, snap = 65535, linktype = 105;
    } gh;
    f.write((uint8_t*)&gh, sizeof(gh));
}

inline void writeRecord(fs::File& f, const uint8_t* d, uint16_t len, uint32_t tsMs) {
    struct __attribute__((packed)) { uint32_t ts_sec, ts_usec, incl, orig; } rh;
    rh.ts_sec = tsMs / 1000; rh.ts_usec = (tsMs % 1000) * 1000;
    rh.incl = rh.orig = len;
    f.write((uint8_t*)&rh, sizeof(rh));
    f.write(d, len);
}

// ── reading (for the cap cracker) — classic libpcap only (.cap/.pcap, not pcapng) ─
// Reads the 24-byte global header; sets *swapped for big-endian captures and
// *linktype. Returns false if the magic is not a classic-pcap magic.
inline bool readGlobalHeader(fs::File& f, bool* swapped, uint32_t* linktype) {
    uint8_t b[24];
    if (f.read(b, 24) != 24) return false;
    uint32_t magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    bool sw;
    if      (magic == 0xa1b2c3d4) sw = false;   // native little-endian (our writer)
    else if (magic == 0xd4c3b2a1) sw = true;    // byte-swapped (big-endian capture)
    else return false;                          // pcapng or not a pcap
    uint32_t lt = sw ? ((uint32_t)b[23] | ((uint32_t)b[22] << 8) | ((uint32_t)b[21] << 16) | ((uint32_t)b[20] << 24))
                     : ((uint32_t)b[20] | ((uint32_t)b[21] << 8) | ((uint32_t)b[22] << 16) | ((uint32_t)b[23] << 24));
    if (swapped)  *swapped = sw;
    if (linktype) *linktype = lt;
    return true;
}

// Reads the next record's frame bytes into buf (up to cap). Returns the number of
// bytes read (0 at EOF / on a short header). Over-long frames are truncated and the
// remainder skipped so iteration stays aligned.
inline uint16_t readRecord(fs::File& f, bool swapped, uint8_t* buf, uint16_t cap) {
    uint8_t rh[16];
    if (f.read(rh, 16) != 16) return 0;
    uint32_t incl = swapped ? ((uint32_t)rh[11] | ((uint32_t)rh[10] << 8) | ((uint32_t)rh[9] << 16) | ((uint32_t)rh[8] << 24))
                            : ((uint32_t)rh[8]  | ((uint32_t)rh[9]  << 8) | ((uint32_t)rh[10] << 16) | ((uint32_t)rh[11] << 24));
    if (incl == 0) return 0;
    uint16_t want = incl > cap ? cap : (uint16_t)incl;
    int got = f.read(buf, want);
    if (got < 0) got = 0;
    if (incl > (uint32_t)got) f.seek(f.position() + (incl - got));   // skip remainder
    return (uint16_t)got;
}

} // namespace pcap

#endif // PCAP_WRITER_H
