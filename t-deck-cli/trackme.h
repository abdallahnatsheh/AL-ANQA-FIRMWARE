#ifndef TRACKME_H
#define TRACKME_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"

#define TM_TIER1_MAX     20
#define TM_TIER2_MAX    100
#define TM_RSSI_HISTORY   8
#define TM_SIG_MAX       40
#define TM_PROBE_RING    16
#define SD_LOG_TRACKME   "/logs/trackme.txt"

enum ThreatLevel : uint8_t {
    THREAT_NONE    = 0,
    THREAT_NOTICE  = 1,
    THREAT_WARNING = 2,
    THREAT_ALERT   = 3
};

struct TrackerSig {
    char        name[24];
    uint16_t    companyId;
    uint8_t     payloadByte; // required mfr[2] value; 0x00 = match any payload
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
    bool        alertFired;          // SD log entry written for this device
    bool        isWiFi;              // detected via probe request (not BLE)
    uint8_t     crowdAtArrival;      // tier1Count when device first appeared
#ifdef BOARD_TDECK_PLUS
    float       dispMeters;          // GPS displacement while device tracked
#endif
};

class TrackMeScanner {
public:
    TrackMeScanner(DisplayManager& dm, SDCardManager& sd);
    void start();

private:
    DisplayManager& dm;
    SDCardManager&  sd;

    TrackerSig  sigs[TM_SIG_MAX];
    int         sigCount;

    TrackedDev  tier1[TM_TIER1_MAX];
    int         tier1Count;
    TrackedDev  tier2[TM_TIER2_MAX];
    int         tier2Count;

    KState      k1[TM_TIER1_MAX];
    KState      k2[TM_TIER2_MAX];

    int      page;
    uint32_t startMs;

    // -- radio --
    void doBLEScan(int seconds);
    void doWiFiSniff(uint32_t durationMs);

    // -- device pool --
    void processDevice(const uint8_t* mac, const char* name,
                       uint16_t companyId, uint8_t mfrType, int8_t rssi, bool isWiFi);
    void initDev(TrackedDev& d, const uint8_t* mac, const char* name,
                 uint16_t companyId, int8_t rssi, bool isWiFi, uint8_t tier,
                 uint8_t mfrType = 0x00, uint8_t crowd = 0);
    void promoteTier2(uint32_t cycleStart);
    void markGaps(uint32_t cycleStart);

    // -- analysis --
    void        runScoring();
    void        runGate2(TrackedDev& d);
    bool        runGate3(const TrackedDev& d);
    int         matchSig(uint16_t companyId, uint8_t mfrType = 0x00);
    float       kalmanUpdate(KState& k, float z);
    float       calcVariance(const TrackedDev& d);

    // -- display --
    void drawScreen(ThreatLevel highestLevel, const char* alertName, uint32_t alertSec);
    void drawHeader();

    // -- audio --
    void beep(ThreatLevel lvl);
    void startI2S();
    void stopI2S();
    void playTone(int freq, int durationMs);
    bool _i2sReady;

    // -- SD --
    void loadSignatures();
    void saveLog();
    void appendLog(const TrackedDev& d);

    // -- helpers --
    String macStr(const uint8_t* m);
    bool   macEq(const uint8_t* a, const uint8_t* b);

    // WiFi promiscuous ring (probe request sniff)
    struct ProbeEntry { uint8_t mac[6]; int8_t rssi; };
    static volatile ProbeEntry _ring[TM_PROBE_RING];
    static volatile uint8_t    _rHead;
    static volatile uint8_t    _rTail;
    static void IRAM_ATTR wifiCb(void* buf, wifi_promiscuous_pkt_type_t type);

#ifdef BOARD_TDECK_PLUS
    void  readGPS();
    float gpsLat;
    float gpsLon;
    bool  gpsValid;
    HardwareSerial* gpsSerial;
    char  gpsBuf[96];
    uint8_t gpsBufLen;
#endif
};

#endif // TRACKME_H
