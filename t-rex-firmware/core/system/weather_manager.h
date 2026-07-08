// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// WeatherManager — current-conditions for the Home-launcher hero, from the
// Open-Meteo API (https://open-meteo.com) — free and KEYLESS (no API key, no
// secret on the SD card). Weather is live server data: it CANNOT be known
// offline. GPS only supplies the *location* of the query. So this needs:
//   1. WiFi connected (`cw`), and
//   2. a location — GPS fix (T-Deck Plus) preferred, else a configured lat/lon.
// The last successful reading is cached in RAM so the hero still shows last-known
// conditions after WiFi drops (until reboot).
//
// Open-Meteo is HTTPS-only, so the fetch uses WiFiClientSecure + setInsecure()
// (no cert pinning — a weather reading needs no confidentiality). GDMA SAFETY:
// the fetch runs ONLY from safe contexts — the `wx now` command and Home-launcher
// entry — never from the global input hook, because netspy/isoscan run associated-
// with-promiscuous and an HTTP(S) fetch there corrupts FatFS/the WiFi engine.
// Config: /config/weather.conf (lat/lon/units). Weather codes are WMO.

#ifndef WEATHER_MANAGER_H
#define WEATHER_MANAGER_H

#include <stdint.h>

class WeatherManager {
public:
    static WeatherManager& instance();

    void init();                 // load /config/weather.conf; call after sdCardManager.begin()
    bool loadConfig();
    bool saveConfig();

    // A blocking HTTPS fetch (~1-3s). Returns true on a fresh reading. Caller must
    // ensure a safe context (WiFi STA connected, NOT promiscuous/AP).
    bool forceFetch();

    bool        configured() const;                 // is there any location source (GPS-capable board or cfg loc)?
    bool        hasReading() const { return _valid; }
    bool        stale()      const;                 // no reading, or older than the refresh interval
    int         temp()       const { return _temp; }// rounded, in the configured units
    int         code()       const { return _code; }// WMO weather-interpretation code (0=clear..99=thunder)
    const char* condition()  const { return _cond; }// short label derived from the WMO code
    bool        imperial()   const { return _imperial; }

    // `weather`/`wx` command setters (each persists to SD).
    void setLoc(float lat, float lon);
    void setUnits(bool imperial);
    void printStatus();

private:
    WeatherManager() {}
    bool fetch();                          // the actual GET + parse
    bool locate(float& lat, float& lon);   // best source: GPS > manual cfg > IP geoloc
    bool locateByIp(float& lat, float& lon); // coarse auto-location from public IP (WiFi)

    float    _cfgLat = 0, _cfgLon = 0;
    bool     _haveCfgLoc = false;
    float    _ipLat = 0, _ipLon = 0;       // cached IP geolocation
    bool     _haveIpLoc = false;
    bool     _imperial   = false;

    volatile bool _valid = false;
    volatile int  _temp  = 0;
    volatile int  _code  = 0;
    char          _cond[16] = {};
    uint32_t      _lastFetchMs = 0;
    bool          _fetching    = false;
    bool          _inited      = false;
};

void runWeatherCmd(char* args);   // `weather`/`wx` handler

#endif // WEATHER_MANAGER_H
