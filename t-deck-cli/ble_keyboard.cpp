// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "ble_keyboard.h"
#include "display_manager.h"
#include "input_handling.h"
#include "utilities.h"

#include <SD.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLESecurity.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

BleKeyboard bleKeyboard;

static volatile bool     s_bleConnected = false;
static volatile uint32_t s_blePasskey   = 0;   // 0 = none pending

// ── HID Report Descriptor ─────────────────────────────────────────────────────
// Keyboard — Report ID 1 (8 bytes: modifier, reserved, 6 keycodes)
// Mouse    — Report ID 2 (4 bytes: buttons, X, Y, wheel)
static const uint8_t kHidDescriptor[] = {
    // Keyboard (Report ID 1)
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID 1
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (Left Ctrl)
    0x29, 0xE7,  //   Usage Maximum (Right GUI)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8) — modifier byte
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8) — reserved byte
    0x81, 0x01,  //   Input (Constant)
    0x95, 0x06,  //   Report Count (6) — key array
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0,        // End Collection
    // Mouse (Report ID 2)
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x02,  //   Report ID 2
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //   Usage Page (Buttons)
    0x19, 0x01,  //   Usage Minimum (1)
    0x29, 0x03,  //   Usage Maximum (3)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x95, 0x03,  //   Report Count (3) — 3 buttons
    0x75, 0x01,  //   Report Size (1)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x05,  //   Report Size (5) — padding
    0x81, 0x01,  //   Input (Constant)
    0x05, 0x01,  //   Usage Page (Generic Desktop)
    0x09, 0x30,  //   Usage (X)
    0x09, 0x31,  //   Usage (Y)
    0x09, 0x38,  //   Usage (Wheel)
    0x15, 0x81,  //   Logical Minimum (-127)
    0x25, 0x7F,  //   Logical Maximum (127)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x03,  //   Report Count (3)
    0x81, 0x06,  //   Input (Data, Variable, Relative)
    0xC0,        // End Collection (Physical)
    0xC0,        // End Collection (Application)
};

// ── Security callbacks ────────────────────────────────────────────────────────
class BleKbdSecCb : public NimBLESecurityCallbacks {
    // Host wants us to display a passkey — show it on screen
    void onPassKeyNotify(uint32_t pk) override { s_blePasskey = pk; }
    // We are DISPLAY_ONLY — we never enter a passkey ourselves
    uint32_t onPassKeyRequest() override { return 0; }
    // Accept all pairing requests
    bool onSecurityRequest() override { return true; }
    // Numeric comparison — accept (not triggered for DISPLAY_ONLY)
    bool onConfirmPIN(uint32_t) override { return true; }
    // Authentication done — set connected only if link is encrypted (MITM-authenticated)
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        s_blePasskey = 0;
        if (desc->sec_state.encrypted) {
            s_bleConnected = true;
        } else {
            // Auth failed — re-advertise, let user retry
            NimBLEDevice::startAdvertising();
        }
    }
};

// ── Server callbacks ──────────────────────────────────────────────────────────
class BleKbdServerCb : public NimBLEServerCallbacks {
    // Do NOT set s_bleConnected here — wait for authenticated link in BleKbdSecCb
    void onConnect(NimBLEServer*) override {}
    void onDisconnect(NimBLEServer*) override {
        s_bleConnected = false;
        s_blePasskey   = 0;
        NimBLEDevice::startAdvertising();
    }
};

