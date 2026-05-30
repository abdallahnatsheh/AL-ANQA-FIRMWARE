// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef WGUARD_H
#define WGUARD_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "wifi_functions.h"
#include "notification_manager.h"

#define WG_RING_SIZE    32
#define WG_EVENT_MAX   128
#define WG_CTR_MAX      16
#define WG_BSSID_MAX   128   // ISR beacon flood tracking — needs 128 slots to reach the 100-AP trigger

struct WgFrame {
    uint8_t  subtype;    // 0xFF=EAPOL, 0xFE=bcn flood, 0xFD=ts-jump clone,
                         // 0xFB=seq gap, 0xFA=bcn interval compress, 0xF9=clock skew,
                         // 8=foreign same-SSID, 12=deauth, 11=auth, 4=probe, 5=probe-resp, 0=assoc
    uint8_t  src[6];
    uint8_t  dst[6];
    char     ssid[33];
    uint8_t  eapolMsg;   // EAPOL msg (1-4); deauth reason code for subtype 12; gap size for 0xFB
    uint16_t seqNum;     // beacon seq number (target-BSSID beacons only, for gap detection)
    int8_t   rssi;
};

struct WgEvent {
    uint32_t ts;
    uint8_t  sev;       // 0=info 1=warning 2=critical
    int8_t   rssi;      // RSSI of triggering frame; -127 = N/A
    char     msg[44];
    char     detail[48];   // log-only DFIR: dst MAC, OUI vendor, frame counts — never shown on screen
};

struct WgCounter {
    uint8_t  mac[6];
    uint32_t count;
    uint32_t winStart;
    uint32_t lastFired;   // millis() when this MAC last fired an event — cooldown anchor
};

struct WgKarmaEntry {
    uint8_t  bssid[6];
    char     ssids[4][33];   // up to 4 unique SSIDs seen from this BSSID
    uint8_t  ssidCount;
    uint32_t firstSeen;
};

class WGuard {
public:
    WGuard(DisplayManager& dm, WiFiFunctions& wf);
    void start(char* args);
    void beginBackground(char* args);
    void pollBackground();
    void stopBackground();
    void enterView();
    bool isBackground() const { return _bgMode; }

private:
    DisplayManager& _dm;
    WiFiFunctions&  _wf;

    uint8_t  _bssid[6];
    char     _ssid[33];
    int      _channel;

    uint32_t _cntBeacons;
    uint32_t _cntProbes;
    uint32_t _cntAuths;
    uint32_t _cntDeauths;
    uint32_t _cntEapols;

    uint8_t  _maxSev;

    WgEvent  _events[WG_EVENT_MAX];
    uint8_t  _evHead;
    uint8_t  _evCount;

    WgCounter _deauthCtr[WG_CTR_MAX];   // targeted deauth per source MAC
    uint8_t   _deauthCtrN;
    WgCounter _bcastCtr[WG_CTR_MAX];    // broadcast deauth per source MAC (separate counter)
    uint8_t   _bcastCtrN;
    WgCounter _probeCtr[WG_CTR_MAX];
    uint8_t   _probeCtrN;
    WgCounter _assocCtr[WG_CTR_MAX];    // association requests — separate from deauth to prevent cross-contamination
    uint8_t   _assocCtrN;

    uint32_t _authFloodStart;
    uint8_t  _authMacs[32][6];
    uint8_t  _authMacN;

    uint32_t _deauthBurstStart;
    uint32_t _deauthBurstCount;
    uint32_t _bcastBurstStart;   // rolling window for broadcast-only deauths
    uint32_t _bcastBurstCount;   // used by HANDSHAKE harvest — bcast deauths only

    uint8_t  _recentProbers[WG_CTR_MAX][6];
    uint8_t  _recentProberN;
    uint32_t _recentProberWindow;

    uint8_t      _evilTwinSeen[8][6];  // BSSIDs that fired WARNING
    uint32_t     _evilTwinSeenTs[8];  // millis() of last beacon from that rogue BSSID
    uint8_t      _evilTwinN;
    uint8_t      _pendingForeign[4][6]; // foreign BSSIDs seen before deauths — pending upgrade
    uint32_t     _pendingForeignTs[4];
    int8_t       _pendingForeignRssi[4]; // beacon RSSI of each pending AP
    uint8_t      _pendingForeignN;

    WgKarmaEntry _karma[WG_CTR_MAX];
    uint8_t      _karmaN;
    bool         _cloneFired;    // true after first detection — enables cooldown path
    uint32_t     _cloneFiredTs;  // millis() of last clone detection — 60s cooldown

    char         _sessionFile[48];   // e.g. /logs/wguard/003.csv
    bool         _autoSaveNeeded;    // set by addEvent when ring is full
    uint32_t     _totalEvents;       // total events since session start (never reset)
    uint8_t      _saveCount;         // auto + manual + checkpoint saves done this session
    uint8_t      _savedEvCount;      // events already flushed to file — skip on next save
    uint32_t     _sessionStartMs;    // millis() at session start for duration calc
    uint32_t     _lastCheckpointMs;  // millis() of last time-based checkpoint
    uint32_t     _harvestFiredTs;    // millis() of last HANDSHAKE harvest event fired
    uint32_t     _lastWarnNotifTs;   // millis() of last WARNING notification (throttle)
    uint32_t     _lastAlertNotifTs;  // millis() of last ALERT notification (throttle)

