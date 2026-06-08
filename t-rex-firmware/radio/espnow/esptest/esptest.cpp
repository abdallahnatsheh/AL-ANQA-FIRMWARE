// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "esptest.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

extern DisplayManager    displayManager;
extern InputHandling     inputHandler;

// ─── wire format (type byte shared with espsniff/espchat) ────────────────────
struct EtMsg {
    uint8_t type;   // 0x01 = chat
    uint8_t seq;
    char    text[48];
};

// ─── RX ring — two-pointer (wr advances in WiFi task, rd in main loop) ───────
#define ET_RX_MAX 16
struct EtRxEntry { uint8_t mac[6]; uint8_t seq; char text[50]; int8_t rssi; };
static EtRxEntry        s_rxRing[ET_RX_MAX];
static volatile uint8_t s_rxWr  = 0;   // next write slot (WiFi task)
static uint8_t          s_rxRd  = 0;   // next read  slot (main loop only)

#define RX_PENDING() ((uint8_t)(s_rxWr - s_rxRd))

// ─── display log ─────────────────────────────────────────────────────────────
#define ET_LOG_MAX 8
struct EtLogEntry { bool isTx; uint8_t mac[6]; int8_t rssi; char line[44]; };
static EtLogEntry s_log[ET_LOG_MAX];
static uint8_t    s_logWr   = 0;
static uint8_t    s_logFill = 0;   // capped at ET_LOG_MAX

// ─── session counters ─────────────────────────────────────────────────────────
static uint8_t          s_txSeq   = 0;
static uint32_t         s_txTotal = 0;
static volatile uint32_t s_rxTotal = 0;
static uint8_t          s_channel = 1;
static uint8_t          s_ownMAC[6];

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ─── helpers ──────────────────────────────────────────────────────────────────
static void shortMac(const uint8_t* m, char* out9) {
    snprintf(out9, 9, "%02X:%02X:%02X", m[3], m[4], m[5]);
}

static void logAppend(bool isTx, const uint8_t* mac, int8_t rssi, const char* line) {
    EtLogEntry& e = s_log[s_logWr % ET_LOG_MAX];
    e.isTx = isTx;
    if (mac) memcpy(e.mac, mac, 6); else memset(e.mac, 0, 6);
    e.rssi = rssi;
    strncpy(e.line, line, sizeof(e.line) - 1);
    e.line[sizeof(e.line) - 1] = '\0';
    s_logWr++;
    if (s_logFill < ET_LOG_MAX) s_logFill++;
}

// ─── recv callback (WiFi task) ────────────────────────────────────────────────
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    uint8_t slot = s_rxWr % ET_RX_MAX;
    EtRxEntry& e = s_rxRing[slot];
    memcpy(e.mac, mac, 6);
    e.rssi = 0;   // ESP-NOW API doesn't expose RSSI in recv_cb
    if (len >= (int)sizeof(EtMsg) && data[0] == 0x01) {
        const EtMsg* m = (const EtMsg*)data;
        e.seq = m->seq;
        strncpy(e.text, m->text, sizeof(e.text) - 1);
        e.text[sizeof(e.text) - 1] = '\0';
    } else {
        e.seq = 0;
        snprintf(e.text, sizeof(e.text), "(raw %d B)", len);
    }
    s_rxWr++;       // advance AFTER writing — main loop reads s_rxRd
    s_rxTotal++;
}

static void onSent(const uint8_t* mac, esp_now_send_status_t status) { (void)mac; (void)status; }

