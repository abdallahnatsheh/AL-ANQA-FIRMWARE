// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "usb_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "constants.h"
#include "utilities.h"

#include <USB.h>
#include <USBMSC.h>
#include "usb_keyboard.h"
#include "bad_usb.h"
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

USBManager usbManager;

static USBMSC        s_msc;
static volatile bool s_usbReady  = false;
static volatile bool s_mscActive = false;

// DMA-safe sector scratch buffer — DRAM + 4-byte alignment for ESP32-S3 SPI DMA
static uint8_t DRAM_ATTR __attribute__((aligned(4))) s_secBuf[512];

// ── Queue-based SD I/O ────────────────────────────────────────────────────────
// LovyanGFX (display) and Arduino SD both share SPI2 on T-DECK.  Their bus-lock
// mechanisms (esp-idf spi_device_acquire_bus vs Arduino SPI semaphore) don't
// coordinate.  All readRAW/writeRAW must therefore run on the same task that
// owns LovyanGFX (the main Arduino task).  TinyUSB callbacks on core-0 post
// requests onto s_ioQ and block on s_ioResp; the main loop services them.

struct MscReq {
    bool     isWrite;
    uint32_t lba;
    uint32_t n;
    uint8_t* buf;
    int32_t  result;
};

static QueueHandle_t s_ioQ    = nullptr;
static QueueHandle_t s_ioResp = nullptr;

// ── MSC callbacks — TinyUSB task ─────────────────────────────────────────────

static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    if (!s_mscActive || !s_ioQ || !s_ioResp) return -1;
    MscReq req = { false, lba, bufsize / 512, (uint8_t*)buf, 0 };
    if (xQueueSend(s_ioQ, &req, pdMS_TO_TICKS(500)) != pdTRUE) return -1;
    if (xQueueReceive(s_ioResp, &req, pdMS_TO_TICKS(8000)) != pdTRUE) return -1;
    return req.result;
}

static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t bufsize) {
    if (!s_mscActive || !s_ioQ || !s_ioResp) return -1;
    MscReq req = { true, lba, bufsize / 512, buf, 0 };
    if (xQueueSend(s_ioQ, &req, pdMS_TO_TICKS(500)) != pdTRUE) return -1;
    // Writes need a long timeout: 15 retries × 100 ms × n sectors
    uint32_t timeoutMs = 15000 + (bufsize / 512) * 1500;
    if (xQueueReceive(s_ioResp, &req, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return -1;
    return req.result;
}

static bool onMscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (!start && load_eject) s_mscActive = false;   // host ejected
    return true;
}

// ── Service one I/O request on the main task ──────────────────────────────────
// Reads:  10 retries × 20 ms = 200 ms max  (SD reads are fast; quick back-off)
// Writes: 15 retries × 100 ms = 1500 ms max (flash programming + erase can be slow)
// Windows shows "format" / "not accessible" on the first unrecovered SCSI error.
static void serviceMscIO() {
    MscReq req;
    if (xQueueReceive(s_ioQ, &req, pdMS_TO_TICKS(10)) != pdTRUE) return;

    const int   retries  = req.isWrite ? 15  : 10;
    const TickType_t gap = req.isWrite ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(20);

    req.result = (int32_t)(req.n * 512);
    for (uint32_t i = 0; i < req.n; i++) {
        bool ok = false;
        for (int r = 0; r < retries && !ok; r++) {
            if (r) vTaskDelay(gap);
            if (req.isWrite) {
                memcpy(s_secBuf, req.buf + i * 512, 512);
                ok = SD.writeRAW(s_secBuf, req.lba + i);
            } else {
                if ((ok = SD.readRAW(s_secBuf, req.lba + i)))
                    memcpy(req.buf + i * 512, s_secBuf, 512);
            }
        }
        if (!ok) { req.result = -1; break; }
    }
    xQueueSend(s_ioResp, &req, portMAX_DELAY);
}

// ── Drain in-flight requests so TinyUSB task can't hang ───────────────────────
static void drainMscQueues() {
    MscReq drain; drain.result = -1;
    // Unblock any callback currently waiting on s_ioResp
    xQueueSend(s_ioResp, &drain, pdMS_TO_TICKS(200));
    // Drain anything still in s_ioQ and post error responses
    while (xQueueReceive(s_ioQ, &drain, pdMS_TO_TICKS(100)) == pdTRUE) {
        drain.result = -1;
        xQueueSend(s_ioResp, &drain, pdMS_TO_TICKS(200));
    }
}

// Send 80 SPI clock pulses with CS HIGH — resets the SD card SPI state machine
// to idle.  Required by the SD spec after any abnormal termination.
static void sdSpiReset() {
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; i++) SPI.transfer(0xFF);   // 10 × 8 = 80 clocks
    SPI.endTransaction();
}

// ── begin() ───────────────────────────────────────────────────────────────────
void USBManager::begin() {
    USB.onEvent([](void*, esp_event_base_t, int32_t id, void*) {
        if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT)
            s_usbReady = true;
        if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT)
            s_usbReady = false;
    });

    s_msc.vendorID("T-Rex");
    s_msc.productID("SD Card");
    s_msc.productRevision("1.0");
    s_msc.onRead(onMscRead);
    s_msc.onWrite(onMscWrite);
    s_msc.onStartStop(onMscStartStop);
    s_msc.mediaPresent(false);
    uint32_t ns = SD.numSectors();
    uint32_t ss = SD.sectorSize();
    s_msc.begin(ns ? ns : 2048, ss ? ss : 512);

    usbKeyboard.begin();
    badUsb.begin();
    USB.begin();
}

