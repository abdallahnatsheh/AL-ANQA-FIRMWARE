// T-REX — offensive security firmware for LilyGo T-Deck
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

} // namespace pcap

#endif // PCAP_WRITER_H