// ── ASCII → HID keycode ───────────────────────────────────────────────────────
static bool asciiToHid(uint8_t c, uint8_t &mod, uint8_t &kc) {
    mod = 0; kc = 0;
    if (c >= 'a' && c <= 'z') { kc = 0x04 + c - 'a'; return true; }
    if (c >= 'A' && c <= 'Z') { mod = 0x02; kc = 0x04 + c - 'A'; return true; }
    if (c >= '1' && c <= '9') { kc = 0x1E + c - '1'; return true; }
    switch (c) {
        case '0':    kc = 0x27; break;
        case ' ':    kc = 0x2C; break;
        case '\r':   kc = 0x28; break;
        case '\n':   kc = 0x28; break;
        case '\b':   kc = 0x2A; break;
        case '\t':   kc = 0x2B; break;
        case '\x1B': kc = 0x29; break;
        case '\x7F': kc = 0x4C; break;
        case '-':  kc = 0x2D; break;  case '=':  kc = 0x2E; break;
        case '[':  kc = 0x2F; break;  case ']':  kc = 0x30; break;
        case '\\': kc = 0x31; break;  case ';':  kc = 0x33; break;
        case '\'': kc = 0x34; break;  case '`':  kc = 0x35; break;
        case ',':  kc = 0x36; break;  case '.':  kc = 0x37; break;
        case '/':  kc = 0x38; break;
        case '!': mod=0x02; kc=0x1E; break;  case '@': mod=0x02; kc=0x1F; break;
        case '#': mod=0x02; kc=0x20; break;  case '$': mod=0x02; kc=0x21; break;
        case '%': mod=0x02; kc=0x22; break;  case '^': mod=0x02; kc=0x23; break;
        case '&': mod=0x02; kc=0x24; break;  case '*': mod=0x02; kc=0x25; break;
        case '(': mod=0x02; kc=0x26; break;  case ')': mod=0x02; kc=0x27; break;
        case '_': mod=0x02; kc=0x2D; break;  case '+': mod=0x02; kc=0x2E; break;
        case '{': mod=0x02; kc=0x2F; break;  case '}': mod=0x02; kc=0x30; break;
        case '|': mod=0x02; kc=0x31; break;  case ':': mod=0x02; kc=0x33; break;
        case '"': mod=0x02; kc=0x34; break;  case '~': mod=0x02; kc=0x35; break;
        case '<': mod=0x02; kc=0x36; break;  case '>': mod=0x02; kc=0x37; break;
        case '?': mod=0x02; kc=0x38; break;
        default: return false;
    }
    return true;
}

// ── sendKey() ─────────────────────────────────────────────────────────────────
void BleKeyboard::sendKey(char k) {
    if (!_inputKbd) return;
    uint8_t mod = 0, kc = 0;
    if (!asciiToHid((uint8_t)k, mod, kc)) return;
    uint8_t press[8]   = { mod, 0, kc, 0, 0, 0, 0, 0 };
    uint8_t release[8] = { 0,   0,  0, 0, 0, 0, 0, 0 };
    _inputKbd->notify(press, sizeof(press));
    vTaskDelay(pdMS_TO_TICKS(5));
    _inputKbd->notify(release, sizeof(release));
}

// ── sendMouseMove() ───────────────────────────────────────────────────────────
void BleKeyboard::sendMouseMove(int8_t x, int8_t y) {
    if (!_inputMouse) return;
    uint8_t report[4] = { 0, (uint8_t)x, (uint8_t)y, 0 };
    _inputMouse->notify(report, sizeof(report));
}

// ── sendMouseClick() ──────────────────────────────────────────────────────────
void BleKeyboard::sendMouseClick(uint8_t btn) {
    if (!_inputMouse) return;
    uint8_t press[4]   = { btn, 0, 0, 0 };
    uint8_t release[4] = { 0,   0, 0, 0 };
    _inputMouse->notify(press, sizeof(press));
    vTaskDelay(pdMS_TO_TICKS(50));
    _inputMouse->notify(release, sizeof(release));
}

// ── mouseStep() ───────────────────────────────────────────────────────────────
int8_t BleKeyboard::mouseStep(uint32_t elapsedMs) {
    if (elapsedMs < 25)  return 20;
    if (elapsedMs < 50)  return 13;
    if (elapsedMs < 100) return 8;
    if (elapsedMs < 200) return 5;
    return 3;
}

