// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// WGuard detection techniques based on published research:
//   Clock skew fingerprinting:
//     Lanze et al., "Clock Skew Based Remote Device Fingerprinting Demystified",
//     IEEE S&P 2012. https://ieeexplore.ieee.org/document/6234403
//   Beacon sequence number gap / interval deviation:
//     Jana & Kasera, "An Accurate Fake AP Detection Method Based on Deviation of
//     Beacon Time Interval", IEEE IPCCC 2010. https://ieeexplore.ieee.org/document/6901631
//     Park et al., "Rogue AP detection mechanism considering sequence number of
//     beacon frame", 2017. https://doi.org/10.1007/s11042-017-4514-5
//   Deauth reason code analysis:
//     Al-Mhiqani et al., "Detection of De-authentication DoS attack in 802.11
//     networks", ResearchGate 2014.
//   Multi-signal suspicion scoring:
//     Arubanetworks WIDS/WIPS detection guide,
//     https://arubanetworking.hpe.com/techdocs/aos/wifi-design-deploy/security/wids-wips/

#include "wguard.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "notification_manager.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include <SD.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── Static ISR state ──────────────────────────────────────────────────────────

volatile WgFrame  WGuard::s_ring[WG_RING_SIZE];
volatile uint8_t  WGuard::s_head            = 0;
volatile uint8_t  WGuard::s_tail            = 0;
volatile uint8_t  WGuard::s_bssid[6]        = {};
volatile char     WGuard::s_ssid[33]        = {};
volatile bool     WGuard::s_active          = false;
volatile uint32_t WGuard::s_isrBeacons      = 0;
volatile uint8_t  WGuard::s_bssidSeen[WG_BSSID_MAX][6] = {};
volatile uint8_t  WGuard::s_bssidSeenN      = 0;
volatile uint32_t WGuard::s_bssidFloodStart = 0;
volatile bool     WGuard::s_bssidFloodFired = false;
volatile uint64_t WGuard::s_lastTargetTs     = 0;
volatile bool     WGuard::s_targetTsSeen     = false;

volatile uint16_t WGuard::s_lastBeaconSeq   = 0;
volatile bool     WGuard::s_beaconSeqSeen   = false;

volatile uint32_t WGuard::s_lastBeaconMs    = 0;
volatile uint32_t WGuard::s_biSum           = 0;
volatile uint8_t  WGuard::s_biSamples       = 0;
volatile uint32_t WGuard::s_biBaseline      = 0;
volatile bool     WGuard::s_biLearned       = false;

volatile uint64_t WGuard::s_csTs[20]        = {};
volatile uint32_t WGuard::s_csArr[20]       = {};
volatile uint8_t  WGuard::s_csCount         = 0;
volatile uint32_t WGuard::s_csSlopeNumer    = 0;
volatile bool     WGuard::s_csSlopeLearned  = false;

// ── ISR helpers ───────────────────────────────────────────────────────────────

void IRAM_ATTR WGuard::extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype, char* out) {
    out[0] = '\0';
    uint16_t pos = 24;
    if (subtype == 8 || subtype == 5) pos += 12;  // skip fixed beacon/probe-resp params
    while (pos + 2 <= len) {
        uint8_t tid = d[pos], tlen = d[pos + 1];
        if (tid == 0) {
            uint8_t l = tlen < 32 ? tlen : 32;
            for (uint8_t i = 0; i < l; i++) out[i] = (char)d[pos + 2 + i];
            out[l] = '\0';
            return;
        }
        pos += 2 + tlen;
    }
}

void IRAM_ATTR WGuard::enqueue(uint8_t sub, const uint8_t* src, const uint8_t* dst,
                                const char* ssid, uint8_t eapolMsg, int8_t rssi,
                                uint16_t seqNum) {
    uint8_t next = (s_head + 1) % WG_RING_SIZE;
    if (next == s_tail) return;
    WgFrame& slot = (WgFrame&)s_ring[s_head];
    slot.subtype  = sub;
    memcpy(slot.src, src, 6);
    memcpy(slot.dst, dst, 6);
    slot.ssid[0]  = '\0';
    if (ssid) { uint8_t i = 0; while (i < 32 && ssid[i]) { slot.ssid[i] = ssid[i]; i++; } slot.ssid[i] = '\0'; }
    slot.eapolMsg = eapolMsg;
    slot.seqNum   = seqNum;
    slot.rssi     = rssi;
    s_head = next;
}

