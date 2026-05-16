// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "usb_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "constants.h"
#include "utilities.h"

#include <USB.h>
#include <USBMSC.h>
#include <USBHIDKeyboard.h>
#include <SD.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

USBManager usbManager;

static USBMSC         s_msc;
static USBHIDKeyboard s_keyboard;
static volatile bool  s_usbReady  = false;
static volatile bool  s_mscActive = false;

// ── Queue-based SD handoff ────────────────────────────────────────────────────
// TinyUSB callbacks run on the TinyUSB task. SD.readRAW/writeRAW share SPI2
// with LovyanGFX. Running SD I/O from the TinyUSB task causes SPI conflicts.
// Fix: callbacks post work to a queue and block on a semaphore; the main task
// drains the queue and does all SD I/O, then signals completion.

struct MscOp {
    bool     isRead;
    uint32_t lba;
    uint8_t* buf;
    uint32_t size;
    int32_t  result;
};

static QueueHandle_t     s_opQueue = nullptr;
static SemaphoreHandle_t s_opDone  = nullptr;

static int32_t dispatchOp(bool isRead, uint32_t lba, uint8_t* buf, uint32_t size) {
    if (!s_mscActive || !s_opQueue || !s_opDone) return -1;
    MscOp op = {isRead, lba, buf, size, -1};
    MscOp* p = &op;
    if (xQueueSend(s_opQueue, &p, pdMS_TO_TICKS(500)) != pdTRUE) return -1;
    xSemaphoreTake(s_opDone, portMAX_DELAY);
    return op.result;
}

static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    return dispatchOp(true, lba, (uint8_t*)buf, bufsize);
}

static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t bufsize) {
    return dispatchOp(false, lba, buf, bufsize);
}

static bool onMscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (!start && load_eject)
        s_mscActive = false;   // host ejected — break main loop
    return true;
}

// DMA-safe sector buffer in internal DRAM — avoids any PSRAM/alignment issues
static uint8_t DRAM_ATTR s_secBuf[512];

// ── Execute one pending SD operation from the main task ───────────────────────
static void processMscOp(MscOp* op) {
    const uint32_t n = op->size / 512;
    int32_t  r = (int32_t)op->size;
    if (op->isRead) {
        for (uint32_t i = 0; i < n; i++) {
            // one immediate retry for transient SPI glitches
            if (!SD.readRAW(s_secBuf, op->lba + i) &&
                !SD.readRAW(s_secBuf, op->lba + i)) { r = -1; break; }
            memcpy(op->buf + i * 512, s_secBuf, 512);
        }
    } else {
        for (uint32_t i = 0; i < n; i++) {
            memcpy(s_secBuf, op->buf + i * 512, 512);
            if (!SD.writeRAW(s_secBuf, op->lba + i) &&
                !SD.writeRAW(s_secBuf, op->lba + i)) { r = -1; break; }
        }
    }
    op->result = r;
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

    s_keyboard.begin();
    USB.begin();
}

// ── startMSC() ────────────────────────────────────────────────────────────────
void USBManager::startMSC() {
    DisplayManager& dm = displayManager;

    if (!sdCardManager.isReady()) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED); dm.println("No SD card mounted.");
        dm.printCommandScreen(); return;
    }

    uint32_t numSectors = SD.numSectors();
    uint32_t secSize    = SD.sectorSize();
    if (numSectors == 0 || secSize == 0) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED); dm.println("Failed to read card size.");
        dm.printCommandScreen(); return;
    }

    // ── UI ────────────────────────────────────────────────────────────────────
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
    char buf[24]; snprintf(buf, sizeof(buf), "%u MB", (unsigned)(numSectors / 2048));
    dm.println(buf);
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_YELLOW);dm.println("SD visible on PC");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);    dm.println("Eject on PC first, then [q]");
    dm.setTextColor(TFT_WHITE);

    // ── Setup queue and enable MSC ─────────────────────────────────────────────
    sdCardManager.lockSD(true);
    s_opQueue  = xQueueCreate(1, sizeof(MscOp*));
    s_opDone   = xSemaphoreCreateBinary();
    s_mscActive = true;

    s_msc.mediaPresent(true);

    // ── Main loop: drain SD ops from main task to avoid SPI2 conflict ─────────
    // Keyboard polled at most every 200 ms — prevents I2C overhead from
    // starving the SD op queue and triggering the task watchdog.
    uint32_t lastKeyMs = millis();
    while (s_mscActive) {
        MscOp* op;
        if (xQueueReceive(s_opQueue, &op, pdMS_TO_TICKS(10)) == pdTRUE) {
            processMscOp(op);
            xSemaphoreGive(s_opDone);
        }
        if (millis() - lastKeyMs >= 200) {
            lastKeyMs = millis();
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { s_mscActive = false; break; }
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    s_mscActive = false;
    s_msc.mediaPresent(false);

    // Drain any op that arrived just before we set mediaPresent(false)
    MscOp* op;
    while (xQueueReceive(s_opQueue, &op, pdMS_TO_TICKS(200)) == pdTRUE) {
        op->result = -1;
        xSemaphoreGive(s_opDone);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    vQueueDelete(s_opQueue);  s_opQueue = nullptr;
    vSemaphoreDelete(s_opDone); s_opDone = nullptr;

    sdCardManager.lockSD(false);

    // Remount FatFS — use sdCardManager.begin() so ready flag is updated
    SD.end();
    vTaskDelay(pdMS_TO_TICKS(500));
    sdCardManager.begin();

    dm.clearScreen(); dm.setCursor(10, outputY);
    dm.setTextColor(TFT_GREEN); dm.println("USB ejected. SD ready.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── startHID() ────────────────────────────────────────────────────────────────
void USBManager::startHID() {
    DisplayManager& dm = displayManager;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("USB");
    dm.setTextColor(0x7BEF);    dm.printText("::");
    dm.setTextColor(TFT_YELLOW);dm.printText("HID");
    dm.setTextColor(0x7BEF);    dm.println("]");
    dm.printSeparator();

    if (!s_usbReady) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED); dm.println("Not connected to PC.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("Plug in USB cable first.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        dm.printCommandScreen(); return;
    }

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.println("Click a text field on PC.");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);    dm.println("[any key] send  [q] cancel");
    dm.setTextColor(TFT_WHITE);

    char k = 0;
    while (k == 0) { k = inputHandler.getKeyboardInput(); vTaskDelay(pdMS_TO_TICKS(50)); }
    if (k == 'q' || k == 'Q') { dm.printCommandScreen(); return; }

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_YELLOW); dm.println("Sending...");

    s_keyboard.print("T-Rex HID Test");
    s_keyboard.press(KEY_RETURN);
    vTaskDelay(pdMS_TO_TICKS(50));
    s_keyboard.release(KEY_RETURN);

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_GREEN); dm.println("Sent!");
    vTaskDelay(pdMS_TO_TICKS(2000));
    dm.printCommandScreen();
}
