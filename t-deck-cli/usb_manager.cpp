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

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

USBManager usbManager;

static USBMSC         s_msc;
static USBHIDKeyboard s_keyboard;
static volatile bool  s_usbReady  = false;
static volatile bool  s_mscActive = false;

// DMA-safe buffer in internal DRAM for SD sector I/O
static uint8_t DRAM_ATTR s_secBuf[512];

// ── MSC callbacks — run on TinyUSB task ──────────────────────────────────────
// ESP-IDF SPI master driver is thread-safe; SD.readRAW/writeRAW can be called
// from any task — the driver serialises access to SPI2 internally.

static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
    if (!s_mscActive) return -1;
    const uint32_t n = bufsize / 512;
    for (uint32_t i = 0; i < n; i++) {
        if (!SD.readRAW(s_secBuf, lba + i) && !SD.readRAW(s_secBuf, lba + i))
            return -1;
        memcpy((uint8_t*)buf + i * 512, s_secBuf, 512);
    }
    return bufsize;
}

static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t bufsize) {
    if (!s_mscActive) return -1;
    const uint32_t n = bufsize / 512;
    for (uint32_t i = 0; i < n; i++) {
        memcpy(s_secBuf, buf + i * 512, 512);
        if (!SD.writeRAW(s_secBuf, lba + i) && !SD.writeRAW(s_secBuf, lba + i))
            return -1;
    }
    return bufsize;
}

static bool onMscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (!start && load_eject) s_mscActive = false;
    return true;
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

    // ── Enable MSC ────────────────────────────────────────────────────────────
    sdCardManager.lockSD(true);
    s_mscActive = true;
    s_msc.mediaPresent(true);

    // ── Main loop: just poll keyboard; SD I/O handled in TinyUSB callbacks ────
    uint32_t lastKeyMs = millis();
    while (s_mscActive) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (millis() - lastKeyMs >= 200) {
            lastKeyMs = millis();
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { s_mscActive = false; break; }
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    s_mscActive = false;
    s_msc.mediaPresent(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    sdCardManager.lockSD(false);

    SD.end();
    vTaskDelay(pdMS_TO_TICKS(200));
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