// ── startMSC() ────────────────────────────────────────────────────────────────
void USBManager::startMSC() {
    DisplayManager& dm = displayManager;

    if (!s_usbReady) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(TFT_RED); dm.println("Not connected to PC.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("Plug in USB cable first.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        dm.printCommandScreen(); return;
    }

    if (!sdCardManager.isReady()) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(TFT_RED); dm.println("No SD card mounted.");
        dm.printCommandScreen(); return;
    }

    uint32_t numSectors = SD.numSectors();
    uint32_t secSize    = SD.sectorSize();
    if (numSectors == 0 || secSize == 0) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(TFT_RED); dm.println("Failed to read card size.");
        dm.printCommandScreen(); return;
    }

    // ── UI ────────────────────────────────────────────────────────────────────
    uint32_t mscNumSectors = numSectors;   // captured for redraw lambda
    auto drawMscScreen = [&]() {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(0x7BEF);    dm.printText("[");
        dm.setTextColor(TFT_CYAN);  dm.printText("USB");
        dm.setTextColor(0x7BEF);    dm.printText("::");
        dm.setTextColor(TFT_YELLOW);dm.printText("MSC");
        dm.setTextColor(0x7BEF);    dm.println("]");
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.printText("Mode    ");
        dm.setTextColor(TFT_GREEN); dm.println("Mass Storage");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.printText("Size    ");
        dm.setTextColor(TFT_WHITE);
        char buf[24]; snprintf(buf, sizeof(buf), "%u MB", (unsigned)(mscNumSectors / 2048));
        dm.println(buf);
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);dm.println("SD visible on PC");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("Eject on PC first, then [q]");
        dm.setTextColor(TFT_WHITE);
    };
    drawMscScreen();

    // ── Stop WiFi GDMA, drain display DMA, release SPI2 ──────────────────────
    wifi_mode_t wifiMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&wifiMode);
    bool wifiWasActive = (wifiMode != WIFI_MODE_NULL);
    if (wifiWasActive) {
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    displayManager.flushSPI();
    vTaskDelay(pdMS_TO_TICKS(20));

    // ── Hold ALL SPI CS pins HIGH ─────────────────────────────────────────────
    // LoRa radio (GPIO9), display (GPIO12) and SD (GPIO39) share SPI2.
    // If RADIO_CS_PIN is low/floating the radio drives MISO during SD reads,
    // corrupting every sector transfer — confirmed by the official LilyGO SD.ino.
    pinMode(RADIO_CS_PIN,   OUTPUT); digitalWrite(RADIO_CS_PIN,   HIGH);
    pinMode(BOARD_TFT_CS,   OUTPUT); digitalWrite(BOARD_TFT_CS,   HIGH);
    pinMode(BOARD_SDCARD_CS,OUTPUT); digitalWrite(BOARD_SDCARD_CS,HIGH);

    // ── Queues (depth 1 = fully synchronous round-trip) ───────────────────────
    s_ioQ    = xQueueCreate(1, sizeof(MscReq));
    s_ioResp = xQueueCreate(1, sizeof(MscReq));

    // ── Enable MSC ────────────────────────────────────────────────────────────
    s_msc.begin(numSectors, secSize);
    sdCardManager.lockSD(true);
    s_mscActive = true;
    s_msc.mediaPresent(true);

    // ── Main loop ─────────────────────────────────────────────────────────────
    uint32_t lastKeyMs = millis();
    while (s_mscActive) {
        serviceMscIO();
        if (LockScreenManager::getInstance().consumeJustUnlocked())
            drawMscScreen();
        if (millis() - lastKeyMs >= 200) {
            lastKeyMs = millis();
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { s_mscActive = false; break; }
        }
    }

    // ── Drain queues so TinyUSB task can't hang ───────────────────────────────
    s_mscActive = false;
    drainMscQueues();

    // ── Teardown ──────────────────────────────────────────────────────────────
    s_msc.mediaPresent(false);
    vTaskDelay(pdMS_TO_TICKS(800));   // let host process ejection

    SD.end();

    // Reset SD card SPI state machine — SD spec requires 80 clock cycles with
    // CS HIGH after any abnormal termination (cable yank, mid-write abort).
    vTaskDelay(pdMS_TO_TICKS(200));
    sdSpiReset();
    vTaskDelay(pdMS_TO_TICKS(1500)); // card internal operations settle

    // Re-mount: up to 8 attempts, 500 ms apart
    bool remounted = false;
    for (int i = 0; i < 8 && !remounted; i++) {
        remounted = sdCardManager.begin();
        if (!remounted) vTaskDelay(pdMS_TO_TICKS(500));
    }
    sdCardManager.lockSD(false);

    vQueueDelete(s_ioQ);    s_ioQ    = nullptr;
    vQueueDelete(s_ioResp); s_ioResp = nullptr;

    dm.clearScreen(); dm.setCursor(10, outputY);
    if (wifiWasActive) WiFi.mode(WIFI_STA);

    if (remounted) {
        dm.setTextColor(TFT_GREEN); dm.println("USB ejected. SD ready.");
    } else {
        dm.setTextColor(TFT_RED);   dm.println("SD remount failed.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("Re-insert card or run sdls.");
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── isConnected() ─────────────────────────────────────────────────────────────
bool USBManager::isConnected() const {
    return s_usbReady;
}