void IRAM_ATTR WGuard::rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (!s_active) return;
    const wifi_promiscuous_pkt_t* ppkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d   = ppkt->payload;
    uint16_t       len = ppkt->rx_ctrl.sig_len;
    int8_t         rssi = ppkt->rx_ctrl.rssi;
    if (len < 10) return;

    uint8_t ftype   = (d[0] >> 2) & 0x03;
    uint8_t subtype = (d[0] >> 4) & 0x0F;

    if (ftype == 0 && len >= 24) {
        const uint8_t* addr1 = d + 4;
        const uint8_t* addr2 = d + 10;
        const uint8_t* addr3 = d + 16;

        if (subtype == 8) {
            s_isrBeacons++;

            // ── Target BSSID beacon analysis ──────────────────────────────────
            if (memcmp(addr2, (const void*)s_bssid, 6) == 0 && len >= 32) {
                uint64_t bssTs = 0;
                for (uint8_t ti = 0; ti < 8; ti++) bssTs |= (uint64_t)d[24 + ti] << (ti * 8);
                uint32_t nowMs = millis();

                // [1] Backward timestamp jump — two radios on same BSSID can't stay in sync.
                //     Ref: Jana & Kasera, IEEE IPCCC 2010.
                if (s_targetTsSeen && bssTs < s_lastTargetTs) {
                    enqueue(0xFD, addr2, addr1, "", 0, rssi, 0);
                }
                s_lastTargetTs = bssTs;
                s_targetTsSeen = true;

                // [2] Beacon sequence number gap — clone AP runs its own counter; interleaved
                //     beacons create apparent gaps > 1 in the observed stream.
                //     Ref: Park et al., 2017.
                uint16_t seqNum = (uint16_t)(((uint16_t)d[22] | ((uint16_t)d[23] << 8)) >> 4);
                if (s_beaconSeqSeen) {
                    uint16_t expected = (s_lastBeaconSeq + 1) & 0x0FFF;
                    uint16_t gap = (seqNum >= expected)
                                 ? (seqNum - expected)
                                 : (uint16_t)(0x1000u + seqNum - expected);
                    if (gap > 10 && gap < 0x0F00u) {  // >10 gap, ignore near-full wraps
                        enqueue(0xFB, addr2, addr1, "", (uint8_t)(gap > 255 ? 255 : gap), rssi, seqNum);
                    }
                }
                s_lastBeaconSeq = seqNum;
                s_beaconSeqSeen = true;

                // [3] Beacon interval compression — two radios on same BSSID double the observed
                //     beacon rate, halving the inter-arrival time vs the learned baseline.
                //     Ref: Jana & Kasera, IEEE IPCCC 2010.
                if (s_lastBeaconMs > 0) {
                    uint32_t interval = nowMs - s_lastBeaconMs;
                    if (!s_biLearned) {
                        if (interval > 50 && interval < 500) {  // valid beacon interval
                            s_biSum += interval;
                            s_biSamples++;
                            if (s_biSamples == 20) {
                                s_biBaseline = s_biSum / 20;
                                s_biLearned  = true;
                            }
                        }
                    } else if (interval > 10 && interval < s_biBaseline / 2) {
                        enqueue(0xFA, addr2, addr1, "", 0, rssi, seqNum);
                    }
                }
                s_lastBeaconMs = nowMs;

                // [4] Clock skew slope fingerprinting — each radio's crystal oscillator drifts
                //     at a unique rate. After a 20-beacon baseline, deviations >2s from the
                //     predicted BSS timestamp indicate a different physical transmitter.
                //     Ref: Lanze et al., IEEE S&P 2012.
                if (s_csCount < 20) {
                    s_csTs[s_csCount]  = bssTs;
                    s_csArr[s_csCount] = nowMs;
                    s_csCount++;
                    if (s_csCount == 20) {
                        uint64_t dtTs = bssTs - s_csTs[0];
                        uint32_t dtMs = nowMs  - s_csArr[0];
                        if (dtMs > 0) {
                            s_csSlopeNumer   = (uint32_t)(dtTs / dtMs);  // µs per ms (~1000)
                            s_csSlopeLearned = true;
                        }
                    }
                } else if (s_csSlopeLearned) {
                    uint32_t elapsed   = nowMs - s_csArr[0];
                    uint64_t predicted = s_csTs[0] + (uint64_t)s_csSlopeNumer * elapsed;
                    uint64_t deviation = (bssTs > predicted) ? (bssTs - predicted)
                                                              : (predicted - bssTs);
                    if (deviation > 2000000ULL) {   // >2s deviation = different oscillator
                        enqueue(0xF9, addr2, addr1, "", 0, rssi, seqNum);
                        s_csCount        = 0;        // re-baseline after detection
                        s_csSlopeLearned = false;
                    }
                }
            }

            // ── Beacon flood: track unique BSSIDs in 30s window ──────────────
            uint32_t now = millis();
            if (now - s_bssidFloodStart > 30000) {
                s_bssidFloodStart = now;
                s_bssidSeenN      = 0;
                s_bssidFloodFired = false;
            }
            bool seen = false;
            for (uint8_t i = 0; i < s_bssidSeenN; i++) {
                if (memcmp((const void*)s_bssidSeen[i], addr2, 6) == 0) { seen = true; break; }
            }
            if (!seen && s_bssidSeenN < WG_BSSID_MAX) {
                memcpy((void*)s_bssidSeen[s_bssidSeenN++], addr2, 6);
                if (s_bssidSeenN >= 100 && !s_bssidFloodFired) {
                    s_bssidFloodFired = true;
                    enqueue(0xFE, addr2, addr1, "", 0, rssi);
                }
            }
            // Evil Twin: same SSID, different BSSID
            if (memcmp(addr2, (const void*)s_bssid, 6) != 0) {
                char ssid[33] = {};
                extractSSID(d, len, subtype, ssid);
                if (ssid[0] && strcmp(ssid, (const char*)s_ssid) == 0)
                    enqueue(8, addr2, addr1, ssid, 0, rssi);
            }
            return;
        }

        bool toUs    = memcmp(addr1, (const void*)s_bssid, 6) == 0;
        bool fromUs  = memcmp(addr2, (const void*)s_bssid, 6) == 0;
        bool bssidOk = memcmp(addr3, (const void*)s_bssid, 6) == 0;

        if      (subtype == 12 && (toUs || fromUs || bssidOk)) {
            // Extract reason code (bytes 24-25 of deauth body) for DFIR enrichment.
            // Attack tools typically use rc=1 (unspecified) or rc=2 (prev auth invalid).
            // Ref: Al-Mhiqani et al., "Detection of De-authentication DoS attack", 2014.
            uint8_t rc = (len >= 26) ? d[24] : 1;
            enqueue(12, addr2, addr1, "", rc, rssi, 0);
        }
        else if (subtype == 11 && (toUs || bssidOk))           enqueue(11, addr2, addr1, "", 0, rssi);
        else if (subtype ==  0 && (toUs || bssidOk))           enqueue(0,  addr2, addr1, "", 0, rssi);
        else if (subtype ==  4) {
            char ssid[33] = {};
            extractSSID(d, len, subtype, ssid);
            if (ssid[0] == '\0' || strcmp(ssid, (const char*)s_ssid) == 0)
                enqueue(4, addr2, addr1, ssid, 0, rssi);
        }
        else if (subtype == 5) {
            // Probe response from a foreign BSSID — track for Karma detection.
            // Karma APs respond to ANY probe with whatever SSID was requested,
            // so the same BSSID shows up in probe responses for many different SSIDs.
            if (memcmp(addr2, (const void*)s_bssid, 6) != 0) {
                char ssid[33] = {};
                extractSSID(d, len, subtype, ssid);
                if (ssid[0]) enqueue(5, addr2, addr1, ssid, 0, rssi);
            }
        }

    } else if (ftype == 2 && len >= 28) {
        uint8_t tods   = d[1] & 0x01;
        uint8_t fromds = (d[1] >> 1) & 0x01;
        if (tods && fromds) return;
        int hdrLen = (subtype & 0x08) ? 26 : 24;
        if (len < (uint16_t)(hdrLen + 12)) return;

        const uint8_t* bssidPtr = tods ? (d + 4) : (fromds ? (d + 10) : (d + 16));
        if (memcmp(bssidPtr, (const void*)s_bssid, 6) != 0) return;

        const uint8_t* llc = d + hdrLen;
        if (llc[0] != 0xAA || llc[1] != 0xAA || llc[6] != 0x88 || llc[7] != 0x8E) return;
        const uint8_t* eapol = llc + 8;
        int eapolSpace = len - hdrLen - 8;
        if (eapolSpace < 8 || eapol[1] != 0x03) return;
        if (eapol[4] != 0x02 && eapol[4] != 0x01) return;

        uint8_t kiHi = eapol[5], kiLo = eapol[6];
        bool ack    = (kiLo & 0x80) != 0;
        bool mic    = (kiHi & 0x01) != 0;
        bool secure = (kiHi & 0x02) != 0;
        uint8_t msg = 0;
        if      ( ack && !mic)            msg = 1;
        else if (!ack &&  mic && !secure) msg = 2;
        else if ( ack &&  mic)            msg = 3;
        else if (!ack &&  mic &&  secure) msg = 4;
        if (!msg) return;

        const uint8_t* src = tods ? (d + 10) : (fromds ? (d + 16) : (d + 10));
        enqueue(0xFF, src, d + 4, "", msg, rssi);
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

WGuard::WGuard(DisplayManager& dm, WiFiFunctions& wf) : _dm(dm), _wf(wf) {}

// ── Argument parsing ──────────────────────────────────────────────────────────

static bool isNumeric(const char* s) {
    if (!s || !*s) return false;
    for (const char* p = s; *p; p++) if (*p < '0' || *p > '9') return false;
    return true;
}

void WGuard::start(char* args) {
    if (!args || !*args) {
        _dm.println("Usage: wg <index|bssid> [ch]");
        _dm.printCommandScreen();
        return;
    }
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    if (!first) { _dm.println("Usage: wg <index|bssid> [ch]"); _dm.printCommandScreen(); return; }

    uint8_t bssid[6]; int channel = 6; char ssid[33] = {};

    if (strchr(first, ':')) {
        if (sscanf(first, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]) != 6) {
            _dm.println("Bad BSSID."); _dm.printCommandScreen(); return;
        }
        if (second) { int ch = atoi(second); if (ch >= 1 && ch <= 13) channel = ch; }
    } else {
        if (!isNumeric(first)) {
            _dm.println("Usage: wg <index|bssid> [ch]"); _dm.printCommandScreen(); return;
        }
        int idx = atoi(first);
        if (!_wf.isScanDone()) { _dm.println("Run scanwifi first."); _dm.printCommandScreen(); return; }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) {
            _dm.println("Invalid index."); _dm.printCommandScreen(); return;
        }
        _wf.getNetworkSSID(idx, ssid);
    }
    run(bssid, channel, ssid);
}

// ── Notification throttle ─────────────────────────────────────────────────────
// Prevents sound spam when a single attack triggers dozens of events per second.
// Background mode skips this — pollBackground() already coalesces events.

void WGuard::notifyThrottled(NotifLevel level, uint32_t now) {
    if (_bgMode) return;
    if (level >= NOTIF_ALERT) {
        if (now - _lastAlertNotifTs < 5000) return;   // max 1 alert sound per 5 s
        _lastAlertNotifTs = now;
        _lastWarnNotifTs  = now;   // alert also resets warning clock
    } else {
        if (now - _lastWarnNotifTs < 10000) return;   // max 1 warning sound per 10 s
        _lastWarnNotifTs = now;
    }
    NotificationManager::getInstance().notify(level);
}

// ── OUI vendor lookup (DFIR aid — log-only) ───────────────────────────────────
// Returns a short vendor string for known attacker-relevant OUIs, or nullptr if unknown.
// Locally-administered bit (0x02 in first octet) always means a spoofed / virtual MAC.

static const char* lookupOui(const uint8_t* mac) {
    if (mac[0] & 0x02) return "LA-MAC";   // bit 1 set = locally administered = spoofed
    struct OuiEntry { uint8_t p0, p1, p2; const char* name; };
    static const OuiEntry ouis[] = {
        // Attack hardware / common security research tools
        {0x00, 0xC0, 0xCA, "Alfa"},       // Alfa Network (USB attack adapters)
        {0x00, 0x13, 0x37, "Hak5"},       // Hak5 LLC (WiFi Pineapple)
        {0x00, 0xE0, 0x4C, "Realtek"},    // Realtek (cheap USB adapters)
        // Raspberry Pi Foundation (all known OUI blocks)
        {0xB8, 0x27, 0xEB, "RPi"},
        {0xDC, 0xA6, 0x32, "RPi"},
        {0xD8, 0x3A, 0xDD, "RPi"},
        {0xE4, 0x5F, 0x01, "RPi"},
        {0x28, 0xCD, 0xC1, "RPi"},
        {0x2C, 0xCF, 0x67, "RPi"},
        // Espressif Systems (DIY attack tools, ESP32-based firmware)
        {0x18, 0xFE, 0x34, "Espressif"},
        {0x24, 0x0A, 0xC4, "Espressif"},
        {0x24, 0x6F, 0x28, "Espressif"},
        {0x30, 0xAE, 0xA4, "Espressif"},
        {0x40, 0xF5, 0x20, "Espressif"},
        {0x54, 0x5A, 0xA6, "Espressif"},
        {0x58, 0xBF, 0x25, "Espressif"},
        {0x60, 0x01, 0x94, "Espressif"},
        {0x68, 0xC6, 0x3A, "Espressif"},
        {0x78, 0x21, 0x84, "Espressif"},
        {0x84, 0xCC, 0xA8, "Espressif"},
        {0x8C, 0xAA, 0xB5, "Espressif"},
        {0x90, 0x97, 0xD5, "Espressif"},
        {0xA0, 0x20, 0xA6, "Espressif"},
        {0xA4, 0xCF, 0x12, "Espressif"},
        {0xB4, 0xE6, 0x2D, "Espressif"},
        {0xC4, 0x4F, 0x33, "Espressif"},
        {0xCC, 0x50, 0xE3, "Espressif"},
        {0xDC, 0x54, 0x75, "Espressif"},
        {0xE8, 0xDB, 0x84, "Espressif"},
        {0xEC, 0x15, 0xAD, "Espressif"},
        {0xFC, 0xF5, 0xC4, "Espressif"},
        {0x7C, 0x87, 0xCE, "Espressif"},  // ESP32-S3 (T-Deck family)
        {0x80, 0xC5, 0x48, "Espressif"},  // ESP32 (Cardputer / M5Stack)
        {0xAC, 0xD0, 0x74, "Espressif"},
    };
    for (uint8_t i = 0; i < sizeof(ouis)/sizeof(ouis[0]); i++)
        if (mac[0] == ouis[i].p0 && mac[1] == ouis[i].p1 && mac[2] == ouis[i].p2)
            return ouis[i].name;
    return nullptr;
}