    uint8_t      _threatEvilTwin;    // count of EVIL TWIN events fired
    uint8_t      _threatBcastDeauth; // count of BCAST DEAUTH events fired
    uint8_t      _threatDeauthStorm; // count of targeted DEAUTH STORM events fired
    uint8_t      _threatHandshake;   // count of HANDSHAKE harvest events fired
    uint8_t      _threatAuthFlood;   // count of AUTH FLOOD events fired
    uint8_t      _threatProbeStorm;  // count of PROBE STORM events fired
    uint8_t      _threatKarma;       // count of KARMA events fired
    uint8_t      _threatClone;       // count of BSSID CLONED events fired
    uint8_t      _threatBeaconFlood; // count of BEACON FLOOD events fired
    uint8_t      _threatSeqGap;      // count of SEQ NUMBER GAP events fired
    uint8_t      _threatBiCompress;  // count of BEACON INTERVAL COMPRESSED events fired
    uint8_t      _threatClockSkew;   // count of CLOCK SKEW ANOMALY events fired

    // ── Multi-signal clone upgrade state ─────────────────────────────────
    bool         _cloneWarnActive  = false;  // 0xFD fired WARNING — watching for upgrade signal
    uint32_t     _cloneWarnTs      = 0;
    int8_t       _cloneWarnRssi    = -127;

    // ── New detection cooldowns ────────────────────────────────────────────
    uint32_t     _seqGapFiredTs    = 0;      // millis() of last SEQ GAP event (60s cooldown)
    bool         _biCompressFired  = false;
    uint32_t     _biCompressFiredTs = 0;     // millis() of last INTERVAL COMPRESSED (30s cooldown)
    bool         _clkSkewFired     = false;
    uint32_t     _clkSkewFiredTs   = 0;      // millis() of last CLOCK SKEW event (60s cooldown)

    void initSession();              // create session file, write header
    void doAutoSave();               // flush ring → file, clear ring (ring-full path)
    void doCheckpoint();             // append ring → file, keep ring intact (time-based path)
    void finalizeSession();          // write remaining + SESSION END (call after promisc off)

    bool     _bgMode       = false;
    uint32_t _popupUntil   = 0;
    uint32_t _lastBgPoll   = 0;
    uint8_t  _lastBgHead   = 0;   // tracks _evHead, not _evCount — avoids cap-at-8 deaf bug

    void run(const uint8_t* bssid, int channel, const char* ssid);
    void runUI();          // blocking UI loop — shared by run() and enterView()
    void processFrame(const WgFrame& f);
    void addEvent(uint8_t sev, const char* msg, int8_t rssi = -127, const char* detail = nullptr);
    void notifyThrottled(NotifLevel level, uint32_t now);   // rate-limited notification
    WgCounter* findOrAdd(WgCounter* table, uint8_t& cnt, const uint8_t* mac);
    uint32_t   rollCount(WgCounter* ctr, uint32_t windowMs, uint32_t now);

    static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static void IRAM_ATTR extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype, char* out);
    static void IRAM_ATTR enqueue(uint8_t sub, const uint8_t* src, const uint8_t* dst,
                                  const char* ssid, uint8_t eapolMsg, int8_t rssi,
                                  uint16_t seqNum = 0);

    static volatile WgFrame  s_ring[WG_RING_SIZE];
    static volatile uint8_t  s_head;
    static volatile uint8_t  s_tail;
    static volatile uint8_t  s_bssid[6];
    static volatile char     s_ssid[33];
    static volatile bool     s_active;
    static volatile uint32_t s_isrBeacons;
    static volatile uint8_t  s_bssidSeen[WG_BSSID_MAX][6];
    static volatile uint8_t  s_bssidSeenN;
    static volatile uint32_t s_bssidFloodStart;
    static volatile bool     s_bssidFloodFired;
    static volatile uint64_t s_lastTargetTs;      // BSS timestamp from last target beacon
    static volatile bool     s_targetTsSeen;

    // ── Beacon sequence number gap (ISR) ──────────────────────────────────
    static volatile uint16_t s_lastBeaconSeq;
    static volatile bool     s_beaconSeqSeen;

    // ── Beacon interval compression (ISR) ────────────────────────────────
    static volatile uint32_t s_lastBeaconMs;
    static volatile uint32_t s_biSum;
    static volatile uint8_t  s_biSamples;    // 0–20 = learning; 20 = baseline locked
    static volatile uint32_t s_biBaseline;   // learned mean inter-beacon ms (~102)
    static volatile bool     s_biLearned;

    // ── Clock skew slope fingerprinting (ISR collects, one-time slope computation) ──
    static volatile uint64_t s_csTs[20];     // BSS timestamps of target-BSSID beacons
    static volatile uint32_t s_csArr[20];    // millis() at receipt
    static volatile uint8_t  s_csCount;      // filled slots; ≥20 = slope computed
    static volatile uint32_t s_csSlopeNumer; // µs per ms, should be ~1000 for real AP
    static volatile bool     s_csSlopeLearned;
};

#endif // WGUARD_H
