// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "usb_keyboard.h"
#include "usb_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "utilities.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

UsbKeyboard    usbKeyboard;
USBHIDKeyboard g_hid_keyboard;

// ── begin() ───────────────────────────────────────────────────────────────────
void UsbKeyboard::begin() {
    g_hid_keyboard.begin();
    _mouse.begin();
}

// ── sendKey() ─────────────────────────────────────────────────────────────────
void UsbKeyboard::sendKey(char k) {
    switch ((uint8_t)k) {
        case 0x08: g_hid_keyboard.press(KEY_BACKSPACE); vTaskDelay(1); g_hid_keyboard.release(KEY_BACKSPACE); break;
        case 0x09: g_hid_keyboard.press(KEY_TAB);       vTaskDelay(1); g_hid_keyboard.release(KEY_TAB);       break;
        case 0x0D: g_hid_keyboard.press(KEY_RETURN);    vTaskDelay(1); g_hid_keyboard.release(KEY_RETURN);    break;
        case 0x1B: g_hid_keyboard.press(KEY_ESC);       vTaskDelay(1); g_hid_keyboard.release(KEY_ESC);       break;
        case 0x7F: g_hid_keyboard.press(KEY_DELETE);    vTaskDelay(1); g_hid_keyboard.release(KEY_DELETE);    break;
        default:
            if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7F) g_hid_keyboard.print(k);
            break;
    }
}

// ── mouseStep() ───────────────────────────────────────────────────────────────
// Accelerated movement: fast spin → large delta, slow/precise → small delta.
int8_t UsbKeyboard::mouseStep(uint32_t elapsedMs) {
    if (elapsedMs < 25)  return 20;
    if (elapsedMs < 50)  return 13;
    if (elapsedMs < 100) return 8;
    if (elapsedMs < 200) return 5;
    return 3;
}