// ── Threat detection ──────────────────────────────────────────────────────────

void WGuard::processFrame(const WgFrame& f) {
    uint32_t now = millis();

    switch (f.subtype) {
    case 12: {   // Deauth / Disassoc
        _cntDeauths++;
        if (now - _deauthBurstStart > 30000) { _deauthBurstStart = now; _deauthBurstCount = 0; }
        _deauthBurstCount++;

        bool isBcast = (f.dst[0] == 0xFF && f.dst[1] == 0xFF && f.dst[2] == 0xFF);
        if (isBcast) {
            if (now - _bcastBurstStart > 30000) { _bcastBurstStart = now; _bcastBurstCount = 0; }
            _bcastBurstCount++;
        }

        if (isBcast) {
            // Broadcast deauth — always malicious (legit APs only deauth specific clients).
            // Tracked in _bcastCtr, separate from targeted _deauthCtr so they don't pollute
            // each other's rolling windows.  Rate-limited to once per 30 s per source MAC.
            WgCounter* ctr = findOrAdd(_bcastCtr, _bcastCtrN, f.src);
            if (ctr) {
                uint32_t cnt = rollCount(ctr, 5000, now);
                if (cnt >= 5) {
                    ctr->count = 0; ctr->winStart = now;   // always reset so next 5 accumulate
                    if (now - ctr->lastFired >= 30000) {
                        ctr->lastFired = now;
                        char msg[44];
                        snprintf(msg, sizeof(msg), "BCAST DEAUTH %02X:%02X:%02X:%02X:%02X:%02X",
                                 f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                        char det[48];
                        const char* vb = lookupOui(f.src);
                        if (vb) snprintf(det, sizeof(det), "rc=%u Dth=%lu v=%s", f.eapolMsg, (unsigned long)_cntDeauths, vb);
                        else    snprintf(det, sizeof(det), "rc=%u Dth=%lu",       f.eapolMsg, (unsigned long)_cntDeauths);
                        addEvent(1, msg, f.rssi, det);
                        _threatBcastDeauth++;
                        notifyThrottled(NOTIF_WARNING, now);
                    }
                }
            }
        } else {
            // Targeted deauth storm: 15/5 s = 3/sec — filters normal band steering (1-3 frames).
            // Rate-limited to once per 30 s per source MAC.
            WgCounter* ctr = findOrAdd(_deauthCtr, _deauthCtrN, f.src);
            if (ctr) {
                uint32_t cnt = rollCount(ctr, 5000, now);
                if (cnt >= 15 && now - ctr->lastFired >= 30000) {
                    ctr->lastFired = now;
                    char msg[44];
                    snprintf(msg, sizeof(msg), "DEAUTH storm %02X:%02X:%02X:%02X:%02X:%02X",
                             f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                    char det[48];
                    const char* vt = lookupOui(f.src);
                    if (vt) snprintf(det, sizeof(det), "dst=%02X%02X%02X rc=%u Dth=%lu v=%s",
                                     f.dst[3],f.dst[4],f.dst[5], f.eapolMsg,
                                     (unsigned long)_cntDeauths, vt);
                    else    snprintf(det, sizeof(det), "dst=%02X%02X%02X rc=%u Dth=%lu",
                                     f.dst[3],f.dst[4],f.dst[5], f.eapolMsg,
                                     (unsigned long)_cntDeauths);
                    addEvent(2, msg, f.rssi, det);
                    _threatDeauthStorm++;
                    notifyThrottled(NOTIF_ALERT, now);
                }
            }
        }

        // At deauth threshold 3: upgrade any pending foreign APs to evil twin WARNING.
        // Evil twin tools (Bruce, airbase-ng) start the rogue AP BEFORE deauthing,
        // so the beacon is seen first and lands in _pendingForeign with no deauths yet.
        if (_deauthBurstCount == 3 && _pendingForeignN > 0) {
            for (int i = 0; i < _pendingForeignN; i++) {
                // Require AP to be within 60 s AND signal strong enough to actually lure clients.
                // Legitimate far-away APs (< -82 dBm) are too weak to be an effective evil twin
                // and are almost certainly a pre-existing co-channel AP or extender on a different subnet.
                bool fresh  = (now - _pendingForeignTs[i] < 60000);
                bool strong = (_pendingForeignRssi[i] > -82);
                if (fresh && strong) {
                    char msg[44];
                    snprintf(msg, sizeof(msg), "EVIL TWIN+DTH %02X:%02X:%02X:%02X:%02X:%02X",
                             _pendingForeign[i][0], _pendingForeign[i][1], _pendingForeign[i][2],
                             _pendingForeign[i][3], _pendingForeign[i][4], _pendingForeign[i][5]);
                    char det[48];
                    const char* vp = lookupOui(_pendingForeign[i]);
                    if (vp) snprintf(det, sizeof(det), "Dth=%lu v=%s", (unsigned long)_cntDeauths, vp);
                    else    snprintf(det, sizeof(det), "Dth=%lu",      (unsigned long)_cntDeauths);
                    addEvent(1, msg, _pendingForeignRssi[i], det);  // use AP beacon RSSI, not deauth RSSI
                    _threatEvilTwin++;
                    notifyThrottled(NOTIF_WARNING, now);
                    if (_evilTwinN < 8) {
                        memcpy(_evilTwinSeen[_evilTwinN], _pendingForeign[i], 6);
                        _evilTwinSeenTs[_evilTwinN] = now;
                        _evilTwinN++;
                    }
                }
            }
            _pendingForeignN = 0;
        }
        break;
    }
    case 11: {   // Auth
        _cntAuths++;
        if (now - _authFloodStart > 10000) { _authFloodStart = now; _authMacN = 0; }
        bool seen = false;
        for (int i = 0; i < _authMacN; i++) if (memcmp(_authMacs[i], f.src, 6) == 0) { seen = true; break; }
        if (!seen && _authMacN < 32) memcpy(_authMacs[_authMacN++], f.src, 6);
        if (_authMacN == 32) {
            char msg[44]; snprintf(msg, sizeof(msg), "AUTH flood: 32+ unique MACs/10s");
            char det[48];
            snprintf(det, sizeof(det), "Ath=%lu", (unsigned long)_cntAuths);
            addEvent(1, msg, f.rssi, det);
            _threatAuthFlood++;
            notifyThrottled(NOTIF_WARNING, now);
            _authMacN = 33;  // prevent re-fire until window resets
        }
        break;
    }
    case 4: {    // Probe request
        _cntProbes++;
        if (now - _recentProberWindow > 30000) { _recentProberWindow = now; _recentProberN = 0; }
        bool seen = false;
        for (int i = 0; i < _recentProberN; i++) if (memcmp(_recentProbers[i], f.src, 6) == 0) { seen = true; break; }
        if (!seen && _recentProberN < WG_CTR_MAX) memcpy(_recentProbers[_recentProberN++], f.src, 6);

        WgCounter* ctr = findOrAdd(_probeCtr, _probeCtrN, f.src);
        if (ctr && rollCount(ctr, 5000, now) == 50) {
            char msg[44];
            snprintf(msg, sizeof(msg), "PROBE storm %02X:%02X:%02X:%02X:%02X:%02X 50/5s",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            char det[48];
            const char* vpr = lookupOui(f.src);
            if (vpr) snprintf(det, sizeof(det), "Prb=%lu v=%s", (unsigned long)_cntProbes, vpr);
            else     snprintf(det, sizeof(det), "Prb=%lu",      (unsigned long)_cntProbes);
            addEvent(1, msg, f.rssi, det);
            _threatProbeStorm++;
            notifyThrottled(NOTIF_WARNING, now);
        }
        break;
    }
    case 0: {    // Association request — rapid retry hint for DFIR
        // 5 assoc requests in 5s from the same MAC is abnormal — could be a PMKID capture
        // attempt, aggressive roaming firmware, or a buggy client hammering after a kick.
        // Logged as INFO (silent, no alert) so DFIR analysts can correlate in the CSV.
        // Uses dedicated _assocCtr — isolated from _deauthCtr to prevent cross-contamination
        // (a victim reconnecting after being deauthed should not inflate the deauth storm counter).
        WgCounter* ctr = findOrAdd(_assocCtr, _assocCtrN, f.src);
        if (ctr && rollCount(ctr, 5000, now) == 5) {
            char msg[44];
            snprintf(msg, sizeof(msg), "RAPID ASSOC (PMKID?) %02X:%02X:%02X:%02X:%02X:%02X",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            char det[48];
            const char* va = lookupOui(f.src);
            if (va) snprintf(det, sizeof(det), "dst=%02X:%02X:%02X:%02X:%02X:%02X v=%s",
                             f.dst[0],f.dst[1],f.dst[2],f.dst[3],f.dst[4],f.dst[5], va);
            else    snprintf(det, sizeof(det), "dst=%02X:%02X:%02X:%02X:%02X:%02X",
                             f.dst[0],f.dst[1],f.dst[2],f.dst[3],f.dst[4],f.dst[5]);
            addEvent(0, msg, f.rssi, det);   // INFO — silent, no alert, written to CSV for DFIR
        }
        break;
    }
    case 5: {   // Probe response from foreign BSSID — Karma attack detection
        // Karma APs respond to ANY probe request claiming whatever SSID was asked for.
        // Signature: one BSSID sends probe responses for 3+ different SSIDs in 60 s.
        WgKarmaEntry* ke = nullptr;
        for (int i = 0; i < _karmaN; i++)
            if (memcmp(_karma[i].bssid, f.src, 6) == 0) { ke = &_karma[i]; break; }
        if (!ke && _karmaN < WG_CTR_MAX) {
            ke = &_karma[_karmaN++];
            memcpy(ke->bssid, f.src, 6);
            ke->ssidCount = 0;
            ke->firstSeen = now;
        }
        if (!ke) break;
        if (now - ke->firstSeen > 60000) { ke->ssidCount = 0; ke->firstSeen = now; }
        bool kSeen = false;
        for (uint8_t i = 0; i < ke->ssidCount && i < 4; i++)
            if (strcmp(ke->ssids[i], f.ssid) == 0) { kSeen = true; break; }
        if (!kSeen && ke->ssidCount < 4) {
            strncpy(ke->ssids[ke->ssidCount], f.ssid, 32);
            ke->ssids[ke->ssidCount][32] = '\0';
            ke->ssidCount++;
        }
        if (ke->ssidCount == 3) {
            char msg[44];
            snprintf(msg, sizeof(msg), "KARMA %02X:%02X:%02X:%02X:%02X:%02X 3+SSIDs",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            char det[48] = {};
            const char* vk = lookupOui(f.src);
            if (vk) snprintf(det, sizeof(det), "v=%s", vk);
            addEvent(1, msg, f.rssi, det[0] ? det : nullptr);   // WARNING
            _threatKarma++;
            notifyThrottled(NOTIF_WARNING, now);
            ke->ssidCount = 4;  // mark fired — won't re-trigger at count 3
        }
        break;
    }
    case 8: {   // Foreign AP with same SSID — evil twin check
        // Prune dedup entries whose rogue AP stopped beaconing (>3 s silence = AP gone).
        // Evil twin tools beacon every ~100 ms; 3 s >> 500 ms poll so a running AP is
        // never pruned, but a stopped-then-relaunched one re-enters detection cleanly.
        for (int i = (int)_evilTwinN - 1; i >= 0; i--) {
            if (now - _evilTwinSeenTs[i] > 3000) {
                memcpy(_evilTwinSeen[i], _evilTwinSeen[_evilTwinN - 1], 6);
                _evilTwinSeenTs[i] = _evilTwinSeenTs[_evilTwinN - 1];
                _evilTwinN--;
            }
        }
        // Check dedup — refresh last-seen so a still-running AP never expires
        for (int i = 0; i < _evilTwinN; i++)
            if (memcmp(_evilTwinSeen[i], f.src, 6) == 0) {
                _evilTwinSeenTs[i] = now;   // AP still active — reset expiry clock
                goto evil_done;
            }
        // Skip if already in pending INFO list
        for (int i = 0; i < _pendingForeignN; i++)
            if (memcmp(_pendingForeign[i], f.src, 6) == 0) goto evil_done;
        {
            bool sameOui      = (f.src[0] == _bssid[0] && f.src[1] == _bssid[1] && f.src[2] == _bssid[2]);
            bool laMac        = (f.src[0] & 0x02) != 0;
            bool recentDeauth = (_deauthBurstCount >= 3 && (now - _deauthBurstStart) < 30000);
            char msg[44];
            char det[48];
            const char* v8 = lookupOui(f.src);
            if (sameOui) {
                snprintf(msg, sizeof(msg), "CO-AP %02X:%02X:%02X:%02X:%02X:%02X same OUI",
                         f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                if (v8) snprintf(det, sizeof(det), "v=%s", v8); else det[0] = '\0';
                addEvent(0, msg, f.rssi, det[0] ? det : nullptr);
                if (_evilTwinN < 8) {
                    memcpy(_evilTwinSeen[_evilTwinN], f.src, 6);
                    _evilTwinSeenTs[_evilTwinN] = now;
                    _evilTwinN++;
                }
            } else if (recentDeauth && f.rssi > -82) {
                // Only fire immediately if AP is close enough to be a real threat.
                // Weak far-away APs (< -82 dBm) go to pending instead — avoids false
                // positives from legitimate extenders/routers that appear during an attack.
                snprintf(msg, sizeof(msg), "EVIL TWIN+DTH %02X:%02X:%02X:%02X:%02X:%02X",
                         f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                if (v8) snprintf(det, sizeof(det), "Dth=%lu v=%s", (unsigned long)_cntDeauths, v8);
                else    snprintf(det, sizeof(det), "Dth=%lu",      (unsigned long)_cntDeauths);
                addEvent(1, msg, f.rssi, det);
                _threatEvilTwin++;
                notifyThrottled(NOTIF_WARNING, now);
                if (_evilTwinN < 8) {
                    memcpy(_evilTwinSeen[_evilTwinN], f.src, 6);
                    _evilTwinSeenTs[_evilTwinN] = now;
                    _evilTwinN++;
                }
            } else {
                snprintf(msg, sizeof(msg), laMac
                    ? "FOREIGN AP (LA) %02X:%02X:%02X:%02X:%02X:%02X"
                    : "FOREIGN AP %02X:%02X:%02X:%02X:%02X:%02X",
                    f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
                if (v8) snprintf(det, sizeof(det), "v=%s", v8); else det[0] = '\0';
                addEvent(0, msg, f.rssi, det[0] ? det : nullptr);
                if (_pendingForeignN < 4) {
                    memcpy(_pendingForeign[_pendingForeignN], f.src, 6);
                    _pendingForeignTs[_pendingForeignN]   = now;
                    _pendingForeignRssi[_pendingForeignN] = f.rssi;  // AP's own beacon RSSI
                    _pendingForeignN++;
                }
            }
        }
        evil_done:;
        break;
    }
    case 0xFD: {  // Cloned BSSID — BSS timestamp went backward (two radios, same MAC)
        // OPEN-mode evil twins clone the exact BSSID; their beacon timestamps diverge from
        // the real AP's monotonically-increasing counter, producing a backward jump.
        // _cloneFired bool ensures the very first detection fires instantly (no uptime dependency).
        // After that, 60s cooldown prevents spam while the clone AP keeps running.
        // A stopped-then-relaunched clone re-triggers cleanly after 60s of silence.
        //
        // Severity downgrade: if a co-AP sharing the first 4 MAC bytes is already known,
        // the ts-jump is almost certainly an AP reboot (ISP batch neighbours share 4-byte prefix).
        // A real attacker's adapter has a different OUI entirely — no prefix match → stays CRITICAL.
        if (!_cloneFired || now - _cloneFiredTs >= 60000) {
            _cloneFired   = true;
            _cloneFiredTs = now;
            char msg[44];
            snprintf(msg, sizeof(msg), "BSSID CLONED %02X:%02X:%02X:%02X:%02X:%02X ts-jump",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            // Check for co-AP with same 4-byte prefix — indicates ISP batch neighbour
            bool coApSamePrefix = false;
            for (int i = 0; i < _evilTwinN; i++) {
                if (memcmp(_evilTwinSeen[i], _bssid, 4) == 0) { coApSamePrefix = true; break; }
            }
            char det[48] = {};
            const char* vc = lookupOui(f.src);
            if (coApSamePrefix) {
                snprintf(det, sizeof(det), "reboot? co-AP same prefix%s%s",
                         vc ? " v=" : "", vc ? vc : "");
                addEvent(1, msg, f.rssi, det);   // WARNING — likely reboot, not clone
                // Keep watching: if interval compression or clock skew follows within 60s,
                // upgrade to CRITICAL (a rebooting AP doesn't compress beacon rate).
                _cloneWarnActive = true;
                _cloneWarnTs     = now;
                _cloneWarnRssi   = f.rssi;
                notifyThrottled(NOTIF_WARNING, now);
            } else {
                if (vc) snprintf(det, sizeof(det), "v=%s", vc);
                addEvent(2, msg, f.rssi, det[0] ? det : nullptr);   // CRITICAL — no co-AP alibi
                _cloneWarnActive = false;
                notifyThrottled(NOTIF_ALERT, now);
            }
            _threatClone++;
        }
        break;
    }
    case 0xFB: {   // Beacon sequence number gap
        // Clone AP runs its own 12-bit counter; interleaved beacons create observed gaps >10.
        // Ref: Park et al., "Rogue AP detection mechanism considering sequence number", 2017.
        // Fired as INFO — standalone seq gaps can occur from wireless loss; value is in
        // correlation with 0xFD / 0xFA / 0xF9 for multi-signal CRITICAL upgrade.
        if (now - _seqGapFiredTs >= 60000) {
            _seqGapFiredTs = now;
            char msg[44];
            snprintf(msg, sizeof(msg), "SEQ GAP %02X:%02X:%02X:%02X:%02X:%02X gap=%u",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5], f.eapolMsg);
            char det[48] = {};
            const char* vs = lookupOui(f.src);
            if (vs) snprintf(det, sizeof(det), "seq=%u v=%s", f.seqNum, vs);
            else    snprintf(det, sizeof(det), "seq=%u",       f.seqNum);
            addEvent(0, msg, f.rssi, det[0] ? det : nullptr);   // INFO
            _threatSeqGap++;
        }
        break;
    }
    case 0xFA: {   // Beacon interval compression
        // Two radios beaconing on the same BSSID doubles the observed rate, halving the
        // inter-arrival interval below 50% of the learned baseline.
        // Ref: Jana & Kasera, "An Accurate Fake AP Detection Method", IEEE IPCCC 2010.
        if (!_biCompressFired || now - _biCompressFiredTs >= 30000) {
            _biCompressFired   = true;
            _biCompressFiredTs = now;
            char msg[44];
            snprintf(msg, sizeof(msg), "BCN INTERVAL COMPRESSED %02X:%02X:%02X:%02X:%02X:%02X",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            addEvent(1, msg, f.rssi, nullptr);   // WARNING
            _threatBiCompress++;
            notifyThrottled(NOTIF_WARNING, now);
            // Upgrade: interval compression after a clone WARNING = confirmed different radio
            if (_cloneWarnActive && now - _cloneWarnTs < 60000) {
                _cloneWarnActive = false;
                addEvent(2, "CLONE CONFIRMED (ts-jump + interval compress)", _cloneWarnRssi, nullptr);
                notifyThrottled(NOTIF_ALERT, now);
            }
        }
        break;
    }
    case 0xF9: {   // Clock skew anomaly
        // Each radio's crystal oscillator drifts at a unique µs/ms rate. After a 20-beacon
        // baseline, BSS timestamp deviation >2s indicates either a different physical
        // transmitter (evil twin) OR an AP reboot (timestamps reset to ~0).
        // Because a reboot also triggers 0xFD, we cannot distinguish the two with 0xF9 alone —
        // we do NOT upgrade _cloneWarnActive here. Use 0xFA (beacon interval compression)
        // for high-confidence upgrade, since a rebooting AP cannot double its beacon rate.
        // Ref: Lanze et al., "Clock Skew Based Remote Device Fingerprinting", IEEE S&P 2012.
        if (!_clkSkewFired || now - _clkSkewFiredTs >= 60000) {
            _clkSkewFired   = true;
            _clkSkewFiredTs = now;
            char msg[44];
            snprintf(msg, sizeof(msg), "CLOCK SKEW ANOMALY %02X:%02X:%02X:%02X:%02X:%02X",
                     f.src[0], f.src[1], f.src[2], f.src[3], f.src[4], f.src[5]);
            addEvent(1, msg, f.rssi, "ts-fingerprint mismatch >2s");   // WARNING
            _threatClockSkew++;
            notifyThrottled(NOTIF_WARNING, now);
        }
        break;
    }
    case 0xFE: {   // Beacon flood trigger from ISR
        char det[48];
        snprintf(det, sizeof(det), "Bcn=%lu", (unsigned long)_cntBeacons);
        addEvent(1, "BEACON FLOOD 100+ APs/30s", -127, det);
        _threatBeaconFlood++;
        notifyThrottled(NOTIF_WARNING, now);
        break;
    }
    case 0xFF: { // EAPOL
        _cntEapols++;
        // Require broadcast deauths — real deauth attacks use bcast to kick all clients.
        // Band steering uses targeted unicast only, so _bcastBurstCount stays 0 during normal operation.
        if (_bcastBurstCount >= 3 && (now - _bcastBurstStart) < 30000) {
            // Rate-limit harvest events to once per 30 s — during a continuous deauth+ET
            // attack, M1s arrive every reconnect attempt; logging each one fills the ring fast.
            if (now - _harvestFiredTs >= 30000) {
                _harvestFiredTs = now;
                char msg[44]; snprintf(msg, sizeof(msg), "HANDSHAKE harvest M%u", f.eapolMsg);
                char det[48];
                snprintf(det, sizeof(det), "Dth=%lu EAP=%lu", (unsigned long)_cntDeauths, (unsigned long)_cntEapols);
                addEvent(2, msg, f.rssi, det);
                _threatHandshake++;
                notifyThrottled(NOTIF_ALERT, now);
            }
            _deauthBurstCount = 0;
        }
        break;
    }
    }
}

void WGuard::addEvent(uint8_t sev, const char* msg, int8_t rssi, const char* detail) {
    WgEvent& e = _events[_evHead];
    e.ts   = millis();
    e.sev  = sev;
    e.rssi = rssi;
    strncpy(e.msg, msg, sizeof(e.msg) - 1); e.msg[sizeof(e.msg) - 1] = '\0';
    if (detail && detail[0])
        { strncpy(e.detail, detail, sizeof(e.detail) - 1); e.detail[sizeof(e.detail) - 1] = '\0'; }
    else
        e.detail[0] = '\0';
    _evHead = (_evHead + 1) % WG_EVENT_MAX;
    if (_evCount < WG_EVENT_MAX) _evCount++;
    if (sev > _maxSev) _maxSev = sev;
    _totalEvents++;
    // Ring is full — request flush on next safe opportunity
    if (_evCount >= WG_EVENT_MAX) _autoSaveNeeded = true;
}

// Writes events[skipCount..evCount) to file — only NEW events since last save.
// Timestamps are session-relative (sessionStartMs = millis() at session start).
// detail column is log-only DFIR data (dst MAC, OUI vendor, frame counts) — never shown on screen.
static void saveEventsToFile(const WgEvent* events, uint8_t evHead, uint8_t evCount,
                             uint8_t skipCount, uint32_t sessionStartMs, File& f) {
    for (uint8_t i = skipCount; i < evCount; i++) {
        uint8_t idx = (uint8_t)((evHead - evCount + i + WG_EVENT_MAX) % WG_EVENT_MAX);
        const WgEvent& e = events[idx];
        uint32_t ms  = (e.ts >= sessionStartMs) ? (e.ts - sessionStartMs) : 0;
        uint32_t sec = ms / 1000;
        f.printf("%02u:%02u:%02u,%s,%d,%s,%s\n",
                 (sec / 3600) % 24, (sec / 60) % 60, sec % 60,
                 e.sev == 2 ? "CRITICAL" : (e.sev == 1 ? "WARNING" : "INFO"),
                 (int)e.rssi,
                 e.msg,
                 e.detail);   // empty string if no DFIR detail
    }
    f.flush();
}

// ── Session file management ───────────────────────────────────────────────────

void WGuard::initSession() {
    // Find next available session number — scans SD so we never overwrite across reboots.
    uint16_t sessionNum = 1;
    if (sdCardManager.canAccessSD()) {
        sdCardManager.ensureDir("/logs/wguard");
        char probe[48];
        while (sessionNum < 999) {
            snprintf(probe, sizeof(probe), "/logs/wguard/%03u.csv", sessionNum);
            if (!SD.exists(probe)) break;
            sessionNum++;
        }
    }
    snprintf(_sessionFile, sizeof(_sessionFile), "/logs/wguard/%03u.csv", sessionNum);
    _autoSaveNeeded    = false;
    _totalEvents       = 0;
    _saveCount         = 0;
    _savedEvCount      = 0;
    _sessionStartMs    = millis();
    _lastCheckpointMs  = _sessionStartMs;

    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir("/logs/wguard");
    File f = SD.open(_sessionFile, FILE_WRITE);   // create / truncate
    if (!f) return;
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "# SESSION %u  wguard \"%s\"  bssid=%02X:%02X:%02X:%02X:%02X:%02X  ch%d  uptime=%lus\n",
             sessionNum, _ssid,
             _bssid[0], _bssid[1], _bssid[2], _bssid[3], _bssid[4], _bssid[5],
             _channel, (unsigned long)(_sessionStartMs / 1000));
    f.print(hdr);
    {
        char ts[22] = "";
        ClockManager::instance().getTimestamp(ts, sizeof(ts));
        if (ts[0]) {
            char tshdr[48];
            snprintf(tshdr, sizeof(tshdr), "# started %s (local)\n", ts);
            f.print(tshdr);
        }
    }
    f.print("time,severity,rssi_dbm,message,detail\n");   // CSV column header
    f.close();
}

void WGuard::doAutoSave() {
    _autoSaveNeeded = false;
    if (!sdCardManager.canAccessSD() || _evCount == 0) { _evHead = _evCount = _savedEvCount = 0; return; }
    File f = SD.open(_sessionFile, FILE_APPEND);
    if (f) {
        // Only write events not yet written by a prior checkpoint/manual save
        uint8_t newEvents = _evCount - _savedEvCount;
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
                 "# AUTO-SAVE %u (events %lu-%lu)  Bcn=%lu Prb=%lu Ath=%lu Dth=%lu EAP=%lu\n",
                 ++_saveCount,
                 (unsigned long)(_totalEvents - newEvents + 1),
                 (unsigned long)_totalEvents,
                 (unsigned long)_cntBeacons, (unsigned long)_cntProbes,
                 (unsigned long)_cntAuths,   (unsigned long)_cntDeauths,
                 (unsigned long)_cntEapols);
        f.print(hdr);
        if (newEvents > 0) {
            f.print("time,severity,rssi_dbm,message,detail\n");
            saveEventsToFile(_events, _evHead, _evCount, _savedEvCount, _sessionStartMs, f);
        }
        f.close();
    }
    _evHead = _evCount = _savedEvCount = 0;   // clear ring, keep _maxSev and _totalEvents
}

void WGuard::doCheckpoint() {
    // Time-based checkpoint: append only NEW events (since last save) to file.
    // Ring is NOT cleared — events stay in memory so the display is unchanged.
    _lastCheckpointMs = millis();
    if (!sdCardManager.canAccessSD() || _savedEvCount >= _evCount) return;  // nothing new
    File f = SD.open(_sessionFile, FILE_APPEND);
    if (!f) return;
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "# CHECKPOINT %u  Bcn=%lu Prb=%lu Ath=%lu Dth=%lu EAP=%lu\n",
             ++_saveCount,
             (unsigned long)_cntBeacons, (unsigned long)_cntProbes,
             (unsigned long)_cntAuths,   (unsigned long)_cntDeauths,
             (unsigned long)_cntEapols);
    f.print(hdr);
    f.print("time,severity,rssi_dbm,message,detail\n");
    saveEventsToFile(_events, _evHead, _evCount, _savedEvCount, _sessionStartMs, f);
    _savedEvCount = _evCount;   // mark all current events as written
    f.close();
}

void WGuard::finalizeSession() {
    // Called after promiscuous is off — safe to write SD.
    if (!sdCardManager.canAccessSD()) return;
    File f = SD.open(_sessionFile, FILE_APPEND);
    if (!f) return;
    if (_savedEvCount < _evCount) {
        // There are unsaved events since the last checkpoint/manual save
        char hdr[128];
        snprintf(hdr, sizeof(hdr),
                 "# FINAL SAVE %u  Bcn=%lu Prb=%lu Ath=%lu Dth=%lu EAP=%lu\n",
                 ++_saveCount,
                 (unsigned long)_cntBeacons, (unsigned long)_cntProbes,
                 (unsigned long)_cntAuths,   (unsigned long)_cntDeauths,
                 (unsigned long)_cntEapols);
        f.print(hdr);
        f.print("time,severity,rssi_dbm,message,detail\n");
        saveEventsToFile(_events, _evHead, _evCount, _savedEvCount, _sessionStartMs, f);
    }
    uint32_t durS = (millis() - _sessionStartMs) / 1000;
    f.printf("# SESSION END  total=%lu events  maxSev=%s  ch=%d  duration=%um%02us"
             "  Bcn=%lu Prb=%lu Ath=%lu Dth=%lu EAP=%lu\n",
             (unsigned long)_totalEvents,
             _maxSev == 2 ? "CRITICAL" : (_maxSev == 1 ? "WARNING" : "OK"),
             _channel, durS / 60, durS % 60,
             (unsigned long)_cntBeacons, (unsigned long)_cntProbes,
             (unsigned long)_cntAuths,   (unsigned long)_cntDeauths,
             (unsigned long)_cntEapols);
    f.printf("# THREATS  evil_twin=%u  bcast_deauth=%u  deauth_storm=%u  handshake=%u  auth_flood=%u  probe_storm=%u  karma=%u  clone=%u  beacon_flood=%u  seq_gap=%u  bi_compress=%u  clk_skew=%u\n",
             _threatEvilTwin, _threatBcastDeauth, _threatDeauthStorm, _threatHandshake,
             _threatAuthFlood, _threatProbeStorm,
             _threatKarma,    _threatClone,       _threatBeaconFlood,
             _threatSeqGap,   _threatBiCompress,  _threatClockSkew);
    f.close();
    _evHead = _evCount = _savedEvCount = 0;
}

// ── Counter helpers ───────────────────────────────────────────────────────────

WgCounter* WGuard::findOrAdd(WgCounter* table, uint8_t& cnt, const uint8_t* mac) {
    for (int i = 0; i < cnt; i++) if (memcmp(table[i].mac, mac, 6) == 0) return &table[i];
    if (cnt < WG_CTR_MAX) {
        WgCounter* c = &table[cnt++];
        memcpy(c->mac, mac, 6); c->count = 0; c->winStart = millis(); c->lastFired = 0;
        return c;
    }
    WgCounter* oldest = &table[0];
    for (int i = 1; i < cnt; i++) if (table[i].winStart < oldest->winStart) oldest = &table[i];
    memcpy(oldest->mac, mac, 6); oldest->count = 0; oldest->winStart = millis(); oldest->lastFired = 0;
    return oldest;
}

uint32_t WGuard::rollCount(WgCounter* ctr, uint32_t windowMs, uint32_t now) {
    if (now - ctr->winStart > windowMs) { ctr->count = 0; ctr->winStart = now; }
    return ++ctr->count;
}

// ── Background mode ───────────────────────────────────────────────────────────

void WGuard::beginBackground(char* args) {
    if (_bgMode) stopBackground();

    if (!args || !*args) {
        _dm.println("Usage: wg <index|bssid> [ch] bg");
        _dm.printCommandScreen();
        return;
    }
    char buf[64]; strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    if (!first) { _dm.println("Usage: wg <index|bssid> [ch] bg"); _dm.printCommandScreen(); return; }

    uint8_t bssid[6]; int channel = 6; char ssid[33] = {};
    if (strchr(first, ':')) {
        if (sscanf(first, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &bssid[0], &bssid[1], &bssid[2], &bssid[3], &bssid[4], &bssid[5]) != 6) {
            _dm.println("Bad BSSID."); _dm.printCommandScreen(); return;
        }
        if (second) { int ch = atoi(second); if (ch >= 1 && ch <= 13) channel = ch; }
    } else {
        if (!isNumeric(first)) {
            _dm.println("Usage: wg <index|bssid> [ch] bg"); _dm.printCommandScreen(); return;
        }
        int idx = atoi(first);
        if (!_wf.isScanDone()) { _dm.println("Run scanwifi first."); _dm.printCommandScreen(); return; }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) { _dm.println("Invalid index."); _dm.printCommandScreen(); return; }
        _wf.getNetworkSSID(idx, ssid);
    }

    memcpy(_bssid, bssid, 6);
    strncpy(_ssid, ssid, 32); _ssid[32] = '\0';
    _channel = channel;
    _cntBeacons = _cntProbes = _cntAuths = _cntDeauths = _cntEapols = 0;
    _maxSev = 0; _evHead = _evCount = _savedEvCount = 0; _lastBgHead = 0;
    _deauthCtrN = _probeCtrN = 0;
    _authFloodStart = 0; _authMacN = 0;
    _deauthBurstStart = 0; _deauthBurstCount = 0;
    _bcastBurstStart  = 0; _bcastBurstCount  = 0;
    _cloneWarnActive  = false; _cloneWarnTs = 0; _cloneWarnRssi = -127;
    _seqGapFiredTs    = 0;
    _biCompressFired  = false; _biCompressFiredTs = 0;
    _clkSkewFired     = false; _clkSkewFiredTs    = 0;
    _threatSeqGap     = 0; _threatBiCompress = 0; _threatClockSkew = 0;
    _recentProberN = 0; _recentProberWindow = millis();
    _evilTwinN = 0;
    _pendingForeignN = 0;
    _karmaN = 0;
    _cloneFired = false;
    _threatEvilTwin = _threatBcastDeauth = _threatDeauthStorm = _threatHandshake = _threatAuthFlood = _threatProbeStorm = _threatKarma = _threatClone = _threatBeaconFlood = 0;
    _harvestFiredTs = _lastWarnNotifTs = _lastAlertNotifTs = 0;
    _bcastCtrN = 0;
    _assocCtrN = 0;
    _cloneFiredTs = 0;
    _popupUntil = 0; _lastBgPoll = 0;
    s_head = s_tail = 0;
    s_targetTsSeen  = false;
    s_lastTargetTs  = 0;
    s_beaconSeqSeen = false; s_lastBeaconSeq = 0;
    s_lastBeaconMs  = 0; s_biSum = 0; s_biSamples = 0; s_biBaseline = 0; s_biLearned = false;
    s_csCount       = 0; s_csSlopeLearned = false; s_csSlopeNumer = 0;
    initSession();   // create session file before promiscuous starts
    s_isrBeacons = 0;
    s_bssidSeenN = 0; s_bssidFloodStart = millis(); s_bssidFloodFired = false;
    memcpy((void*)s_bssid, bssid, 6);
    strncpy((char*)s_ssid, ssid, 32); ((char*)s_ssid)[32] = '\0';

    WiFi.disconnect(false);   // drop any existing association before promiscuous
    WiFi.mode(WIFI_STA);
    delay(100);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);
    s_active = true;
    _bgMode  = true;
    _dm.setWGuardState(true, 0);

    _dm.setTextColor(TFT_GREEN);
    _dm.printText("WGUARD bg: ch");
    char chb[4]; snprintf(chb, sizeof(chb), "%d", channel); _dm.printText(chb);
    _dm.printText(" "); _dm.println(ssid[0] ? ssid : "<?>");
    _dm.setTextColor(0x4208); _dm.println("wg stop  to disable");
    _dm.setTextColor(TFT_WHITE);
    _dm.printCommandScreen();
}

void WGuard::stopBackground() {
    if (!_bgMode) return;
    s_active = false;
    _bgMode  = false;
    _popupUntil = 0;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_STA);
    finalizeSession();   // flush remaining events + SESSION END
    _dm.setWGuardState(false, 0);
    _dm.setTextColor(TFT_RED); _dm.println("WGUARD stopped.");
    _dm.setTextColor(TFT_WHITE);
    _dm.printCommandScreen();
}

void WGuard::pollBackground() {
    if (!_bgMode) return;
    uint32_t now = millis();

    // Auto-save in background: ring full — flush, clear, continue
    if (_autoSaveNeeded) {
        s_active = false;
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        esp_wifi_set_promiscuous(false);
        doAutoSave();
        esp_wifi_set_promiscuous_rx_cb(rxCallback);
        esp_wifi_set_promiscuous(true);
        s_active = true;
        _lastCheckpointMs = millis();
        _lastBgHead = 0;   // ring cleared — prevent stale popup on next poll
    }

    // Time-based checkpoint every 2 min (background mode) — only if there are new events
    if (_savedEvCount < _evCount && (now - _lastCheckpointMs) >= 120000) {
        s_active = false;
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        esp_wifi_set_promiscuous(false);
        doCheckpoint();
        esp_wifi_set_promiscuous_rx_cb(rxCallback);
        esp_wifi_set_promiscuous(true);
        s_active = true;
    }

    // Drain ring at most every 200 ms
    if (now - _lastBgPoll >= 200) {
        _lastBgPoll = now;
        while (s_tail != s_head) {
            WgFrame f;
            memcpy(&f, (const void*)&s_ring[s_tail], sizeof(WgFrame));
            s_tail = (s_tail + 1) % WG_RING_SIZE;
            processFrame(f);
        }
        _cntBeacons = s_isrBeacons;

        // New event since last poll? (compare head positions, not capped _evCount)
        if (_evHead != _lastBgHead) {
            _lastBgHead = _evHead;
            // Most recent event
            uint8_t idx = (uint8_t)((_evHead - 1 + WG_EVENT_MAX) % WG_EVENT_MAX);
            const WgEvent& e = _events[idx];

            // Update shield icon colour in status bar
            _dm.setWGuardState(true, _maxSev);

            // Sound — only for sev>0; INFO events (CO-AP etc.) are silent
            if (e.sev > 0)
                NotificationManager::getInstance().notify(e.sev >= 2 ? NOTIF_ALERT : NOTIF_WARNING);

            // Popup bar at bottom of screen — only for sev>0 and screen not locked
            if (e.sev > 0 && !_dm.isBlocked()) {
                uint16_t bgColor = (e.sev >= 2) ? TFT_RED : 0x9400;
                _dm.fillRect(0, 222, 320, 16, bgColor);
                _dm.setCursor(4, 223);
                _dm.setTextColor(TFT_WHITE);
                _dm.printText("WGUARD: ");
                char trunc[30]; strncpy(trunc, e.msg, 29); trunc[29] = '\0';
                _dm.printText(trunc);
                _popupUntil = now + 4000;
            }
        }
    }

    // Expire popup
    if (_popupUntil && now >= _popupUntil) {
        _popupUntil = 0;
        if (!_dm.isBlocked())
            _dm.printCommandScreen();
    }
}

// ── Shared interactive UI loop ────────────────────────────────────────────────
// Called by both run() and enterView(). WiFi/promiscuous already set up by caller.

void WGuard::runUI() {
    // ── Draw static layout ────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setDefaultTextSize();

    // Header — cyberpunk style: [WGUARD::MONITOR]  ch6 . YourNetwork . monitoring
    _dm.setCursor(4, outputY);
    _dm.setTextColor(0x7BEF);     _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);   _dm.printText("WGUARD");
    _dm.setTextColor(0x7BEF);     _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW); _dm.printText("MONITOR");
    _dm.setTextColor(0x7BEF);     _dm.printText("]  ch");
    char chBuf[4]; snprintf(chBuf, sizeof(chBuf), "%d", _channel);
    _dm.setTextColor(TFT_WHITE);  _dm.printText(chBuf);
    _dm.setTextColor(0x4208);     _dm.printText(" . ");
    char ssidShort[14]; strncpy(ssidShort, _ssid[0] ? _ssid : "<?>", 13); ssidShort[13] = '\0';
    _dm.setTextColor(TFT_WHITE);  _dm.printText(ssidShort);
    _dm.setTextColor(0x4208);     _dm.println(" . monitoring");
    _dm.printSeparator();

    int32_t statusY = _dm.getCursorY();
    int32_t statsY  = statusY + LINE_HEIGHT;
    int32_t evSepY  = statsY  + LINE_HEIGHT;   // EVENTS header row
    int32_t evY     = evSepY  + LINE_HEIGHT;   // first event row

    // Footer is redrawn every refresh so save notes can appear/expire

    int      viewOffset  = 0;   // 0 = newest events; positive = scroll into history
    uint32_t lastRefresh = 0;
    bool     forceRedraw = false;
    bool     fullRedraw  = false;   // full header+layout redraw needed (after unlock)
    char     saveNote[48] = {};   // temporary footer message after [s]
    uint32_t saveNoteUntil = 0;  // millis() when note expires

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        // Detect unlock — redraw full static layout so the app is restored
        if (LockScreenManager::getInstance().consumeJustUnlocked())
            fullRedraw = forceRedraw = true;

        if (fullRedraw && !_dm.isBlocked()) {
            fullRedraw = false;
            _dm.clearScreen();
            _dm.setDefaultTextSize();
            _dm.setCursor(4, outputY);
            _dm.setTextColor(0x7BEF);     _dm.printText("[");
            _dm.setTextColor(TFT_CYAN);   _dm.printText("WGUARD");
            _dm.setTextColor(0x7BEF);     _dm.printText("::");
            _dm.setTextColor(TFT_YELLOW); _dm.printText("MONITOR");
            _dm.setTextColor(0x7BEF);     _dm.printText("]  ch");
            char chBuf2[4]; snprintf(chBuf2, sizeof(chBuf2), "%d", _channel);
            _dm.setTextColor(TFT_WHITE);  _dm.printText(chBuf2);
            _dm.setTextColor(0x4208);     _dm.printText(" . ");
            char ssidShort2[14]; strncpy(ssidShort2, _ssid[0] ? _ssid : "<?>", 13); ssidShort2[13] = '\0';
            _dm.setTextColor(TFT_WHITE);  _dm.printText(ssidShort2);
            _dm.setTextColor(0x4208);     _dm.println(" . monitoring");
            _dm.printSeparator();
        }

        if (k == 's' || k == 'S') {
            // Manual save — append only NEW events to session file (GDMA: pause promisc)
            s_active = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            if (!sdCardManager.canAccessSD()) {
                strncpy(saveNote, "No SD card", sizeof(saveNote));
            } else if (_savedEvCount >= _evCount) {
                strncpy(saveNote, "Nothing new to save", sizeof(saveNote));
            } else {
                File f = SD.open(_sessionFile, FILE_APPEND);
                if (f) {
                    uint8_t newEvts = _evCount - _savedEvCount;
                    char hdr[128];
                    snprintf(hdr, sizeof(hdr),
                             "# MANUAL SAVE %u  Bcn=%lu Prb=%lu Ath=%lu Dth=%lu EAP=%lu\n",
                             ++_saveCount,
                             (unsigned long)_cntBeacons, (unsigned long)_cntProbes,
                             (unsigned long)_cntAuths,   (unsigned long)_cntDeauths,
                             (unsigned long)_cntEapols);
                    f.print(hdr);
                    f.print("time,severity,rssi_dbm,message,detail\n");
                    saveEventsToFile(_events, _evHead, _evCount, _savedEvCount, _sessionStartMs, f);
                    _savedEvCount = _evCount;   // mark all current events as written
                    f.close();
                    snprintf(saveNote, sizeof(saveNote), "Saved %u events", newEvts);
                } else {
                    strncpy(saveNote, "SD open failed", sizeof(saveNote));
                }
            }
            saveNoteUntil = millis() + 2500;
            forceRedraw   = true;
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            esp_wifi_set_promiscuous(true);
            s_active = true;
        }

        // Trackball scroll — up = older events, down = newer events
        TrackballEvent tEvt = inputHandler.getTrackballEvent();
        if (tEvt == TBALL_UP) {
            if (viewOffset + 5 < (int)_evCount) { viewOffset += 5; forceRedraw = true; }
        } else if (tEvt == TBALL_DOWN) {
            if (viewOffset > 0) { viewOffset -= 5; if (viewOffset < 0) viewOffset = 0; forceRedraw = true; }
        }

        // Drain frame ring
        while (s_tail != s_head) {
            WgFrame f;
            memcpy(&f, (const void*)&s_ring[s_tail], sizeof(WgFrame));
            s_tail = (s_tail + 1) % WG_RING_SIZE;
            processFrame(f);
        }
        _cntBeacons = s_isrBeacons;

        // Auto-save when ring is full (GDMA: pause promisc)
        if (_autoSaveNeeded) {
            s_active = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            doAutoSave();                   // flushes ring → file, clears ring
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            esp_wifi_set_promiscuous(true);
            s_active = true;
            viewOffset  = 0;               // jump back to newest after clear
            forceRedraw = true;
            _lastCheckpointMs = millis();  // reset checkpoint clock after a full flush
            addEvent(0, "Auto-saved, buffer cleared");
        }

        // Time-based checkpoint every 2 min — append new events to file, keep ring
        uint32_t now2 = millis();
        if (_savedEvCount < _evCount && (now2 - _lastCheckpointMs) >= 120000) {
            s_active = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            doCheckpoint();
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            esp_wifi_set_promiscuous(true);
            s_active = true;
            forceRedraw = true;
        }

        uint32_t now = millis();
        if (forceRedraw || now - lastRefresh >= 500) {
            if (_dm.isBlocked()) { forceRedraw = false; continue; }
            lastRefresh = now;
            forceRedraw = false;

            // Session-elapsed HH:MM:SS for the live clock
            uint32_t upMs  = (now >= _sessionStartMs) ? (now - _sessionStartMs) : 0;
            uint32_t upSec = upMs / 1000;
            char timeBuf[10];
            snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u",
                     (upSec / 3600) % 24, (upSec / 60) % 60, upSec % 60);

            // ── Status line ───────────────────────────────────────────────────
            _dm.fillRect(4, statusY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, statusY);
            _dm.setTextColor(0x7BEF); _dm.printText("STATUS: ");
            if      (_maxSev == 2) { _dm.setTextColor(TFT_RED);    _dm.printText("!! CRITICAL"); }
            else if (_maxSev == 1) { _dm.setTextColor(TFT_YELLOW); _dm.printText("WARNING    "); }
            else                   { _dm.setTextColor(TFT_GREEN);  _dm.printText("OK         "); }
            _dm.setCursor(268, statusY);
            _dm.setTextColor(0x4208); _dm.printText(timeBuf);
            _dm.setTextColor(TFT_WHITE);

            // ── Stats row ─────────────────────────────────────────────────────
            _dm.fillRect(4, statsY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, statsY);
            _dm.setTextColor(0x4208); _dm.printText("Bcn:");
            _dm.setTextColor(TFT_WHITE);
            char n1[6]; snprintf(n1, sizeof(n1), "%-4u", _cntBeacons);  _dm.printText(n1);
            _dm.setTextColor(0x4208); _dm.printText(" Prb:");
            _dm.setTextColor(TFT_WHITE);
            char n2[5]; snprintf(n2, sizeof(n2), "%-3u", _cntProbes);   _dm.printText(n2);
            _dm.setTextColor(0x4208); _dm.printText(" Ath:");
            _dm.setTextColor(TFT_WHITE);
            char n3[5]; snprintf(n3, sizeof(n3), "%-3u", _cntAuths);    _dm.printText(n3);
            _dm.setTextColor(0x4208); _dm.printText(" Dth:");
            _dm.setTextColor(TFT_WHITE);
            char n4[5]; snprintf(n4, sizeof(n4), "%-3u", _cntDeauths);  _dm.printText(n4);
            _dm.setTextColor(0x4208); _dm.printText(" EAP:");
            _dm.setTextColor(TFT_WHITE);
            char n5[5]; snprintf(n5, sizeof(n5), "%-3u", _cntEapols);   _dm.println(n5);

            // ── EVENTS header — shows page position ───────────────────────────
            _dm.fillRect(4, evSepY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, evSepY);
            _dm.setTextColor(0x7BEF);
            if (_evCount > 5) {
                int totalPages = ((int)_evCount + 4) / 5;
                int curPage    = viewOffset / 5 + 1;
                char evHdr[32];
                snprintf(evHdr, sizeof(evHdr), "EVENTS  [%d/%d]", curPage, totalPages);
                _dm.println(evHdr);
            } else {
                _dm.println("EVENTS");
            }

            // ── Events (5 lines, with scroll offset) ──────────────────────────
            _dm.fillRect(4, evY, 312, LINE_HEIGHT * 5, TFT_BLACK);
            int show = (int)_evCount - viewOffset;
            if (show > 5) show = 5;
            for (int i = 0; i < show; i++) {
                int slot = viewOffset + i;   // 0 = most recent
                if (slot >= (int)_evCount) break;
                uint8_t idx = (uint8_t)((_evHead - 1 - slot + WG_EVENT_MAX * 2) % WG_EVENT_MAX);
                const WgEvent& e = _events[idx];
                _dm.setCursor(4, evY + i * LINE_HEIGHT);
                uint32_t ms = (e.ts >= _sessionStartMs) ? (e.ts - _sessionStartMs) : 0;
                uint32_t es = ms / 1000;
                char tbuf[10];
                snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u",
                         (es / 3600) % 24, (es / 60) % 60, es % 60);
                _dm.setTextColor(0x4208); _dm.printText(tbuf); _dm.printText(" ");
                if      (e.sev == 2) { _dm.setTextColor(TFT_RED);    _dm.printText("!! "); }
                else if (e.sev == 1) { _dm.setTextColor(TFT_YELLOW); _dm.printText("W  "); }
                else                  { _dm.setTextColor(0x7BEF);    _dm.printText("i  "); }
                _dm.setTextColor(TFT_WHITE);
                char trunc[40]; strncpy(trunc, e.msg, 39); trunc[39] = '\0';
                _dm.println(trunc);
            }

            // ── Footer — show save note briefly, then revert to key hint ─────
            int32_t footerY = evY + LINE_HEIGHT * 5;
            _dm.fillRect(4, footerY, 312, LINE_HEIGHT, TFT_BLACK);
            _dm.setCursor(4, footerY);
            if (now < saveNoteUntil && saveNote[0]) {
                _dm.setTextColor(TFT_GREEN);
                _dm.println(saveNote);
            } else {
                _dm.setTextColor(0x4208);
                _dm.println("[q]quit  [s]save  [^v]scroll");
            }
            _dm.setTextColor(TFT_WHITE);
        }
    }
}