// ─── peer setup ───────────────────────────────────────────────────────────────
static void addBcastPeer() {
    esp_now_del_peer(BCAST);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void setChannel(uint8_t ch) {
    s_channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    addBcastPeer();
}

// ─── drain RX ring into display log (main loop only) ─────────────────────────
static bool drainRx() {
    bool got = false;
    while (RX_PENDING() > 0) {
        const EtRxEntry& e = s_rxRing[s_rxRd % ET_RX_MAX];
        char line[48];
        snprintf(line, sizeof(line), "s=%d %s", e.seq, e.text);
        logAppend(false, e.mac, e.rssi, line);
        s_rxRd++;
        got = true;
    }
    return got;
}

// ─── UI ───────────────────────────────────────────────────────────────────────
static void drawScreen() {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    // header
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("ESPTEST");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("TX+RX");
    dm.setTextColor(0x7BEF);     dm.printText("]  ch");
    char tmp[4]; snprintf(tmp, sizeof(tmp), "%d", s_channel);
    dm.setTextColor(TFT_WHITE);  dm.println(tmp);
    dm.printSeparator();

    // status line
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_ownMAC[0], s_ownMAC[1], s_ownMAC[2],
             s_ownMAC[3], s_ownMAC[4], s_ownMAC[5]);
    char status[52];
    snprintf(status, sizeof(status), "MAC:%s  TX:%lu  RX:%lu",
             mac, s_txTotal, (unsigned long)s_rxTotal);
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(0x4208);
    dm.println(status);

    // log (newest at bottom)
    int y = outputY + LINE_HEIGHT * 3;
    uint8_t start = (s_logFill < ET_LOG_MAX) ? 0 : (uint8_t)(s_logWr - ET_LOG_MAX);

    for (int i = 0; i < ET_LOG_MAX; i++) {
        dm.setCursor(4, y + LINE_HEIGHT * i);
        if (i >= (int)s_logFill) {
            dm.setTextColor(TFT_BLACK);
            dm.println("                                             ");
            continue;
        }
        const EtLogEntry& e = s_log[(start + i) % ET_LOG_MAX];
        char smac[9]; shortMac(e.mac, smac);
        char rowBuf[52];
        if (e.isTx) {
            snprintf(rowBuf, sizeof(rowBuf), "[TX->BCAST] %s", e.line);
            dm.setTextColor(TFT_CYAN);
        } else {
            snprintf(rowBuf, sizeof(rowBuf), "[RX %s] %s", smac, e.line);
            dm.setTextColor(TFT_GREEN);
        }
        dm.println(rowBuf);
    }

    // footer
    int footerY = outputY + LINE_HEIGHT * (3 + ET_LOG_MAX + 1);
    dm.setCursor(4, footerY);
    dm.setTextColor(0x4208);
    dm.println("[+/-] ch  [q] quit");
}

// ─── main entry point ─────────────────────────────────────────────────────────
void runEspTest(char* args) {
    int initCh = (args && *args) ? atoi(args) : 1;
    if (initCh < 1 || initCh > 13) initCh = 1;

    s_rxWr = s_rxRd = 0;
    s_rxTotal = 0;
    s_logWr = s_logFill = 0;
    s_txSeq = s_txTotal = 0;
    s_channel = (uint8_t)initCh;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    addBcastPeer();
    WiFi.macAddress(s_ownMAC);

    drawScreen();

    uint32_t lastTx     = 0;
    bool     needRedraw = false;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawScreen();
        }

        // drain RX ring → log
        if (drainRx()) needRedraw = true;

        // send broadcast every 2 s
        if (millis() - lastTx >= 2000) {
            lastTx = millis();
            EtMsg msg = {};
            msg.type = 0x01;
            msg.seq  = s_txSeq++;
            snprintf(msg.text, sizeof(msg.text), "hello from T-Rex #%d", msg.seq);
            esp_now_send(BCAST, (uint8_t*)&msg, sizeof(msg));
            s_txTotal++;
            char line[48];
            snprintf(line, sizeof(line), "#%d %s", msg.seq, msg.text);
            logAppend(true, nullptr, 0, line);
            needRedraw = true;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q') break;
        if (k == '+' || k == '=') { if (s_channel < 13) { setChannel(s_channel + 1); needRedraw = true; } }
        else if (k == '-')        { if (s_channel >  1) { setChannel(s_channel - 1); needRedraw = true; } }

        if (needRedraw && !displayManager.isBlocked()) {
            needRedraw = false;
            drawScreen();
        }
    }

    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_STA);

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_WHITE);
    char bye[56];
    snprintf(bye, sizeof(bye), "ESPTest done. TX:%lu  RX:%lu",
             s_txTotal, (unsigned long)s_rxTotal);
    displayManager.println(bye);
}