// ── start() ───────────────────────────────────────────────────────────────────
// T-DECK keyboard + trackball → USB HID keyboard + mouse passthrough.
// Trackball center: tap < 300ms = left click, hold 300ms–1.5s = right click, hold ≥1.5s = exit.
// Backspace: auto-repeats after 500ms (60ms interval, 2s max — press BS again for more).
void UsbKeyboard::start() {
    static const uint8_t DIR_PINS[4]   = { BOARD_TBOX_G02, BOARD_TBOX_G01,
                                           BOARD_TBOX_G04, BOARD_TBOX_G03 };
    static const int8_t  DIR_SIGN_X[4] = {  1, 0, -1, 0 };   // RIGHT, UP, LEFT, DOWN
    static const int8_t  DIR_SIGN_Y[4] = {  0, -1, 0,  1 };
    static const char*   DIR_SYM[4]    = { "R", "U", "L", "D" };

    DisplayManager& dm = displayManager;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("USB");
    dm.setTextColor(0x7BEF);    dm.printText("::");
    dm.setTextColor(TFT_YELLOW);dm.printText("KBD");
    dm.setTextColor(0x7BEF);    dm.println("]");
    dm.printSeparator();

    if (!usbManager.isConnected()) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED); dm.println("Not connected to PC.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("Plug in USB cable first.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        dm.printCommandScreen(); return;
    }

    // Flush any stale HID state from a previous BadUSB or KBD session
    g_hid_keyboard.releaseAll();
    vTaskDelay(pdMS_TO_TICKS(500));

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.println("Keyboard + Mouse active.");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF); dm.println("L=tap R=hold Exit=hold 1.5s");
    dm.printSeparator();
    int statusY = dm.getCursorY();

    // ── State ─────────────────────────────────────────────────────────────────
    bool dirLast[4];
    for (int i = 0; i < 4; i++) dirLast[i] = (bool)digitalRead(DIR_PINS[i]);
    uint32_t lastDirMs = millis();

    bool     clickHeld   = false;
    uint32_t clickDownMs = 0;

    char     lastKey         = 0;
    uint32_t lastKeyMs       = 0;
    bool     bsRepeatActive  = false;
    uint32_t bsLastBsMs = 0;           // last time \x08 was delivered; arm only when cold
    uint32_t bsRepeatStartMs = 0;
    uint32_t lastBsRepeatMs  = 0;
    static const uint32_t BS_HOLD_DELAY = 1500; // hold 1.5s before repeat starts
    static const uint32_t BS_RATE       = 60;   // ms between repeat keystrokes
    static const uint32_t BS_MAX_TIME   = 2000; // repeat for max 2s then stop

    uint32_t    lastDisplayMs = 0;
    char        lastKeyBuf[8] = "---    ";
    const char* lastDirSym    = " ";

    bool running = true;
    while (running) {
        uint32_t now = millis();

        // ── Keyboard passthrough + hold-to-repeat backspace ──────────────────
        char k = inputHandler.getKeyboardInput();
        if (k != 0) {
            sendKey(k);
            if (k == '\x08') {
                if (lastKey == '\x08') {
                    // Timer armed: second tap cancels it — user is tapping, not holding
                    lastKey = 0;   // lastKey != '\x08' → hold-timer branch won't fire
                } else if (now - bsLastBsMs >= BS_HOLD_DELAY) {
                    // Cold: arm hold timer
                    lastKey   = k;
                    lastKeyMs = now;
                }
                // else: hot — single delete, no arm
                bsLastBsMs = now;
            } else {
                lastKey    = k;
                lastKeyMs  = now;
                bsLastBsMs = 0;  // reset: next \x08 is cold → fresh hold works immediately
            }
            bsRepeatActive = false;
            if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7F)
                snprintf(lastKeyBuf, sizeof(lastKeyBuf), "'%c'    ", k);
            else
                snprintf(lastKeyBuf, sizeof(lastKeyBuf), "0x%02X  ", (uint8_t)k);
        } else if (lastKey == '\x08') {
            if (!bsRepeatActive && (now - lastKeyMs >= BS_HOLD_DELAY)) {
                bsRepeatActive  = true;
                bsRepeatStartMs = now;
                lastBsRepeatMs  = now;
                sendKey('\x08');
                bsLastBsMs = now;  // keep hot while auto-delete runs
            } else if (bsRepeatActive) {
                if (now - bsRepeatStartMs >= BS_MAX_TIME) {
                    bsRepeatActive = false;
                    lastKey = 0;
                } else if (now - lastBsRepeatMs >= BS_RATE) {
                    lastBsRepeatMs = now;
                    sendKey('\x08');
                    bsLastBsMs = now;  // keep hot while auto-delete runs
                }
            }
        }

        // ── Mouse: accelerated trackball directions ───────────────────────────
        for (int i = 0; i < 4; i++) {
            bool cur = (bool)digitalRead(DIR_PINS[i]);
            if (cur != dirLast[i]) {
                dirLast[i] = cur;
                uint32_t elapsed = now - lastDirMs;
                lastDirMs = now;
                int8_t step = mouseStep(elapsed);
                _mouse.move(DIR_SIGN_X[i] * step, DIR_SIGN_Y[i] * step, 0);
                lastDirSym = DIR_SYM[i];
            }
        }

        // ── Mouse: center click / long-press exit ─────────────────────────────
        // BOARD_BOOT_PIN (GPIO0): LOW = pressed, HIGH = released (active-low)
        bool clickCur = (bool)digitalRead(BOARD_BOOT_PIN);
        if (!clickCur && !clickHeld) {
            clickDownMs = now;
            clickHeld   = true;
        } else if (clickCur && clickHeld) {
            uint32_t heldMs = now - clickDownMs;
            clickHeld = false;
            if (heldMs < 300)
                _mouse.click(MOUSE_LEFT);
            else if (heldMs < 1500)
                _mouse.click(MOUSE_RIGHT);
            else
                running = false;
        } else if (!clickCur && clickHeld) {
            if (now - clickDownMs >= 1500)
                running = false;
        }

        // ── Status display: ~8 fps ────────────────────────────────────────────
        if (now - lastDisplayMs >= 125) {
            lastDisplayMs = now;
            dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
            dm.setCursor(10, statusY);
            dm.setTextColor(TFT_CYAN);  dm.printText("KEY ");
            dm.setTextColor(TFT_WHITE); dm.printText(lastKeyBuf);
            dm.setTextColor(TFT_CYAN);  dm.printText(" DIR ");
            dm.setTextColor(TFT_YELLOW);dm.println(lastDirSym);
            dm.setCursor(10, statusY + LINE_HEIGHT);
            if (bsRepeatActive) {
                dm.setTextColor(TFT_RED); dm.println("BS repeating...");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(4));
    }

    g_hid_keyboard.releaseAll();
    _mouse.release(MOUSE_LEFT);

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN); dm.println("KBD mode ended.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── jiggle() ──────────────────────────────────────────────────────────────────
// Nudges mouse +2px right then -2px left every 30s to prevent screen lock.
// Cursor returns to its original position — imperceptible to the target user.
void UsbKeyboard::jiggle() {
    static const uint32_t INTERVAL_MS = 30000;

    DisplayManager& dm = displayManager;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("USB");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_GREEN);  dm.printText("JIGGLE");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    if (!usbManager.isConnected()) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED);  dm.println("Not connected to PC.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);   dm.println("Plug in USB cable first.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        dm.printCommandScreen(); return;
    }

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);  dm.println("Jiggling every 30s.");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);     dm.println("Press q to stop.");
    dm.printSeparator();
    int statusY = dm.getCursorY();

    uint32_t lastJiggleMs = millis() - INTERVAL_MS; // jiggle immediately on start
    uint32_t lastDisplayMs = 0;
    uint32_t jiggles = 0;

    while (true) {
        uint32_t now = millis();

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (now - lastJiggleMs >= INTERVAL_MS) {
            lastJiggleMs = now;
            _mouse.move(2, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(80));
            _mouse.move(-2, 0, 0);
            jiggles++;
        }

        if (now - lastDisplayMs >= 250) {
            lastDisplayMs = now;
            uint32_t secsLeft = (INTERVAL_MS - (now - lastJiggleMs)) / 1000;
            dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
            dm.setCursor(10, statusY);
            dm.setTextColor(TFT_CYAN);  dm.printText("Next jiggle in ");
            dm.setTextColor(TFT_YELLOW);
            char buf[8]; snprintf(buf, sizeof(buf), "%lus", secsLeft);
            dm.println(buf);
            dm.setCursor(10, statusY + LINE_HEIGHT);
            dm.setTextColor(TFT_CYAN);  dm.printText("Jiggles: ");
            dm.setTextColor(TFT_WHITE);
            snprintf(buf, sizeof(buf), "%lu", jiggles);
            dm.println(buf);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN); dm.println("Jiggler stopped.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}
