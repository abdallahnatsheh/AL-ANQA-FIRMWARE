// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "ble_keyboard.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "utilities.h"

#include <SD.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

BleKeyboard bleKeyboard;

static volatile bool     s_bleConnected      = false;
static volatile uint16_t s_connHandle        = BLE_HS_CONN_HANDLE_NONE;
static volatile bool     s_bleExiting        = false;
static volatile bool     s_kbdStaleBond      = false;  // Windows has stale LTK — must remove from BT settings
static volatile int      s_lastDisconReason  = 0;      // last onDisconnect reason code — shown on screen for diagnosis

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

// ── Server + security callbacks (NimBLE v2.x: security folded into server cb) ─
class BleKbdServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
        s_kbdStaleBond = false;
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& connInfo, int reason) override {
        s_bleConnected     = false;
        s_connHandle       = BLE_HS_CONN_HANDLE_NONE;
        s_lastDisconReason = reason;
        if (reason == 0x05 || reason == 0x06) {
            // AUTH_FAIL / KEY_MISSING — stale LTK mismatch.
            // Delete T-DECK's bond so on the next attempt Windows gets
            // LL_REJECT_IND and initiates a fresh pairing automatically.
            NimBLEDevice::deleteBond(connInfo.getAddress());
            s_kbdStaleBond = true;
        }
        if (!s_bleExiting) NimBLEDevice::startAdvertising();
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        if (connInfo.isEncrypted()) {
            s_kbdStaleBond     = false;
            s_lastDisconReason = 0;
            s_connHandle       = connInfo.getConnHandle();
            s_bleConnected     = true;
        } else {
            // SMP completed but link is unencrypted — treat as stale bond.
            s_lastDisconReason = -1;
            s_kbdStaleBond     = true;
            NimBLEDevice::deleteBond(connInfo.getAddress());
            NimBLEDevice::startAdvertising();
        }
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
// Pairing: Just Works (no PIN), bonding — link is AES-128 encrypted after pairing.
// Trackball center: tap <300ms = left click, hold 300ms–1.5s = right click, ≥1.5s = exit.
// Backspace: auto-repeats after 1s hold (60ms interval, 2s max).
void BleKeyboard::start() {
    static const uint8_t DIR_PINS[4]   = { BOARD_TBOX_G02, BOARD_TBOX_G01,
                                           BOARD_TBOX_G04, BOARD_TBOX_G03 };
    static const int8_t  DIR_SIGN_X[4] = {  1, 0, -1, 0 };
    static const int8_t  DIR_SIGN_Y[4] = {  0, -1, 0,  1 };
    static const char*   DIR_SYM[4]    = { "R", "U", "L", "D" };

    DisplayManager& dm = displayManager;

    // ── Init NimBLE HID ───────────────────────────────────────────────────────
    s_bleConnected = false;
    s_connHandle   = BLE_HS_CONN_HANDLE_NONE;
    s_bleExiting   = false;
    s_kbdStaleBond = false;
    // Double-cycle cold-reset — required; single-cycle leaves the HID stack in
    // a state that crashes on exit. Longer delays (200ms) prevent crashes when
    // coming from buddy, which leaves its own init("T-REX") active on exit.
    if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::deinit(true);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    NimBLEDevice::init("T-REX-KBD");
    vTaskDelay(pdMS_TO_TICKS(200));
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(200));
    NimBLEDevice::init("T-REX-KBD");
    // Stable unique address for btkbd (suffix CB) — different from buddy (BD) so
    // Windows stores two separate bonds and never confuses keyboard with NUS client.
    // Must be called after init() — ble_hs_id_set_rnd needs the host running.
    {
        uint8_t hwmac[6];
        esp_read_mac(hwmac, ESP_MAC_BT);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "C2:%02X:%02X:%02X:%02X:CB",
                 hwmac[1], hwmac[2], hwmac[3], hwmac[4]);
        NimBLEDevice::setOwnAddr(NimBLEAddress(macStr, BLE_ADDR_RANDOM));  // register first
        NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);                // succeeds now address exists
    }
    // Just Works — no PIN dialog, link is encrypted after pairing
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    NimBLEServer* server = NimBLEDevice::createServer();
    // Use heap-allocated callback: NimBLE v2.x setCallbacks() defaults deleteCallbacks=true,
    // so deinit(true) will delete it cleanly. Using a static address would crash.
    server->setCallbacks(new BleKbdServerCb());

    NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
    hid->setManufacturer("T-REX");
    hid->setPnp(0x02, 0x05AC, 0x820A, 0x0110);
    hid->setHidInfo(0x00, 0x02);
    hid->setReportMap((uint8_t*)kHidDescriptor, sizeof(kHidDescriptor));
    _inputKbd   = hid->getInputReport(1);
    _inputMouse = hid->getInputReport(2);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    NimBLEDevice::startAdvertising();

    // ── Phase 1 static draw ───────────────────────────────────────────────────
    int waitY = 0;
    auto drawPhase1 = [&]() {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(0x7BEF);     dm.printText("[");
        dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
        dm.setTextColor(0x7BEF);     dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("KBD");
        dm.setTextColor(0x7BEF);     dm.println("]");
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.println("Advertising as: T-REX-KBD");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);   dm.println("Waiting for host...");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);   dm.println("Hold 1.5s to cancel.");
        dm.printSeparator();
        waitY = dm.getCursorY();
    };
    drawPhase1();

    // ── Phase 1: Wait for host to connect ────────────────────────────────────
    bool     exitReq     = false;
    bool     clickHeld   = false;
    uint32_t clickDownMs = 0;
    uint32_t lastDrawMs  = 0;
    uint8_t  spinIdx     = 0;
    const char* spinChars = "|/-\\";

    while (!s_bleConnected && !exitReq) {
        uint32_t now = millis();
        inputHandler.getKeyboardInput();

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawPhase1();
            lastDrawMs = 0;
        }

        bool cc = (bool)digitalRead(BOARD_BOOT_PIN);
        if (!cc && !clickHeld)                                   { clickDownMs = now; clickHeld = true; }
        else if (cc && clickHeld)                                { clickHeld = false; }
        else if (!cc && clickHeld && now - clickDownMs >= 1500)  { exitReq = true; break; }

        if (now - lastDrawMs >= 250) {
            lastDrawMs = now;
            dm.fillRect(0, waitY, SCREEN_WIDTH, LINE_HEIGHT * 3, TFT_BLACK);
            dm.setCursor(10, waitY);
            if (s_kbdStaleBond) {
                char rbuf[32];
                snprintf(rbuf, sizeof(rbuf), "Auth fail (0x%02X) re-pair", s_lastDisconReason & 0xFF);
                dm.setTextColor(TFT_RED);    dm.println(rbuf);
                dm.setCursor(10, waitY + LINE_HEIGHT);
                dm.setTextColor(TFT_YELLOW); dm.println("Waiting for Win to");
                dm.setCursor(10, waitY + LINE_HEIGHT * 2);
                dm.setTextColor(TFT_YELLOW); dm.println("re-pair automatically...");
            } else {
                char spin[2] = { spinChars[spinIdx++ & 3], 0 };
                dm.setTextColor(TFT_CYAN);   dm.printText("Waiting ");
                dm.setTextColor(TFT_YELLOW); dm.println(spin);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // ── Phase 2: Keyboard + Mouse loop ────────────────────────────────────────
    if (!exitReq) {
        int statusY = 0;
        auto drawPhase2Header = [&]() {
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
            statusY = dm.getCursorY();
        };
        drawPhase2Header();

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

            // Host disconnected — show banner, wait for reconnect
            if (!s_bleConnected) {
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(TFT_YELLOW); dm.println("Host disconnected.");
                dm.setCursor(10, statusY + LINE_HEIGHT);
                dm.setTextColor(0x7BEF);     dm.println("Reconnecting...");
                while (!s_bleConnected && running) {
                    inputHandler.getKeyboardInput();

                    if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                        drawPhase2Header();
                        dm.setCursor(10, statusY);
                        dm.setTextColor(TFT_YELLOW); dm.println("Host disconnected.");
                        dm.setCursor(10, statusY + LINE_HEIGHT);
                        dm.setTextColor(0x7BEF);     dm.println("Reconnecting...");
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

            // ── Unlock redraw ─────────────────────────────────────────────────
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawPhase2Header();
                lastDisplayMs = 0;
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
    _inputKbd    = nullptr;
    _inputMouse  = nullptr;
    s_bleExiting = true;

    NimBLEDevice::getAdvertising()->stop();

    // Disconnect active session and wait for the link to fully close
    if (s_connHandle != BLE_HS_CONN_HANDLE_NONE) {
        NimBLEDevice::getServer()->disconnect(s_connHandle);
        uint32_t t0 = millis();
        while (s_connHandle != BLE_HS_CONN_HANDLE_NONE && millis() - t0 < 1000) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // 500ms drain: after disconnect the NimBLE host task clears CCCD subscriptions
    // and flushes pending HID ATT ops. deinit(true) is safe once that's done.
    vTaskDelay(pdMS_TO_TICKS(500));

    s_bleConnected = false;
    s_connHandle   = BLE_HS_CONN_HANDLE_NONE;
    s_bleExiting   = false;

    // Full teardown — leaves BLE off so buddy/next command inits from clean slate
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    NimBLEDevice::init("T-REX");

    SD.begin(39);

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN); dm.println("BLE KBD ended.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}