// ── start() ───────────────────────────────────────────────────────────────────
// T-DECK keyboard + trackball → BLE HID keyboard + mouse passthrough.
// Pairing: MITM + bonding, DISPLAY_ONLY — passkey shown on screen, typed on host.
// Trackball center: tap <300ms = left click, hold 300ms–1.5s = right click, ≥1.5s = exit.
// Backspace: auto-repeats after 1s hold (60ms interval, 2s max).
void BleKeyboard::start() {
    static const uint8_t DIR_PINS[4]   = { BOARD_TBOX_G02, BOARD_TBOX_G01,
                                           BOARD_TBOX_G04, BOARD_TBOX_G03 };
    static const int8_t  DIR_SIGN_X[4] = {  1, 0, -1, 0 };
    static const int8_t  DIR_SIGN_Y[4] = {  0, -1, 0,  1 };
    static const char*   DIR_SYM[4]    = { "R", "U", "L", "D" };

    DisplayManager& dm = displayManager;

    // ── Header ────────────────────────────────────────────────────────────────
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("KBD");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    // ── Init NimBLE HID ───────────────────────────────────────────────────────
    s_bleConnected = false;
    s_blePasskey   = 0;
    if (NimBLEDevice::getInitialized()) NimBLEDevice::deinit(true);
    NimBLEDevice::init("T-REX-KBD");
    // Bonding + MITM (passkey display) — encrypted + authenticated link
    NimBLEDevice::setSecurityAuth(true, true, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityCallbacks(new BleKbdSecCb());

    NimBLEServer*    server = NimBLEDevice::createServer();
    server->setCallbacks(new BleKbdServerCb());

    NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
    hid->manufacturer()->setValue("T-REX");
    hid->pnp(0x02, 0x05AC, 0x820A, 0x0110);
    hid->hidInfo(0x00, 0x02);
    hid->reportMap((uint8_t*)kHidDescriptor, sizeof(kHidDescriptor));
    _inputKbd   = hid->inputReport(1);
    _inputMouse = hid->inputReport(2);
    hid->startServices();  // security must be set before this

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(false);
    NimBLEDevice::startAdvertising();

    // ── Phase 1: Wait for host to pair + connect ──────────────────────────────
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.println("Advertising as: T-REX-KBD");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);   dm.println("Waiting for host...");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);   dm.println("Hold 1.5s to cancel.");
    dm.printSeparator();
    int waitY = dm.getCursorY();

    bool     exitReq     = false;
    bool     clickHeld   = false;
    uint32_t clickDownMs = 0;
    uint32_t lastDrawMs  = 0;
    uint8_t  spinIdx     = 0;
    const char* spinChars = "|/-\\";
    uint32_t lastPasskey  = 0; // track change to force redraw

    while (!s_bleConnected && !exitReq) {
        uint32_t now = millis();
        inputHandler.getKeyboardInput();

        bool cc = (bool)digitalRead(BOARD_BOOT_PIN);
        if (!cc && !clickHeld)                                   { clickDownMs = now; clickHeld = true; }
        else if (cc && clickHeld)                                { clickHeld = false; }
        else if (!cc && clickHeld && now - clickDownMs >= 1500)  { exitReq = true; break; }

        uint32_t pk = s_blePasskey;
        if (pk != lastPasskey || now - lastDrawMs >= 250) {
            lastDrawMs  = now;
            lastPasskey = pk;
            dm.fillRect(0, waitY, SCREEN_WIDTH, LINE_HEIGHT * 3, TFT_BLACK);
            dm.setCursor(10, waitY);
            if (pk) {
                // Passkey arrived — show it prominently
                char digits[8]; snprintf(digits, sizeof(digits), "%06lu", pk);
                char spaced[14];
                snprintf(spaced, sizeof(spaced), "%c %c %c %c %c %c",
                         digits[0], digits[1], digits[2],
                         digits[3], digits[4], digits[5]);
                dm.setTextColor(TFT_YELLOW); dm.println("Type on host:");
                dm.setCursor(10, waitY + LINE_HEIGHT);
                dm.setTextColor(TFT_GREEN);  dm.println(spaced);
            } else {
                // Spinner while waiting
                char spin[2] = { spinChars[spinIdx++ & 3], 0 };
                dm.setTextColor(TFT_CYAN);   dm.printText("Waiting ");
                dm.setTextColor(TFT_YELLOW); dm.println(spin);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // ── Phase 2: Keyboard + Mouse loop ────────────────────────────────────────
    if (!exitReq) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(0x7BEF);     dm.printText("[");
        dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
        dm.setTextColor(0x7BEF);     dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("KBD");
        dm.setTextColor(0x7BEF);     dm.println("]");
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.println("Keyboard + Mouse active.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("L=tap R=hold Exit=hold 1.5s");
        dm.printSeparator();
        int statusY = dm.getCursorY();

        bool dirLast[4];
        for (int i = 0; i < 4; i++) dirLast[i] = (bool)digitalRead(DIR_PINS[i]);
        uint32_t lastDirMs = millis();

        clickHeld   = false;
        clickDownMs = 0;

        char     lastKey         = 0;
        uint32_t lastKeyMs       = 0;
        bool     bsRepeatActive  = false;
        uint32_t bsRepeatStartMs = 0;
        uint32_t lastBsRepeatMs  = 0;
        static const uint32_t BS_HOLD_DELAY = 1000;
        static const uint32_t BS_RATE       = 60;
        static const uint32_t BS_MAX_TIME   = 2000;

        uint32_t lastDisplayMs = 0;
        char     lastKeyBuf[8] = "---    ";
        const char* lastDirSym = " ";

        bool running = true;
        while (running) {
            uint32_t now = millis();

            // Host disconnected — show banner, wait for reconnect + re-auth
            if (!s_bleConnected) {
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(TFT_YELLOW); dm.println("Host disconnected.");
                dm.setCursor(10, statusY + LINE_HEIGHT);
                dm.setTextColor(0x7BEF);     dm.println("Reconnecting...");
                lastPasskey = 0;
                while (!s_bleConnected && running) {
                    inputHandler.getKeyboardInput();
                    uint32_t pk2 = s_blePasskey;
                    if (pk2 != lastPasskey) {
                        lastPasskey = pk2;
                        dm.fillRect(0, statusY + LINE_HEIGHT, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                        dm.setCursor(10, statusY + LINE_HEIGHT);
                        if (pk2) {
                            char d[8]; snprintf(d, sizeof(d), "%06lu", pk2);
                            char sp[14];
                            snprintf(sp, sizeof(sp), "%c %c %c %c %c %c",
                                     d[0],d[1],d[2],d[3],d[4],d[5]);
                            dm.setTextColor(TFT_YELLOW); dm.printText("Code: ");
                            dm.setTextColor(TFT_GREEN);  dm.println(sp);
                        } else {
                            dm.setTextColor(0x7BEF); dm.println("Reconnecting...");
                        }
                    }
                    bool cc2 = (bool)digitalRead(BOARD_BOOT_PIN);
                    if (!cc2 && !clickHeld)                              { clickDownMs = millis(); clickHeld = true; }
                    else if (cc2 && clickHeld)                           { clickHeld = false; }
                    else if (!cc2 && clickHeld && millis()-clickDownMs>=1500) { running = false; break; }
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                if (!running) break;
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                bsRepeatActive = false; lastKey = 0;
            }

            // ── Keyboard passthrough + backspace hold-repeat ──────────────────
            char k = inputHandler.getKeyboardInput();
            if (k != 0) {
                if (s_bleConnected) sendKey(k);
                lastKey        = k;
                lastKeyMs      = now;
                bsRepeatActive = false;
                if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7F)
                    snprintf(lastKeyBuf, sizeof(lastKeyBuf), "'%c'    ", k);
                else
                    snprintf(lastKeyBuf, sizeof(lastKeyBuf), "0x%02X  ", (uint8_t)k);
            } else if (lastKey == '\x08' && s_bleConnected) {
                if (!bsRepeatActive && now - lastKeyMs >= BS_HOLD_DELAY) {
                    bsRepeatActive  = true;
                    bsRepeatStartMs = now;
                    lastBsRepeatMs  = now;
                    sendKey('\x08');
                } else if (bsRepeatActive) {
                    if (now - bsRepeatStartMs >= BS_MAX_TIME) {
                        bsRepeatActive = false; lastKey = 0;
                    } else if (now - lastBsRepeatMs >= BS_RATE) {
                        lastBsRepeatMs = now;
                        sendKey('\x08');
                    }
                }
            }

            // ── Mouse: accelerated trackball ──────────────────────────────────
            for (int i = 0; i < 4; i++) {
                bool cur = (bool)digitalRead(DIR_PINS[i]);
                if (cur != dirLast[i]) {
                    dirLast[i] = cur;
                    uint32_t elapsed = now - lastDirMs;
                    lastDirMs = now;
                    if (s_bleConnected)
                        sendMouseMove(DIR_SIGN_X[i] * mouseStep(elapsed),
                                      DIR_SIGN_Y[i] * mouseStep(elapsed));
                    lastDirSym = DIR_SYM[i];
                }
            }

            // ── Trackball center: click / long-press exit ─────────────────────
            bool clickCur = (bool)digitalRead(BOARD_BOOT_PIN);
            if (!clickCur && !clickHeld) {
                clickDownMs = now; clickHeld = true;
            } else if (clickCur && clickHeld) {
                uint32_t heldMs = now - clickDownMs;
                clickHeld = false;
                if      (heldMs < 300)  { if (s_bleConnected) sendMouseClick(0x01); }
                else if (heldMs < 1500) { if (s_bleConnected) sendMouseClick(0x02); }
                else                    { running = false; }
            } else if (!clickCur && clickHeld && now - clickDownMs >= 1500) {
                running = false;
            }

            // ── Status display ~8 fps ─────────────────────────────────────────
            if (now - lastDisplayMs >= 125) {
                lastDisplayMs = now;
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(TFT_CYAN);   dm.printText("KEY ");
                dm.setTextColor(TFT_WHITE);  dm.printText(lastKeyBuf);
                dm.setTextColor(TFT_CYAN);   dm.printText(" DIR ");
                dm.setTextColor(TFT_YELLOW); dm.println(lastDirSym);
                dm.setCursor(10, statusY + LINE_HEIGHT);
                if (bsRepeatActive) {
                    dm.setTextColor(TFT_RED); dm.println("BS repeating...");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    _inputKbd      = nullptr;
    _inputMouse    = nullptr;
    s_bleConnected = false;
    s_blePasskey   = 0;
    NimBLEDevice::deinit(true);
    SD.begin(39); // restore SPI after BLE deinit

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN); dm.println("BLE KBD ended.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}
