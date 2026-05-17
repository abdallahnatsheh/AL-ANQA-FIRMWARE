// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Claude Desktop BLE remote (claude-desktop-buddy port).
// Connects to Claude Desktop via BLE Nordic UART Service (NUS).
// Desktop pushes session status + permission prompts; T-DECK approves/denies.
// Original protocol: https://github.com/anthropics/claude-desktop-buddy

#include "buddy.h"
#include "display_manager.h"
#include "input_handling.h"

#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <SD.h>
#include "esp_random.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── NUS UUIDs (standard Nordic UART Service) ─────────────────────────────────
#define NUS_SVC_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // desktop→device
#define NUS_TX_UUID   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // device→desktop

// ── RX ring buffer (written from BLE callback, read from main loop) ───────────
static const size_t RX_CAP = 2048;
static uint8_t           s_rxBuf[RX_CAP];
static volatile size_t   s_rxHead = 0;
static volatile size_t   s_rxTail = 0;

static void rxPush(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t next = (s_rxHead + 1) % RX_CAP;
        if (next == s_rxTail) return;  // full — drop
        s_rxBuf[s_rxHead] = p[i];
        s_rxHead = next;
    }
}
static bool rxAvail()  { return s_rxHead != s_rxTail; }
static int  rxRead()   {
    if (s_rxHead == s_rxTail) return -1;
    int b = s_rxBuf[s_rxTail];
    s_rxTail = (s_rxTail + 1) % RX_CAP;
    return b;
}

// ── BLE state ─────────────────────────────────────────────────────────────────
static NimBLEServer*         s_server    = nullptr;
static NimBLECharacteristic* s_txChar    = nullptr;
static volatile bool         s_connected = false;
static volatile bool         s_secure    = false;
static uint32_t              s_passkey   = 0;

// ── Session state (TamaState from claude-desktop-buddy data.h) ────────────────
struct TamaState {
    uint8_t  sessionsTotal;
    uint8_t  sessionsRunning;
    uint8_t  sessionsWaiting;
    bool     recentlyCompleted;
    uint32_t tokensToday;
    uint32_t lastUpdated;
    char     msg[24];
    bool     connected;
    char     lines[8][92];
    uint8_t  nLines;
    uint16_t lineGen;
    char     promptId[40];    // empty = no pending prompt
    char     promptTool[20];
    char     promptHint[44];
};

// ── BLE write (chunked to MTU) ────────────────────────────────────────────────
static size_t buddyWrite(const uint8_t* data, size_t len) {
    if (!s_connected || !s_txChar) return 0;
    const size_t chunk = 180;
    size_t sent = 0;
    while (sent < len) {
        size_t n = len - sent;
        if (n > chunk) n = chunk;
        s_txChar->setValue(data + sent, n);
        s_txChar->notify();
        sent += n;
        delay(4);
    }
    return sent;
}

static void sendPermission(const char* id, const char* decision) {
    char buf[108];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
             id, decision);
    buddyWrite((const uint8_t*)buf, strlen(buf));
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────
class BuddyRxCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string v = c->getValue();
        if (!v.empty()) rxPush((const uint8_t*)v.data(), v.size());
    }
};

class BuddySrvCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        s_connected = true;
    }
    void onDisconnect(NimBLEServer*) override {
        s_connected = false;
        s_secure    = false;
        NimBLEDevice::startAdvertising();
    }
};

// ── JSON parsing (adapted from data.h, M5.Rtc calls removed) ─────────────────
static uint32_t s_lastLiveMs    = 0;
static uint32_t s_lastBtByteMs = 0;
static char     s_lineBuf[1024];
static uint16_t s_lineLen = 0;