// ── Enter live view of running bg session ─────────────────────────────────────
// No WiFi setup/teardown — bg promiscuous stays running throughout.

void WGuard::enterView() {
    _bgMode = false;   // disable so processFrame notifies normally during live view
    runUI();
    _bgMode = true;    // restore bg mode
    _lastBgHead = _evHead;  // don't re-alert events we just saw
    _dm.setWGuardState(true, _maxSev);
    _dm.clearScreen();
    _dm.printFirstCommandScreen("");
}

// ── Fresh interactive session ─────────────────────────────────────────────────

void WGuard::run(const uint8_t* bssid, int channel, const char* ssid) {
    if (_bgMode) stopBackground();   // clean slate — stop bg if it was running

    memcpy(_bssid, bssid, 6);
    strncpy(_ssid, ssid, 32); _ssid[32] = '\0';
    _channel = channel;
    _cntBeacons = _cntProbes = _cntAuths = _cntDeauths = _cntEapols = 0;
    _maxSev = 0; _evHead = _evCount = _savedEvCount = 0;
    _deauthCtrN = _probeCtrN = 0;
    _authFloodStart = 0; _authMacN = 0;
    _deauthBurstStart = 0; _deauthBurstCount = 0;
    _bcastBurstStart  = 0; _bcastBurstCount  = 0;
    _cloneWarnActive  = false; _cloneWarnTs = 0; _cloneWarnRssi = -127;
    _seqGapFiredTs    = 0;
    _biCompressFired  = false; _biCompressFiredTs = 0;
    _clkSkewFired     = false; _clkSkewFiredTs    = 0;
    _threatSeqGap     = 0; _threatBiCompress = 0; _threatClockSkew = 0;
    _recentProberN = 0; _recentProberWindow = millis();
    _evilTwinN = 0;
    _pendingForeignN = 0;
    _karmaN = 0;
    _cloneFired = false;
    _threatEvilTwin = _threatBcastDeauth = _threatDeauthStorm = _threatHandshake = _threatAuthFlood = _threatProbeStorm = _threatKarma = _threatClone = _threatBeaconFlood = 0;
    _harvestFiredTs = _lastWarnNotifTs = _lastAlertNotifTs = 0;
    _bcastCtrN = 0;
    _assocCtrN = 0;
    _cloneFiredTs = 0;
    _bgMode = false;   // explicitly — interactive mode, not bg
    s_head = s_tail = 0;
    s_targetTsSeen  = false;
    s_lastTargetTs  = 0;
    s_beaconSeqSeen = false; s_lastBeaconSeq = 0;
    s_lastBeaconMs  = 0; s_biSum = 0; s_biSamples = 0; s_biBaseline = 0; s_biLearned = false;
    s_csCount       = 0; s_csSlopeLearned = false; s_csSlopeNumer = 0;
    s_isrBeacons = 0;
    s_bssidSeenN = 0; s_bssidFloodStart = millis(); s_bssidFloodFired = false;
    memcpy((void*)s_bssid, bssid, 6);
    strncpy((char*)s_ssid, ssid, 32); ((char*)s_ssid)[32] = '\0';

    initSession();   // create /logs/wguard_NNN.csv — SD safe before promiscuous

    WiFi.disconnect(false);   // drop any existing association before promiscuous
    WiFi.mode(WIFI_STA);
    delay(100);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);
    s_active = true;
    _dm.setWGuardState(true, 0);

    runUI();   // blocking

    // ── Cleanup ───────────────────────────────────────────────────────────────
    s_active = false;
    _bgMode  = false;
    _dm.setWGuardState(false, 0);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_STA);
    finalizeSession();   // write remaining events + SESSION END — promiscuous off, SD safe
    _dm.clearScreen();
    _dm.printFirstCommandScreen("");
}
