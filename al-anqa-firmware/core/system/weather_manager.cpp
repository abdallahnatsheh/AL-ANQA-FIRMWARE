// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// WeatherManager — see weather_manager.h. Source: Open-Meteo (free, keyless).

#include "weather_manager.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#ifdef BOARD_TDECK_PLUS
#include "gps_manager.h"
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <math.h>

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;

static const uint32_t WX_REFRESH_MS = 15UL * 60 * 1000;   // 15 min between fetches

// Short label from a WMO weather-interpretation code (open-meteo.com/en/docs).
static const char* wmoLabel(int code) {
    if (code == 0)                       return "Clear";
    if (code <= 2)                       return "Partly cloudy";
    if (code == 3)                       return "Cloudy";
    if (code == 45 || code == 48)        return "Fog";
    if (code >= 51 && code <= 57)        return "Drizzle";
    if (code >= 61 && code <= 67)        return "Rain";
    if (code >= 71 && code <= 77)        return "Snow";
    if (code >= 80 && code <= 82)        return "Showers";
    if (code == 85 || code == 86)        return "Snow";
    if (code >= 95)                      return "Thunder";
    return "";
}

WeatherManager& WeatherManager::instance() {
    static WeatherManager inst;
    return inst;
}

void WeatherManager::init() {
    if (_inited) return;
    _inited = true;
    if (!sdCardManager.canAccessSD()) return;
    if (SD.exists("/config/weather.conf")) { loadConfig(); return; }
    // First boot with no config — drop a self-documenting template so the user can
    // just edit it on the card (no command needed), mirroring the other seeded confs.
    // Open-Meteo is keyless, so there is nothing secret to store — only location.
    SD.mkdir("/config");
    File f = SD.open("/config/weather.conf", FILE_WRITE);
    if (f) {
        f.println("# weather.conf — Al-Anqa weather (Open-Meteo, no API key), shown in the `home` launcher");
        f.println("# On the T-Deck Plus with a GPS fix, location is automatic.");
        f.println("# Otherwise set your coordinates below (or run: wx loc <lat> <lon>), then reboot.");
        f.println("# lat=31.9539");
        f.println("# lon=35.9106");
        f.println("units=metric   # or: imperial");
        f.close();
    }
}

bool WeatherManager::stale() const {
    return !_valid || (millis() - _lastFetchMs) > WX_REFRESH_MS;
}

bool WeatherManager::configured() const {
    // Always usable: a location comes from GPS (Plus), a manual `wx loc`, or
    // automatic IP geolocation over WiFi. The actual fetch is still gated on
    // WiFi being connected, so this just means "weather is available to try".
    return true;
}

// ── Config (/config/weather.conf, key=value — mirrors ClockManager) ──────────
bool WeatherManager::loadConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/config/weather.conf", FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        int hash = line.indexOf('#');            // strip inline comments
        if (hash >= 0) line = line.substring(0, hash);
        line.trim();
        if (!line.length()) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String k = line.substring(0, eq); k.trim();
        String v = line.substring(eq + 1); v.trim();
        if (k == "lat")        { _cfgLat = v.toFloat(); _haveCfgLoc = true; }
        else if (k == "lon")   { _cfgLon = v.toFloat(); }
        else if (k == "units") { _imperial = (v == "imperial" || v == "f" || v == "F"); }
    }
    f.close();
    return true;
}

bool WeatherManager::saveConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    SD.mkdir("/config");
    File f = SD.open("/config/weather.conf", FILE_WRITE);   // truncates on ESP32
    if (!f) return false;
    f.println("# weather.conf — Al-Anqa weather (Open-Meteo, no API key)");
    if (_haveCfgLoc) { f.printf("lat=%.5f\n", _cfgLat); f.printf("lon=%.5f\n", _cfgLon); }
    f.printf("units=%s\n", _imperial ? "imperial" : "metric");
    f.close();
    return true;
}

// ── Location: best available source ──────────────────────────────────────────
// GPS fix (most accurate) > manual lat/lon (explicit) > IP geolocation (auto over
// WiFi, coarse). So `wx now` "just works": on the Plus it uses GPS the moment
// there's a fix; otherwise it auto-locates from the network — `wx loc` is only an
// optional manual override, never required.
bool WeatherManager::locate(float& lat, float& lon) {
#ifdef BOARD_TDECK_PLUS
    GpsManager& gm = GpsManager::instance();
    if (gm.isValid()) { lat = gm.lat(); lon = gm.lon(); return true; }
#endif
    if (_haveCfgLoc) { lat = _cfgLat; lon = _cfgLon; return true; }
    return locateByIp(lat, lon);
}

// Coarse (city-level) location from the public IP via ip-api.com — free, no key,
// plain HTTP. Cached after the first success (your IP location only changes when
// you change networks). Note: this reveals the device's public IP to ip-api.com;
// a GPS fix or a manual `wx loc` avoids the call entirely.
bool WeatherManager::locateByIp(float& lat, float& lon) {
    if (_haveIpLoc) { lat = _ipLat; lon = _ipLon; return true; }   // cached
    if (WiFi.status() != WL_CONNECTED) return false;
    WiFiClient client;                                             // ip-api free tier is HTTP
    HTTPClient http;
    bool ok = false;
    if (http.begin(client, "http://ip-api.com/json/?fields=status,lat,lon")) {
        http.setConnectTimeout(4000);
        http.setTimeout(4000);
        if (http.GET() == 200) {
            String p = http.getString();
            JsonDocument d;
            if (!deserializeJson(d, p) && (d["lat"].is<float>() || d["lat"].is<int>())) {
                _ipLat = (float)d["lat"]; _ipLon = (float)d["lon"]; _haveIpLoc = true;
                lat = _ipLat; lon = _ipLon; ok = true;
            }
        }
        http.end();
    }
    return ok;
}

