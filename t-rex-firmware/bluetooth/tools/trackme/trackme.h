#ifndef TRACKME_H
#define TRACKME_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#ifdef BOARD_TDECK_PLUS
#include <TinyGPS++.h>
#endif

#define TM_TIER1_MAX     20
#define TM_TIER2_MAX    100
#define TM_RSSI_HISTORY   8
#define TM_SIG_MAX       40
#define TM_PROBE_RING    16
#define TM_BLE_RING      24

enum ThreatLevel : uint8_t {
    THREAT_NONE    = 0,
    THREAT_NOTICE  = 1,
    THREAT_WARNING = 2,
    THREAT_ALERT   = 3
};

struct TrackerSig {
    char        name[24];
    uint16_t    companyId;
    uint8_t     payloadByte;
    uint8_t     minMfrLen;   // minimum mfr data length required; 0 = no check
    ThreatLevel level;
};

struct KState {
    float x;   // RSSI estimate
    float P;   // error covariance
};

struct TrackedDev {
    uint8_t     mac[6];
    char        name[28];
    uint16_t    companyId;
    uint32_t    firstSeen;           // millis
    uint32_t    lastSeen;            // millis
    uint16_t    sightings;
    float       rssiSmoothed;        // Kalman output
    int8_t      rssiHistory[TM_RSSI_HISTORY];
    uint8_t     rssiIdx;
    uint8_t     rssiCount;
    int         score;
    ThreatLevel alertLevel;
    bool        isKnown;             // matched a signature
    int8_t      sigIdx;              // index into sigs[], -1 = unknown
    uint8_t     tier;
    uint8_t     distinctWindows;     // gap-and-return events
    uint32_t    gapStart;            // millis when device was last lost
    bool        gapActive;           // device is currently absent
    bool        gapReturned;         // has ever disappeared then returned
    bool        alertFired;
    bool        isAppleDevice;       // true = matched THREAT_NONE Apple entry, never scored/alerted
    bool        isWiFi;              // detected via probe request (not BLE)
    bool        isCompanion;         // seen during baseline or on SD whitelist — never scored
    uint8_t     crowdAtArrival;      // tier1Count when device first appeared
    float       followDistM;         // metres the user moved while this device was in range (GPS)
    uint32_t    lastSightingMs;       // millis of last ~1Hz-gated scoring update
};

class TmBleScanCb;

class TrackMeScanner {
    friend class TmBleScanCb;
public:
    TrackMeScanner(DisplayManager& dm, SDCardManager& sd);
    void start(bool silent = false);

private:
    DisplayManager& dm;
    SDCardManager&  sd;

    TrackerSig  sigs[TM_SIG_MAX];   // small (40×29B = ~1.2KB) — stays in DRAM for fast lookup
    int         sigCount;

    TrackedDev* tier1;              // ps_malloc'd — ~1.8KB in PSRAM
    int         tier1Count;
    TrackedDev* tier2;              // ps_malloc'd — ~8.8KB in PSRAM
    int         tier2Count;

    KState*     k1;                 // ps_malloc'd — ~160B in PSRAM
    KState*     k2;                 // ps_malloc'd — ~800B in PSRAM

    int      page;
    uint32_t startMs;

    // -- UI state --
    enum TmView { TM_VIEW_TIER1, TM_VIEW_TIER2 };
    enum TmSort { TM_SORT_NONE, TM_SORT_SCORE, TM_SORT_RSSI };
    TmView   viewMode;
    int      page2;
    TmSort   sortMode;
    bool     filterAlerts;
    char     _pendingKey;

    // GPS movement tracking (always present; stays 0 when no GPS available)
    float    _totalDistM;   // cumulative distance moved this session
    bool     _gpsMoving;    // true once _totalDistM >= 50 m

    // Companion / whitelist
    bool     _baselineDone;              // true after first 60 s of session
    uint8_t  _knownMAC[20][6];           // permanent whitelist loaded from SD
    char     _knownLabel[20][24];
    int      _knownCount;
    bool     matchKnown(const uint8_t* mac);
    void     loadWhitelist();
    bool     addToWhitelist(const uint8_t* mac, const char* label);

    void ensureBuffers();   // lazy PSRAM alloc — call at start of start(), not in ctor

    // -- radio --
    void drainBleRing();
    void doWiFiSniff(uint32_t durationMs);

    // -- device pool --
    void processDevice(const uint8_t* mac, const char* name,
                       uint16_t companyId, uint8_t mfrType, int8_t rssi, bool isWiFi,
                       uint8_t mfrDataLen = 0);
    void initDev(TrackedDev& d, const uint8_t* mac, const char* name,
                 uint16_t companyId, int8_t rssi, bool isWiFi, uint8_t tier,
                 uint8_t mfrType = 0x00, uint8_t mfrDataLen = 0, uint8_t crowd = 0);
    void markGaps(uint32_t cycleStart);

    // -- analysis --
    void        runScoring();
    void        runGate2(TrackedDev& d);
    bool        runGate3(const TrackedDev& d);
    int         matchSig(uint16_t companyId, uint8_t mfrType, uint8_t mfrDataLen);
    float       kalmanUpdate(KState& k, float z);
    float       calcVariance(const TrackedDev& d);

    // -- display --
    void drawScreen(ThreatLevel highestLevel, const char* alertName, uint32_t alertSec);
    void drawHeader();
    void drawHelp();

    bool _silent;

    // -- SD --
    void     loadSignatures();
    void     saveLog();
    void     appendLog(const TrackedDev& d);
    void     drawSdNotice(const char* msg);
    uint32_t _sdNoticeMs;

    // Transient feedback for view/sort/filter toggles, shown in the alert bar
    uint32_t _uiNoticeMs;
    char     _uiNoticeText[40];
    uint16_t _uiNoticeColor;

    // -- helpers --
    String macStr(const uint8_t* m);
    bool   macEq(const uint8_t* a, const uint8_t* b);

    // WiFi promiscuous ring (probe request sniff)
    struct ProbeEntry { uint8_t mac[6]; int8_t rssi; };
    static volatile ProbeEntry _ring[TM_PROBE_RING];
    static volatile uint8_t    _rHead;
    static volatile uint8_t    _rTail;
    static void IRAM_ATTR wifiCb(void* buf, wifi_promiscuous_pkt_type_t type);

    // BLE continuous-scan ring (filled by NimBLEScanCallbacks::onResult)
    struct TmBleEntry {
        uint8_t  mac[6];
        char     name[20];
        uint16_t companyId;
        uint8_t  mfrType;
        uint8_t  mfrLen;
        int8_t   rssi;
    };
    static volatile TmBleEntry _bleRing[TM_BLE_RING];
    static volatile uint8_t    _bleHead;
    static volatile uint8_t    _bleTail;

#ifdef BOARD_TDECK_PLUS
    float gpsDistance(float lat1, float lon1, float lat2, float lon2);
    float       gpsLat;
    float       gpsLon;
    bool        gpsValid;
    HardwareSerial* gpsSerial;
    TinyGPSPlus gps;
#endif
};

#endif // TRACKME_H