static void applyJson(const char* line, TamaState* out) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;

    // Skip time sync — no RTC on T-DECK
    if (!doc["time"].isNull()) { s_lastLiveMs = millis(); return; }

    out->sessionsTotal     = doc["total"]        | out->sessionsTotal;
    out->sessionsRunning   = doc["running"]      | out->sessionsRunning;
    out->sessionsWaiting   = doc["waiting"]      | out->sessionsWaiting;
    out->recentlyCompleted = doc["completed"]    | false;
    out->tokensToday       = doc["tokens_today"] | out->tokensToday;

    const char* m = doc["msg"];
    if (m) { strncpy(out->msg, m, sizeof(out->msg)-1); out->msg[sizeof(out->msg)-1] = 0; }

    JsonArray la = doc["entries"];
    if (!la.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : la) {
            if (n >= 8) break;
            const char* s = v.as<const char*>();
            strncpy(out->lines[n], s ? s : "", 91);
            out->lines[n][91] = 0;
            n++;
        }
        if (n != out->nLines) out->lineGen++;
        out->nLines = n;
    }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
        const char* pid = pr["id"];
        const char* pt  = pr["tool"];
        const char* ph  = pr["hint"];
        strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId)-1);
        out->promptId[sizeof(out->promptId)-1] = 0;
        strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool)-1);
        out->promptTool[sizeof(out->promptTool)-1] = 0;
        strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint)-1);
        out->promptHint[sizeof(out->promptHint)-1] = 0;
    } else {
        out->promptId[0] = 0;
        out->promptTool[0] = 0;
        out->promptHint[0] = 0;
    }
    out->lastUpdated = millis();
    s_lastLiveMs = millis();
}

static void dataPoll(TamaState* out) {
    while (rxAvail()) {
        int c = rxRead();
        if (c < 0) break;
        s_lastBtByteMs = millis();
        if (c == '\n' || c == '\r') {
            if (s_lineLen > 0) {
                s_lineBuf[s_lineLen] = 0;
                if (s_lineBuf[0] == '{') applyJson(s_lineBuf, out);
                s_lineLen = 0;
            }
        } else if (s_lineLen < sizeof(s_lineBuf) - 1) {
            s_lineBuf[s_lineLen++] = (char)c;
        }
    }
    bool fresh = s_lastLiveMs && (millis() - s_lastLiveMs) <= 30000;
    out->connected = fresh && s_connected;
    if (!out->connected) {
        const char* idle = s_connected ? "Connected, waiting for data..."
                                       : "Waiting for Claude Desktop...";
        strncpy(out->msg, idle, sizeof(out->msg)-1);
        out->msg[sizeof(out->msg)-1] = 0;
    }
}

// ── Display ───────────────────────────────────────────────────────────────────
static void drawBuddy(const TamaState& t, uint32_t pk, bool bleConn, const char* name) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    int y = outputY;

    // Header
    displayManager.setCursor(10, y);
    displayManager.setTextColor(0x7BEF);  displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN); displayManager.printText("BUDDY");
    displayManager.setTextColor(0x7BEF);  displayManager.printText("::");
    if (!bleConn) {
        displayManager.setTextColor(0x7BEF);  displayManager.printText("advertising");
    } else if (t.sessionsWaiting > 0) {
        displayManager.setTextColor(0xFA20);  displayManager.printText("APPROVAL NEEDED");
    } else {
        displayManager.setTextColor(TFT_GREEN); displayManager.printText("connected");
    }
    displayManager.setTextColor(0x7BEF); displayManager.printText("] ");
    displayManager.setTextColor(0x7BEF); displayManager.println(name);
    y += LINE_HEIGHT;

    if (!bleConn) {
        y += LINE_HEIGHT;
        displayManager.setCursor(10, y);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Pair with Claude Desktop:");
        y += LINE_HEIGHT;
        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Help -> Troubleshooting ->");
        y += LINE_HEIGHT;
        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Enable Developer Mode");
        y += LINE_HEIGHT;
        displayManager.setCursor(10, y);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println(name);
        y += LINE_HEIGHT * 2;
        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF); displayManager.println("[q] quit");
        return;
    }

    // Connected — session stats
    y += LINE_HEIGHT;
    displayManager.setCursor(10, y);
    displayManager.setTextColor(0x7BEF);  displayManager.printText("Sessions: ");
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.printText(String(t.sessionsTotal).c_str());
    displayManager.setTextColor(0x7BEF);  displayManager.printText(" total  ");
    displayManager.setTextColor(t.sessionsWaiting > 0 ? (uint16_t)0xFA20 : (uint16_t)TFT_GREEN);
    displayManager.printText(String(t.sessionsWaiting).c_str());
    displayManager.setTextColor(0x7BEF);  displayManager.println(" waiting");
    y += LINE_HEIGHT;

    displayManager.setCursor(10, y);
    displayManager.setTextColor(0x7BEF); displayManager.printText("Tokens today: ");
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println(String(t.tokensToday).c_str());
    y += LINE_HEIGHT;

    displayManager.setCursor(10, y);
    displayManager.printSeparator();
    y += LINE_HEIGHT;

    if (t.promptId[0]) {
        // Pending approval
        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF);    displayManager.printText(" Tool:  ");
        displayManager.setTextColor(TFT_YELLOW); displayManager.println(t.promptTool);
        y += LINE_HEIGHT;

        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF);   displayManager.printText(" Hint:  ");
        displayManager.setTextColor(TFT_CYAN); displayManager.println(t.promptHint);
        y += LINE_HEIGHT * 2;

        displayManager.setCursor(10, y);
        displayManager.printSeparator();
        y += LINE_HEIGHT;

        displayManager.setCursor(10, y);
        displayManager.setTextColor(TFT_GREEN); displayManager.printText("[y]");
        displayManager.setTextColor(0x7BEF);    displayManager.printText(" approve  ");
        displayManager.setTextColor(TFT_RED);   displayManager.printText("[n]");
        displayManager.setTextColor(0x7BEF);    displayManager.printText(" deny  ");
        displayManager.setTextColor(0x7BEF);    displayManager.println("[q] quit");
    } else {
        // Idle
        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF);
        displayManager.println(t.msg[0] ? t.msg : "No pending approvals");
        y += LINE_HEIGHT * 2;

        displayManager.setCursor(10, y);
        displayManager.setTextColor(0x7BEF); displayManager.println("[q] quit");
    }
}

