#pragma once
#include <Arduino.h>

// ── OUI / MAC vendor + type lookup ────────────────────────────────────────────
// Covers common consumer devices, routers, IoT, and attack hardware.
// LA-MAC (locally administered) is detected without a table entry.
//
// Usage:
//   OuiInfo info = ouiLookup(mac6);
//   info.vendor   → "Apple" / "Intel" / nullptr
//   info.type     → "Phone" / "Laptop" / "Router" / "IoT" / "TV" /
//                   "Gaming" / "Attack" / "Embed" / nullptr
//
// Implementation lives in oui_lookup.cpp — one table copy shared across all TUs.
// (Previously the table was static in this header, duplicating 8 KB per includer.)

struct OuiInfo {
    const char* vendor;
    const char* type;
};

struct OuiEntry {
    uint8_t     p0, p1, p2;
    const char* vendor;
    const char* type;
};

OuiInfo     ouiLookup(const uint8_t* mac);
const char* ouiVendor(const uint8_t* mac);
