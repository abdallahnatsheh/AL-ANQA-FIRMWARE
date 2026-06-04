#ifndef WIFIMON_FUNCTIONS_H
#define WIFIMON_FUNCTIONS_H

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include <SD.h>

#define WM_MAX_NETS      24
#define WM_MAX_CLIENTS   32
#define WM_PKT_RING      32
#define WM_CLIENT_TTL    90000   // drop unassoc client after 90 s of silence

// PCAP ring — raw frames buffered in RAM, flushed to SD with promiscuous paused
#define WM_PCAP_RING     64     // 64 × 262 bytes ≈ 17 KB — fits in DRAM
#define WM_PCAP_SNAPLEN  256    // max raw bytes captured per frame
#define WM_PROBE_DEDUP   64     // unique MAC+SSID pairs deduplicated in RAM
#define WM_PROBE_BUF     16     // pending probe entries before SD flush

enum WmView { VIEW_NETS, VIEW_CLIENTS };

struct WmNet {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    uint32_t beacons;
};

struct WmClient {
    uint8_t     mac[6];
    uint8_t     apBssid[6];    // all-zero = unassociated (probe-only)
    bool        associated;
    int8_t      rssi;
    uint32_t    lastSeen;
    const char* vendor;
    const char* type;
};

struct WmPkt {
    uint8_t type;
    uint8_t subtype;
    uint8_t dsFlags;
    int8_t  rssi;
    uint8_t channel;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    char    ssid[33];
};

struct WmRawFrame {
    uint32_t tsMs;
    uint16_t len;
    uint8_t  data[WM_PCAP_SNAPLEN];
};

struct WmProbeEntry {
    uint8_t  mac[6];
    char     ssid[33];
    int8_t   rssi;
    uint32_t tsMs;
};

struct WmProbeDedup {
    uint8_t mac[6];
    char    ssid[33];
};

class WiFiMonitor {
public:
    WiFiMonitor(DisplayManager& displayManager, SDCardManager& sdCardManager);
    void start(int fixedChannel = 0);

    static void IRAM_ATTR extractSSID(const uint8_t* data, uint16_t len,
                                      uint8_t subtype, char* ssidOut);

private:
    DisplayManager& _dm;
    SDCardManager&  _sd;

    // packet counters
    uint32_t _totalPkts;
    uint32_t _beaconPkts;
    uint32_t _probePkts;
    uint32_t _dataPkts;
    uint32_t _mgmtPkts;
    uint32_t _ctrlPkts;
    uint32_t _deauthPkts;

    // tracked networks and clients
    WmNet    _nets[WM_MAX_NETS];
    int      _netCount;
    WmClient _clients[WM_MAX_CLIENTS];
    int      _clientCount;

    // UI
    WmView   _view;
    int      _page;
    int      _selClient;
    int      _currentChannel;
    bool     _hopping;
    char     _statusMsg[52];
    uint32_t _statusExpiry;

    // PCAP capture
    File     _pcapFile;
    bool     _pcapOpen;
    uint32_t _pcapFrames;
    char     _pcapPath[44];
    uint32_t _lastPcapFlush;

    // probe log
    File         _probeFile;
    bool         _probeOpen;
    uint32_t     _probeCount;
    uint32_t     _lastProbeFlush;
    char         _probePath[40];
    WmProbeDedup _dedup[WM_PROBE_DEDUP];
    int          _dedupCount;
    int          _dedupHead;
    WmProbeEntry _probePending[WM_PROBE_BUF];
    int          _probePendCount;

    void resetAll();
    void processRing();
    void handlePacket(const WmPkt& p);
    void trackNet(const WmPkt& p);
    void trackClient(const uint8_t* mac, const uint8_t* apBssid,
                     bool assoc, int8_t rssi);
    void expireClients(uint32_t now);
    int  findNet(const uint8_t* bssid) const;
    int  findClient(const uint8_t* mac) const;
    int  countClientsForNet(const uint8_t* bssid) const;
    void setChannel(int ch);
    void drawNets();
    void drawClients();
    void deauthClient(int absIdx);
    void openPcap();
    void flushPcap();
    void closePcap();
    void openProbeLog();
    void flushProbeLog();
    void closeProbeLog();
    void logProbe(const uint8_t* mac, const char* ssid, int8_t rssi, uint32_t tsMs);
    void setStatus(const char* msg, uint32_t ms = 2500);

    static String macStr(const uint8_t* mac);

    static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static void parseFrame(const uint8_t* data, uint16_t len,
                           uint8_t ch, int8_t rssi, WmPkt& out);

    // parsed-frame ring (for display/tracking)
    static volatile WmPkt    s_ring[WM_PKT_RING];
    static volatile uint8_t  s_head;
    static volatile uint8_t  s_tail;

    // raw-frame ring (for PCAP)
    static volatile WmRawFrame s_pcapRing[WM_PCAP_RING];
    static volatile uint8_t    s_pcapHead;
    static volatile uint8_t    s_pcapTail;
    static volatile bool       s_pcapActive;  // gated false during SD flush
    static volatile uint32_t   s_pcapDropped; // frames dropped due to ring full
};

#endif // WIFIMON_FUNCTIONS_H