// ── Command entry point ───────────────────────────────────────────────────────
void buddyCommand(char* args) {
    // Build BLE device name: "Claude-XXYY" or user-provided
    char btName[20];
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    if (args && *args)
        snprintf(btName, sizeof(btName), "%.16s", args);
    else
        snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);

    // Fresh random 6-digit passkey every session
    s_passkey = 100000 + (esp_random() % 900000);

    // Reset all state
    s_rxHead = 0; s_rxTail = 0;
    s_lineLen = 0;
    s_connected = false; s_secure = false;
    s_lastLiveMs = 0; s_lastBtByteMs = 0;
    TamaState tama = {};

    // NimBLE: init first (idempotent) so deinit is always safe regardless
    // of whether a previous command left the stack up or not
    NimBLEDevice::init("");
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    NimBLEDevice::init(btName);
    NimBLEDevice::setMTU(517);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new BuddySrvCb());

    NimBLEService* pSvc = s_server->createService(NUS_SVC_UUID);

    s_txChar = pSvc->createCharacteristic(NUS_TX_UUID,
                   NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    NimBLECharacteristic* rxChar = pSvc->createCharacteristic(NUS_RX_UUID,
                   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new BuddyRxCb());

    pSvc->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(NUS_SVC_UUID);
    pAdv->setScanResponse(true);
    NimBLEDevice::startAdvertising();

    displayManager.setBtActive(true);
    displayManager.updateStatusBar();
    drawBuddy(tama, s_passkey, false, btName);

    // Change-tracking to avoid unnecessary redraws
    bool    lastConn = false;
    char    lastPid[40] = "";
    uint8_t lastWait    = 255;
    uint16_t lastGen    = 0xFFFF;

    while (true) {
        dataPoll(&tama);

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if ((k == 'y' || k == 'Y') && tama.promptId[0] && s_connected) {
            sendPermission(tama.promptId, "once");
            tama.promptId[0] = 0;
            lastPid[0] = '\1';   // force redraw
        }
        if ((k == 'n' || k == 'N') && tama.promptId[0] && s_connected) {
            sendPermission(tama.promptId, "deny");
            tama.promptId[0] = 0;
            lastPid[0] = '\1';
        }

        bool changed = (s_connected          != lastConn)
                    || (strcmp(tama.promptId, lastPid) != 0)
                    || (tama.sessionsWaiting  != lastWait)
                    || (tama.lineGen          != lastGen);
        if (changed) {
            drawBuddy(tama, s_passkey, s_connected, btName);
            lastConn = s_connected;
            strncpy(lastPid, tama.promptId, sizeof(lastPid) - 1);
            lastPid[sizeof(lastPid) - 1] = 0;
            lastWait = tama.sessionsWaiting;
            lastGen  = tama.lineGen;
        }

        delay(100);
    }

    // Cleanup: tear down server, restore BLE for other commands
    s_server    = nullptr;
    s_txChar    = nullptr;
    s_connected = false;
    s_secure    = false;

    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    NimBLEDevice::init("T-REX");
    SD.begin(39);

    displayManager.setBtActive(false);
    displayManager.updateStatusBar();
    displayManager.printCommandScreen();
}
