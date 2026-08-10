// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared .cap parser — extracts a WPA/WPA2 4-way-handshake (M1 ANonce +
// M2 SNonce/MIC) OR a PMKID from a classic libpcap capture (linktype 105).
// Factored out of capcrack so both `cc` (offline cap cracker) and `pwn`
// (autonomous capture→crack pet, backlog cracking) share one parser instead of
// each hand-rolling the EAPOL/PMKID extraction (rule 5b, DRY). Header-only.

#ifndef CAP_PARSE_H
#define CAP_PARSE_H

#include <FS.h>
#include <SD.h>
#include <string.h>
#include <stdio.h>
#include "dot11.h"
#include "pcap_writer.h"

namespace capparse {

// Crack material extracted from a .cap. `haveHs` → a full M1+M2 handshake with an
// ESSID (crackable via wpacrack::verifyHandshake); `havePmkid` → a PMKID + ESSID
// (crackable via wpacrack::verifyPMKID).
struct CrackJob {
    bool     haveHs;
    bool     havePmkid;
    char     ssid[33];
    uint8_t  apMac[6], staMac[6];
    uint8_t  anonce[32], snonce[32], mic[16];
    uint8_t  eapol[256]; uint16_t eapolLen;     // M2 frame, MIC field zeroed
    uint8_t  pmkid[16];
};

// Parse a .cap file at `path` into `job`. Returns true if a crackable HS or PMKID
// (with ESSID) was found; false otherwise, writing a short reason into err.
inline bool parseCap(const char* path, CrackJob& job, char* err, size_t errN) {
    memset(&job, 0, sizeof(job));
    File f = SD.open(path, FILE_READ);
    if (!f) { snprintf(err, errN, "Cannot open file"); return false; }

    bool sw = false; uint32_t lt = 0;
    if (!pcap::readGlobalHeader(f, &sw, &lt)) {
        f.close(); snprintf(err, errN, "Not a classic .cap/.pcap"); return false;
    }

    bool haveM1 = false, haveM2 = false;
    uint8_t fr[512];
    uint16_t n;
    while ((n = pcap::readRecord(f, sw, fr, sizeof(fr))) > 0) {
        if (n < 24) continue;
        uint8_t ft = dot11::fType(fr), st = dot11::fSubtype(fr);

        if (ft == 0 && (st == dot11::ST_BEACON || st == dot11::ST_PROBE_RESP)) {
            if (!job.ssid[0]) {
                char s[33];
                if (dot11::extractSSID(fr, n, st, s, sizeof(s))) { strncpy(job.ssid, s, 32); job.ssid[32] = '\0'; }
            }
            continue;
        }
        if (ft != 2) continue;

        dot11::Eapol ek;
        if (!dot11::parseEapol(fr, n, ek)) continue;

        if (ek.msg == 1 && ek.len >= 49) {
            if (!haveM1) {
                memcpy(job.anonce, ek.p + 17, 32);
                memcpy(job.apMac, dot11::dataBssid(fr), 6);     // M1 fromDS: bssid = addr2
                haveM1 = true;
            }
            // PMKID KDE in M1 Key Data: DD <len> 00 0F AC 04 <16B PMKID>
            if (!job.havePmkid && ek.len >= 99) {
                uint16_t kdl = ((uint16_t)ek.p[97] << 8) | ek.p[98];
                uint32_t maxkd = ek.len - 99; if (kdl > maxkd) kdl = (uint16_t)maxkd;
                const uint8_t* kd = ek.p + 99;
                for (uint16_t i = 0; i + 22 <= kdl; i++) {
                    if (kd[i] == 0xDD && kd[i + 2] == 0x00 && kd[i + 3] == 0x0F &&
                        kd[i + 4] == 0xAC && kd[i + 5] == 0x04) {
                        memcpy(job.pmkid, kd + i + 6, 16);
                        memcpy(job.apMac,  dot11::dataBssid(fr), 6);
                        memcpy(job.staMac, fr + 4, 6);          // M1 fromDS: DA = STA
                        job.havePmkid = true;
                        break;
                    }
                }
            }
        } else if (ek.msg == 2 && ek.len >= 97 && !haveM2) {
            memcpy(job.snonce, ek.p + 17, 32);
            memcpy(job.mic,    ek.p + 81, 16);
            uint16_t el = (((uint16_t)ek.p[2] << 8) | ek.p[3]) + 4;
            if (el > ek.len)            el = ek.len;
            if (el > sizeof(job.eapol)) el = sizeof(job.eapol);
            memcpy(job.eapol, ek.p, el);
            memset(job.eapol + 81, 0, 16);                       // zero MIC for the HMAC
            job.eapolLen = el;
            memcpy(job.apMac,  dot11::dataBssid(fr), 6);         // M2 toDS: bssid = addr1
            memcpy(job.staMac, fr + 10, 6);                      // M2 toDS: SA = STA
            haveM2 = true;
        }
    }
    f.close();

    if (haveM1 && haveM2 && job.ssid[0]) job.haveHs = true;
    if (job.haveHs || (job.havePmkid && job.ssid[0])) return true;

    snprintf(err, errN, "No HS/PMKID:%s%s%s%s", job.ssid[0] ? "" : " noESSID",
             haveM1 ? "" : " noM1", haveM2 ? "" : " noM2",
             job.havePmkid ? " (PMKID found but no ESSID)" : "");
    return false;
}

} // namespace capparse

#endif // CAP_PARSE_H
