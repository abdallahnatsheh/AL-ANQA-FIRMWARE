// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// rogue_handshake — fully manual rogue-AP WPA2 half-handshake engine.
//
// Unlike the IDF softAP (which hides the ANonce it generates), we ARE the AP and
// pick the ANonce ourselves, then inject our own M1. So we never need to capture
// M1 over the air (impossible anyway — ESP32 promiscuous can't hear self-TX).
//
// In STA + promiscuous we inject the whole AP side by hand:
//   beacon (RSN) → probe-resp → auth-resp → assoc-resp → EAPOL M1 (our ANonce)
// and sniff only the client's M2 (SNonce + MIC). With the known ANonce + M2 the
// half-handshake is crackable offline exactly like a real 4-way capture.
//
// CAVEAT: esp_wifi_80211_tx does NOT hardware-ACK unicast frames, so association
// success depends on the client tolerating missing ACKs. The live stage counters
// (probes/auths/assocs/m1Sent/gotM2) show how far each client gets. See
// .claude/memory project_karma_rogue_handshake.

#ifndef ROGUE_HANDSHAKE_H
#define ROGUE_HANDSHAKE_H

#include <Arduino.h>

namespace roguehs {

struct State {
    char    ssid[33];
    uint8_t channel;
    uint8_t apMac[6];          // our BSSID = the interface's actual MAC (ACK match)
    bool    macRandomized;     // true if the LA-MAC randomize took; false = factory MAC
    uint8_t anonce[32];        // we generate it → no need to capture M1

    // live stage diagnostics (updated by poll/cb)
    volatile uint32_t probes;  // directed probe reqs for our SSID
    volatile uint32_t auths;   // open-auth reqs to our BSSID
    volatile uint32_t assocs;  // (re)assoc reqs to our BSSID
    volatile uint32_t m1Sent;  // M1 frames injected
    volatile uint32_t m2Seen;  // M2 frames sniffed

    // crack material — valid once gotM2 is true
    volatile bool gotM2;
    uint8_t  staMac[6];
    uint8_t  snonce[32];
    uint8_t  mic[16];
    uint8_t  eapol[256];       // M2 EAPOL frame, MIC field zeroed
    uint16_t eapolLen;

    // raw 802.11 frames for an aircrack/hashcat-ready .cap export:
    // beacon (carries the ESSID/BSSID) + the M1 we injected + the M2 we sniffed.
    uint8_t  beacon[160];  uint16_t beaconLen;
    uint8_t  m1Raw[160];   uint16_t m1RawLen;
    uint8_t  m2Raw[256];   uint16_t m2RawLen;
    uint32_t capTs;        // millis() at M2 capture (pcap timestamp base)
};

// Bring up the manual AP for `ssid` on `channel`. Generates a random BSSID + a
// fresh ANonce. Returns false if WiFi setup fails. Call end() to tear down.
bool begin(const char* ssid, uint8_t channel);

// Re-arm an already-running engine for a NEW target without restarting WiFi: keeps
// the BSSID/promiscuous/cb, resets counters + state, picks a fresh ANonce, and sets
// the channel. Lets a sweep switch SSIDs without per-target esp_wifi_stop/start churn.
void retarget(const char* ssid, uint8_t channel);

// Service the responder: re-beacon, drain the client-event ring, inject
// probe/auth/assoc responses + M1. Call frequently from the owner's loop.
void poll();

const State& state();

// Reactive karma: while baiting one SSID, the cb also notes probe requests for OTHER
// SSIDs. nextProbeHint() returns the most recent such SSID (and clears it), so the
// auto loop can retarget to whatever a device is searching for right now. false = none.
bool nextProbeHint(char* out, size_t n);

// Stop promiscuous + injection, return WiFi to idle STA (SD-safe afterwards).
void end();

}  // namespace roguehs

#endif  // ROGUE_HANDSHAKE_H