// ── Fetch (blocking, HTTPS) ───────────────────────────────────────────────────
bool WeatherManager::fetch() {
    if (_fetching) return false;                     // no reentrancy
    if (WiFi.status() != WL_CONNECTED) return false;
    float lat, lon;
    if (!locate(lat, lon)) return false;

    _fetching = true;
    char url[256];
    // Plain HTTP, NOT HTTPS: weather needs no confidentiality, and under the
    // undercover home launcher (large PSRAM sprite + VLW fonts resident) the TLS
    // handshake's ~30-40KB internal-DRAM allocation fails, so WiFiClientSecure
    // returned "fetch failed" there while `wx now` from the CLI worked. Open-Meteo
    // serves the identical response over HTTP (keyless), so drop TLS entirely.
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code&temperature_unit=%s",
             lat, lon, _imperial ? "fahrenheit" : "celsius");

    WiFiClient client;                               // plain HTTP — no TLS memory cost
    HTTPClient http;
    bool ok = false;
    if (http.begin(client, url)) {
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        int rc = http.GET();
        if (rc == 200) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                JsonVariant cur = doc["current"];
                if (cur["temperature_2m"].is<float>() || cur["temperature_2m"].is<int>()) {
                    _temp = (int)lroundf((float)cur["temperature_2m"]);
                    _code = cur["weather_code"] | 0;
                    strncpy(_cond, wmoLabel(_code), sizeof(_cond) - 1);
                    _cond[sizeof(_cond) - 1] = '\0';
                    _valid = true;
                    _lastFetchMs = millis();
                    ok = true;
                }
            }
        }
        http.end();
    }
    _fetching = false;
    return ok;
}

bool WeatherManager::forceFetch() { return fetch(); }

void WeatherManager::setLoc(float lat, float lon) {
    _cfgLat = lat; _cfgLon = lon; _haveCfgLoc = true;
    saveConfig();
}
void WeatherManager::setUnits(bool imperial) {
    _imperial = imperial;
    _valid = false;               // force a refetch in the new units
    saveConfig();
}

// ── Command output ────────────────────────────────────────────────────────────
void WeatherManager::printStatus() {
    displayManager.setTextColor(TFT_CYAN);
    displayManager.newLinePrintLn("Weather (Open-Meteo, no key)");

    displayManager.setTextColor(0x7BEF);
    displayManager.printText("Location: ");
    displayManager.setTextColor(TFT_WHITE);
    {
        char b[48];
#ifdef BOARD_TDECK_PLUS
        if (GpsManager::instance().isValid()) { displayManager.println("GPS fix (most accurate)"); }
        else
#endif
        if (_haveCfgLoc)      { snprintf(b, sizeof(b), "manual %.3f,%.3f", _cfgLat, _cfgLon); displayManager.println(b); }
        else if (_haveIpLoc)  { snprintf(b, sizeof(b), "IP ~%.2f,%.2f (auto)", _ipLat, _ipLon); displayManager.println(b); }
        else                    displayManager.println("auto (GPS/WiFi-IP when online)");
    }

    displayManager.setTextColor(0x7BEF);
    displayManager.printText("Units: ");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println(_imperial ? "imperial (F)" : "metric (C)");

    displayManager.setTextColor(0x7BEF);
    displayManager.printText("Reading: ");
    displayManager.setTextColor(TFT_WHITE);
    if (_valid) {
        char b[48];
        snprintf(b, sizeof(b), "%d%c %s  (%us ago)",
                 _temp, _imperial ? 'F' : 'C', _cond, (unsigned)((millis() - _lastFetchMs) / 1000));
        displayManager.println(b);
    } else {
        displayManager.println("none yet");
    }
    displayManager.printCommandScreen();
}

// ── `weather`/`wx` handler ────────────────────────────────────────────────────
void runWeatherCmd(char* args) {
    WeatherManager& wx = WeatherManager::instance();
    if (!args || !*args) { wx.printStatus(); return; }

    if (strncmp(args, "loc", 3) == 0) {
        float la = 0, lo = 0;
        if (sscanf(args + 3, "%f %f", &la, &lo) == 2) {
            wx.setLoc(la, lo);
            displayManager.newLinePrintLn("Location saved");
        } else {
            displayManager.newLinePrintLn("Usage: wx loc <lat> <lon>");
        }
        displayManager.printCommandScreen();
    } else if (strncmp(args, "units", 5) == 0) {
        const char* p = args + 5; while (*p == ' ') p++;
        bool imp = (strncmp(p, "imp", 3) == 0 || *p == 'f' || *p == 'F');
        wx.setUnits(imp);
        displayManager.newLinePrintLn(imp ? "Units: imperial (F)" : "Units: metric (C)");
        displayManager.printCommandScreen();
    } else if (strncmp(args, "now", 3) == 0) {
        if (WiFi.status() != WL_CONNECTED) { displayManager.newLinePrintLn("Not connected — cw first"); displayManager.printCommandScreen(); return; }
        displayManager.newLinePrintLn("Fetching...");
        bool ok = wx.forceFetch();
        if (ok) wx.printStatus();
        else { displayManager.newLinePrintLn("Fetch failed (location/network?)"); displayManager.printCommandScreen(); }
    } else if (strncmp(args, "status", 6) == 0) {
        wx.printStatus();
    } else {
        displayManager.newLinePrintLn("wx [loc <lat> <lon>|units metric|imperial|now|status]");
        displayManager.printCommandScreen();
    }
}
