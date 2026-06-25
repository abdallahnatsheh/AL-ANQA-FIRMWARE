// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// ble_ident.h — header-only BLE advertisement identifier.
// Turns a 16-bit company ID + manufacturer-data bytes into a human label,
// e.g.  CID:004C 07 19 01 0F 20 ...  ->  "Apple AirPods 2 (pairing)".
//
// Three layers, in order of reliability:
//   1. Company name   — Bluetooth SIG "Company Identifiers" (the 16-bit ID in
//                       manufacturer data; survives MAC randomization).
//   2. Apple Continuity message TYPE (1st mfr byte) — what the Apple device is
//                       broadcasting (proximity pairing, nearby, Find My, ...).
//   3. Apple Proximity-Pairing MODEL (type 0x07) — exact AirPods/Beats model.
//
// Sources: Bluetooth SIG Assigned Numbers (company IDs); furiousMAC/continuity
// + librepods + ECTO-1A/AppleJuice (Apple message types & proximity models,
// community reverse-engineered — model rows marked (*) are widely replicated
// but not officially confirmed).
//
// NOTE — Google Fast Pair model IDs are a 24-bit value resolved via Google's
// online registry (4000+ devices) and cannot be tabled offline; we only flag
// the device as a Fast Pair device (service UUID 0xFE2C, handled by the caller).

#pragma once
#include <Arduino.h>

// ── 1. Bluetooth SIG company identifiers (compact, common vendors) ────────────
struct BleCompany { uint16_t id; const char* name; };

static const BleCompany BLE_COMPANIES[] = {
    {0x004C, "Apple"},        {0x0006, "Microsoft"},     {0x0075, "Samsung"},
    {0x00E0, "Google"},       {0x0087, "Garmin"},        {0x000F, "Broadcom"},
    {0x0059, "Nordic"},       {0x0499, "Ruuvi"},         {0x0157, "Huami/Amazfit"},
    {0x0001, "Ericsson"},     {0x0002, "Intel"},         {0x000D, "Texas Instr."},
    {0x0030, "ST Micro"},     {0x004F, "APT/Qualcomm"},  {0x0078, "Nike"},
    {0x00C4, "LG Electronics"},{0x0107, "Fitbit"},       {0x0171, "Amazon"},
    {0x0118, "Garmin Intl"},  {0x015D, "Estimote"},      {0x0110, "Sony Ericsson"},
    {0x012D, "Sony"},         {0x008A, "Bose"},          {0x009E, "Bose Corp"},
    {0x038F, "Xiaomi"},       {0x0157, "Huami"},         {0x05A7, "Sonos"},
    {0x0131, "Cypress"},      {0x0500, "Fitbit Inc"},    {0x0A12, "Tile"},
    {0x0646, "JBL/Harman"},   {0x004D, "Plantronics"},   {0x01D1, "Logitech"},
    {0x0201, "GN Netcom/Jabra"},{0x02E5, "Espressif"},   {0x0A8D, "Anker"},
    {0x0094, "Polar"},        {0x0822, "adidas"},        {0x06AA, "Realtek"},
    {0x05D6, "Oura"},         {0x0BDA, "Realtek SC"},    {0x0CEF, "Govee"},
    {0x048D, "Tuya"},         {0x0A9E, "Withings"},      {0x010C, "Suunto"},
};
static const int BLE_COMPANY_COUNT = sizeof(BLE_COMPANIES) / sizeof(BLE_COMPANIES[0]);

inline const char* bleCompanyName(uint16_t cid) {
    for (int i = 0; i < BLE_COMPANY_COUNT; i++)
        if (BLE_COMPANIES[i].id == cid) return BLE_COMPANIES[i].name;
    return nullptr;
}

