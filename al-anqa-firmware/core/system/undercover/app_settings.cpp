// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "app_settings.h"
#include "wifi_creds.h"          // getWifiNetwork / appendWpaNetwork (shared with sw/cw)
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

void SettingsApp::onEnter() {
    _page = MENU; _sub = MAIN; _msgMs = 0; _sel = 0; _scroll = 0;
    if (WiFi.status() == WL_CONNECTED) _on = true;
}
void SettingsApp::msg(const char* m, uint16_t c) {
    strncpy(_msgText, m, sizeof(_msgText) - 1); _msgText[sizeof(_msgText) - 1] = 0;
    _msgCol = c; _msgMs = millis();
}
bool SettingsApp::savedPassword(const char* ssid, String& out) {
    Preferences prefs; prefs.begin("wifi", true);
    out = prefs.getString(ssid, ""); prefs.end();
    if (out.length()) return true;
    WifiNetwork saved = getWifiNetwork(String(ssid));
    if (!saved.ssid.isEmpty() && !saved.isHashed && !saved.open) { out = saved.psk; return true; }
    return false;
}
void SettingsApp::saveCreds(const char* ssid, const char* pass, bool open, const uint8_t* bssid) {
    if (!open) { Preferences prefs; prefs.begin("wifi", false); prefs.putString(ssid, pass); prefs.end(); }
    WifiNetwork n; n.ssid = String(ssid); n.psk = String(pass); n.open = open; n.hidden = false;
    if (bssid) {
        char b[18]; snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
                             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        n.bssid = String(b);
    }
    appendWpaNetwork(n);
}
void SettingsApp::wfToggle() {
    // GDMA rule: never WIFI_OFF. "Off" just disconnects and stays idle in STA mode.
    if (_on) { WiFi.disconnect(false); _on = false; _nets.clear(); msg("Wi-Fi off", _ui.muted); }
    else     { WiFi.mode(WIFI_STA); _on = true; msg("Wi-Fi on", _ui.col(0x34A853)); }
}
void SettingsApp::wfScan() {
    if (!_on) { msg("Turn Wi-Fi on first", _ui.col(0xEA4335)); return; }
    msg("Scanning...", _ui.muted); draw(); _ui.present();
    int n = WiFi.scanNetworks(false, true);
    _nets.clear();
    for (int i = 0; i < n && (int)_nets.size() < 40; i++) {
        WNet w; strncpy(w.ssid, WiFi.SSID(i).c_str(), sizeof(w.ssid) - 1); w.ssid[sizeof(w.ssid) - 1] = 0;
        w.rssi = WiFi.RSSI(i);
        w.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        uint8_t* b = WiFi.BSSID(i); if (b) memcpy(w.bssid, b, 6);
        if (w.ssid[0]) _nets.push_back(w);
    }
    WiFi.scanDelete();
    _sel = 0; _scroll = 0;
    msg(_nets.empty() ? "No networks found" : "", _ui.muted);
}
void SettingsApp::wfConnect(const char* pass) {
    _sub = MAIN;
    msg("Connecting...", _ui.muted); draw(); _ui.present();
    WiFi.disconnect(false); delay(100); WiFi.mode(WIFI_STA);
    WiFi.begin(_tgtSsid, _tgtOpen ? nullptr : pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(300);
    if (WiFi.status() == WL_CONNECTED) {
        saveCreds(_tgtSsid, _tgtOpen ? "" : pass, _tgtOpen, _tgtBssid);
        msg("Connected", _ui.col(0x34A853));
    } else {
        msg("Connection failed", _ui.col(0xEA4335));
    }
}
void SettingsApp::wfStartConnect(int idx) {
    if (idx < 0 || idx >= (int)_nets.size()) return;
    WNet& w = _nets[idx];
    strncpy(_tgtSsid, w.ssid, sizeof(_tgtSsid)); _tgtSsid[sizeof(_tgtSsid) - 1] = 0;
    _tgtOpen = w.open; memcpy(_tgtBssid, w.bssid, 6);
    if (w.open) { wfConnect(""); return; }
    String saved;
    if (savedPassword(w.ssid, saved)) { wfConnect(saved.c_str()); return; }
    _pass[0] = 0; _passLen = 0; _sub = PASSWORD;
}
void SettingsApp::drawMenu() {
    auto* G = _ui.g();
    _ui.appBar("Settings");
    int ry = UI_CONTENT_Y + 12, rh = 46;
    G->fillSmoothRoundRect(10, ry, SCREEN_WIDTH - 20, rh, 10, _ui.bar);
    _ui.wifiGlyph(30, ry + rh / 2 - 2, _ui.ink);
    G->setFont(_ui.fTitle()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_left);
    G->drawString("Wi-Fi", 50, ry + 16);
    char sub[48];
    if (!_on)                                strcpy(sub, "Off");
    else if (WiFi.status() == WL_CONNECTED)  snprintf(sub, sizeof(sub), "%s", WiFi.SSID().c_str());
    else                                     strcpy(sub, "Not connected");
    G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
    G->setClipRect(50, ry + rh - 22, SCREEN_WIDTH - 50 - 66, 18);
    G->drawString(sub, 50, ry + 32);
    G->clearClipRect();
    _ui.toggle(SCREEN_WIDTH - 60, ry + rh / 2 - 9, 42, 18, _on);
    G->setTextDatum(textdatum_t::top_left);
}
void SettingsApp::drawWifiPage() {
    auto* G = _ui.g();
    _ui.appBar("Wi-Fi");
    int WFY0 = UI_CONTENT_Y + 6, WFSTAT = UI_CONTENT_Y + 46, WFLIST = UI_CONTENT_Y + 70, WFROWH = 24;
    G->fillSmoothRoundRect(10, WFY0, SCREEN_WIDTH - 20, 32, 8, _ui.bar);
    G->setFont(_ui.fTitle()); G->setTextColor(_ui.ink);
    G->setTextDatum(textdatum_t::middle_left);
    G->drawString("Wi-Fi", 22, WFY0 + 16);
    _ui.toggle(SCREEN_WIDTH - 58, WFY0 + 7, 42, 18, _on);
    G->setTextDatum(textdatum_t::top_left);
    int scw = 62, scx = SCREEN_WIDTH - 10 - scw;
    const char* status; char sbuf[64]; uint16_t scol;
    if (_msgMs && millis() - _msgMs < 4000 && _msgText[0]) { status = _msgText; scol = _msgCol; }
    else if (!_on) { status = "Wi-Fi is off"; scol = _ui.muted; }
    else if (WiFi.status() == WL_CONNECTED) {
        snprintf(sbuf, sizeof(sbuf), "%s  %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        status = sbuf; scol = _ui.col(0x34A853);
    } else { status = "Not connected"; scol = _ui.muted; }
    G->setFont(_ui.fMeta()); G->setTextColor(scol);
    G->setTextDatum(textdatum_t::middle_left);
    G->setClipRect(12, WFSTAT - 8, scx - 16, 18);
    G->drawString(status, 12, WFSTAT);
    G->clearClipRect();
    G->fillSmoothRoundRect(scx, WFSTAT - 10, scw, 20, 8, _ui.col(0x1DA1F2));
    G->setTextColor(_ui.bg); G->setTextDatum(textdatum_t::middle_center);
    G->drawString("Scan", scx + scw / 2, WFSTAT);
    G->setTextDatum(textdatum_t::top_left);
    int n = (int)_nets.size(), vis = (SCREEN_HEIGHT - WFLIST) / WFROWH;
    for (int i = 0; i < vis && _scroll + i < n; i++) {
        int idx = _scroll + i, ry = WFLIST + i * WFROWH;
        WNet& w = _nets[idx];
        if (idx == _sel) G->fillSmoothRoundRect(8, ry, SCREEN_WIDTH - 16, WFROWH - 2, 6, _ui.bar);
        G->setFont(_ui.fBody()); G->setTextColor(_ui.ink);
        G->setTextDatum(textdatum_t::middle_left);
        G->setClipRect(16, ry, SCREEN_WIDTH - 16 - 76, WFROWH);
        G->drawString(w.ssid, 18, ry + WFROWH / 2);
        G->clearClipRect();
        _ui.signalBars(SCREEN_WIDTH - 54, ry + WFROWH / 2 - 3, w.rssi);
        if (!w.open) _ui.lockGlyph(SCREEN_WIDTH - 26, ry + WFROWH / 2 - 2);
    }
    G->setTextDatum(textdatum_t::top_left);
}
void SettingsApp::drawPassword() {
    auto* G = _ui.g();
    _ui.appBar("Wi-Fi password");
    G->setFont(_ui.fMeta()); G->setTextColor(_ui.muted);
    G->setTextDatum(textdatum_t::top_left);
    G->drawString(_tgtSsid, 20, UI_CONTENT_Y + 8);
    int fy = UI_CONTENT_Y + 30;
    G->fillSmoothRoundRect(20, fy, SCREEN_WIDTH - 40, 28, 8, _ui.bar);
    int dotX = 32;
    for (int i = 0; i < _passLen; i++) { G->fillSmoothCircle(dotX, fy + 14, 3, _ui.ink); dotX += 11; }
    if (dotX < SCREEN_WIDTH - 30) G->fillRect(dotX - 2, fy + 6, 2, 16, _ui.muted);
    _ui.twoButtons("Connect", "Cancel", _ui.col(0x34A853));
}
void SettingsApp::draw() {
    if (_page == MENU)        drawMenu();
    else if (_sub == PASSWORD) drawPassword();
    else                       drawWifiPage();
}
Nav SettingsApp::onTouch(const TouchEvent& te) {
    if (te.type != TouchEvent::TAP) return Nav::Stay;
    if (_page == MENU) {
        if (Ui::hitAppBack(te.x, te.y)) return Nav::Back;
        int ry = UI_CONTENT_Y + 12, rh = 46;
        if (te.y >= ry && te.y <= ry + rh) {
            if (te.x >= SCREEN_WIDTH - 66) wfToggle();
            else { _page = WIFI; _sub = MAIN; }
        }
        return Nav::Stay;
    }
    if (_sub == PASSWORD) {
        if (Ui::hitAppBack(te.x, te.y)) { _sub = MAIN; return Nav::Stay; }
        if (Ui::hitBtnA(te.x, te.y)) { wfConnect(_pass); return Nav::Stay; }
        if (Ui::hitBtnB(te.x, te.y)) { _sub = MAIN; return Nav::Stay; }
        return Nav::Stay;
    }
    // Wi-Fi page
    if (Ui::hitAppBack(te.x, te.y)) { _page = MENU; return Nav::Stay; }
    int WFY0 = UI_CONTENT_Y + 6, WFSTAT = UI_CONTENT_Y + 46, WFLIST = UI_CONTENT_Y + 70, WFROWH = 24;
    if (te.y >= WFY0 && te.y <= WFY0 + 32 && te.x >= SCREEN_WIDTH - 72) { wfToggle(); return Nav::Stay; }
    int scw = 62, scx = SCREEN_WIDTH - 10 - scw;
    if (te.x >= scx && te.x <= scx + scw && te.y >= WFSTAT - 10 && te.y <= WFSTAT + 10) { wfScan(); return Nav::Stay; }
    int n = (int)_nets.size(), vis = (SCREEN_HEIGHT - WFLIST) / WFROWH;
    for (int i = 0; i < vis && _scroll + i < n; i++) {
        int ry = WFLIST + i * WFROWH;
        if (te.y >= ry && te.y <= ry + WFROWH - 2) { _sel = _scroll + i; wfStartConnect(_sel); return Nav::Stay; }
    }
    return Nav::Stay;
}
Nav SettingsApp::onTrackball(TrackballEvent tb) {
    if (_page == MENU) {
        if (tb == TBALL_CLICK) { _page = WIFI; _sub = MAIN; }
        else if (tb == TBALL_LEFT || tb == TBALL_RIGHT) wfToggle();
        return Nav::Stay;
    }
    if (_sub == PASSWORD) { if (tb == TBALL_CLICK) wfConnect(_pass); return Nav::Stay; }
    int WFLIST = UI_CONTENT_Y + 70, WFROWH = 24;
    int n = (int)_nets.size(), vis = (SCREEN_HEIGHT - WFLIST) / WFROWH;
    if (tb == TBALL_UP && _sel > 0)            { _sel--; if (_sel < _scroll) _scroll--; }
    else if (tb == TBALL_DOWN && _sel < n - 1) { _sel++; if (_sel >= _scroll + vis) _scroll++; }
    else if (tb == TBALL_LEFT)                 wfToggle();
    else if (tb == TBALL_CLICK && n > 0)       wfStartConnect(_sel);
    return Nav::Stay;
}
Nav SettingsApp::onKey(char k) {
    if (_page == WIFI && _sub == PASSWORD) {
        if (k == '\r' || k == '\n') { wfConnect(_pass); return Nav::Stay; }
        if (k == '\x08' || k == '\x7F') { if (_passLen > 0) _pass[--_passLen] = 0; return Nav::Stay; }
        if (k >= 0x20 && k < 0x7F) {
            if (_passLen < (int)sizeof(_pass) - 1) { _pass[_passLen++] = k; _pass[_passLen] = 0; }
        }
        return Nav::Stay;
    }
    if (_page == WIFI && (k == 'q' || k == 'Q')) { _page = MENU; return Nav::Stay; }
    if (k == 'q' || k == 'Q') return Nav::Back;
    return Nav::Stay;
}