// ── 2. Apple Continuity message types (1st manufacturer-data byte) ────────────
//    furiousMAC/continuity — these are stable/well-documented.
inline const char* appleContinuityType(uint8_t t) {
    switch (t) {
        case 0x02: return "iBeacon";
        case 0x03: return "AirPrint";
        case 0x05: return "AirDrop";
        case 0x06: return "HomeKit";
        case 0x07: return "pairing";        // Proximity Pairing (AirPods/Beats)
        case 0x08: return "Hey Siri";
        case 0x09: return "AirPlay tgt";
        case 0x0A: return "AirPlay src";
        case 0x0B: return "MagicSwitch";    // Apple Watch on/off wrist
        case 0x0C: return "Handoff";
        case 0x0D: return "tether tgt";
        case 0x0E: return "tether src";
        case 0x0F: return "Nearby Action";
        case 0x10: return "Nearby Info";    // most common (iPhone/iPad/Mac)
        case 0x12: return "Find My";        // offline finding / AirTag
        default:   return nullptr;
    }
}

// ── 3. Apple Proximity-Pairing model IDs (type 0x07, 2-byte model) ────────────
//    Model = (mfr[3] << 8) | mfr[4]  (after company, type, len, prefix).
//    (*) = community-sourced, not officially confirmed.
struct AppleModel { uint16_t id; const char* name; };

static const AppleModel APPLE_MODELS[] = {
    {0x0220, "AirPods 1"},        {0x0F20, "AirPods 2"},
    {0x1320, "AirPods 3"},        {0x1920, "AirPods 4"},          // (*)
    {0x1B20, "AirPods 4 ANC"},    // (*)
    {0x0E20, "AirPods Pro"},      {0x1420, "AirPods Pro 2"},
    {0x2420, "AirPods Pro 2 USBC"},// (*)
    {0x0A20, "AirPods Max"},      {0x1F20, "AirPods Max USBC"},   // (*)
    {0x0520, "BeatsX"},           {0x0620, "Beats Solo3"},
    {0x0920, "Beats Studio3"},    {0x0320, "Powerbeats3"},
    {0x0B20, "Powerbeats Pro"},   {0x0C20, "Beats Solo Pro"},
    {0x1020, "Beats Flex"},       {0x1120, "Beats Studio Buds"},  // (*)
    {0x1220, "Beats Fit Pro"},    {0x1720, "Beats Studio Buds+"}, // (*)
    {0x1620, "Beats Studio Pro"}, // (*)
    {0x1D20, "Powerbeats4"},      // (*)
};
static const int APPLE_MODEL_COUNT = sizeof(APPLE_MODELS) / sizeof(APPLE_MODELS[0]);

inline const char* appleProximityModel(uint16_t model) {
    for (int i = 0; i < APPLE_MODEL_COUNT; i++)
        if (APPLE_MODELS[i].id == model) return APPLE_MODELS[i].name;
    return nullptr;
}

// ── combined describer ────────────────────────────────────────────────────────
// cid  = 16-bit company ID (mfr[0] | mfr[1]<<8)
// d/len= manufacturer-data bytes AFTER the company ID (i.e. starting at type).
// Writes a friendly label into out. Returns true if it produced something
// more specific than a bare company name.
inline bool bleDescribe(uint16_t cid, const uint8_t* d, size_t len,
                        char* out, size_t outLen) {
    const char* co = bleCompanyName(cid);

    // Apple: decode message type, and the model for proximity pairing.
    if (cid == 0x004C && len >= 1) {
        uint8_t type = d[0];
        const char* tn = appleContinuityType(type);
        if (type == 0x07 && len >= 5) {
            uint16_t model = ((uint16_t)d[3] << 8) | d[4];
            const char* mdl = appleProximityModel(model);
            if (mdl) { snprintf(out, outLen, "Apple %s (pairing)", mdl); return true; }
            snprintf(out, outLen, "Apple pairing 0x%04X", model);  // unknown model
            return true;
        }
        if (tn) { snprintf(out, outLen, "Apple %s", tn); return true; }
        snprintf(out, outLen, "Apple (type 0x%02X)", type);
        return true;
    }

    // Microsoft Swift Pair beacons ride company 0x0006.
    if (cid == 0x0006) { snprintf(out, outLen, "Microsoft (Swift Pair?)"); return true; }

    if (co) { snprintf(out, outLen, "%s", co); return true; }
    snprintf(out, outLen, "CID 0x%04X", cid);
    return false;
}
