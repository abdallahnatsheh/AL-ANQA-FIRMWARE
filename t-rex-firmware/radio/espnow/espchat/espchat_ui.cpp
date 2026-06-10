// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "espchat.h"
#include "espchat_core.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "utilities.h"
#include "notification_manager.h"
#include <WiFi.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── Screen layout constants ───────────────────────────────────────────────────
// outputY=38, LINE_HEIGHT=14
// Row 0  y=38  : header
// Row 1  y=52  : separator
// Row 2  y=66  : pair-req bar  (1 row, blank when inactive)
// Rows 3-9 y=80-164 : EC_VIS=7 message rows  (row 2 reserved for pair bar)
// Row 10 y=178 : separator before input
// Row 11 y=192 : footer controls
// Row 12 y=206 : input line "> text_"

static const int PAIR_Y  = outputY + LINE_HEIGHT * 2;   // y=66  pair-request notice
static const int MSG_Y0  = outputY + LINE_HEIGHT * 3;   // y=80  first message row
static const int SEP2_Y  = outputY + LINE_HEIGHT * 10;  // y=178 separator
static const int FOOT_Y  = outputY + LINE_HEIGHT * 11;  // y=192 footer hints
static const int INPUT_Y = outputY + LINE_HEIGHT * 12;  // y=206 input line

// Slider strip: right edge 4 px wide, spans the message area
static const int SLIDER_X = SCREEN_WIDTH - 4;           // x=316
static const int SLIDER_W = 4;

// ── Helpers ───────────────────────────────────────────────────────────────────
static bool parseMac(const char* str, uint8_t mac[6]) {
    unsigned int b[6] = {};
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
    return true;
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

// Header: mode tag, channel (flashes yellow after change), peer info, scroll indicator
static void drawChatHeader(bool isPublic, const uint8_t* peerMac,
                            int scroll, int unread, uint32_t chanFlashMs) {
    auto& dm = displayManager;
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText(isPublic ? "PUB" : "PRV");
    dm.setTextColor(0x7BEF);     dm.printText("]  ch");

    // Channel number — briefly yellow after a channel change
    char tmp[4]; snprintf(tmp, sizeof(tmp), "%d", g_ecChannel);
    bool flashing = (chanFlashMs && millis() - chanFlashMs < 1500);
    dm.setTextColor(flashing ? TFT_YELLOW : TFT_WHITE);
    dm.printText(tmp);
    dm.printText("  ");

    // Peer info: private → contact name or short MAC; public → own short MAC
    if (!isPublic && peerMac) {
        const EcContact* ct = ecFindContact(peerMac);
        if (ct && ct->name[0]) {
            dm.setTextColor(TFT_YELLOW);
            char buf[16]; snprintf(buf, sizeof(buf), "> %-10s", ct->name);
            dm.printText(buf);
        } else {
            dm.setTextColor(TFT_WHITE);
            char buf[12];
            snprintf(buf, sizeof(buf), ">%02X%02X%02X",
                     peerMac[3], peerMac[4], peerMac[5]);
            dm.printText(buf);
        }
    } else {
        dm.setTextColor(0x4208);
        char mac[13];
        snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
                 g_ecOwnMac[0], g_ecOwnMac[1], g_ecOwnMac[2],
                 g_ecOwnMac[3], g_ecOwnMac[4], g_ecOwnMac[5]);
        dm.printText(mac);
    }

    // Scroll / unread indicator — right-aligned at x=292
    if (unread > 0 || scroll > 0) {
        char ind[10];
        if (unread > 0) snprintf(ind, sizeof(ind), " +%dnew", min(unread, 99));
        else            snprintf(ind, sizeof(ind), " ^%d",    min(scroll, 99));
        int indX = SCREEN_WIDTH - 4 - (int)strlen(ind) * 6;
        dm.setCursor(max(indX, 200), outputY);
        dm.setTextColor(unread > 0 ? TFT_GREEN : TFT_CYAN);
        dm.printText(ind);
    }

    dm.setCursor(4, outputY + LINE_HEIGHT);
    dm.printSeparator();
}

// Pair-request notice bar (row 2, always drawn — blank when inactive)
static void drawPairBar(bool active, const uint8_t* pairMac, const char* pairName) {
    auto& dm = displayManager;
    dm.fillRect(0, PAIR_Y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    if (!active) return;
    dm.fillRect(0, PAIR_Y, SCREEN_WIDTH, LINE_HEIGHT, 0x8400);  // dark amber bg
    dm.setCursor(4, PAIR_Y);
    char who[11] = "???";
    if (pairName && pairName[0])        snprintf(who, sizeof(who), "%.10s", pairName);
    else if (pairMac)
        snprintf(who, sizeof(who), "%02X%02X%02X", pairMac[3], pairMac[4], pairMac[5]);
    char bar[54];
    snprintf(bar, sizeof(bar), "!! PAIR REQ: %-10s  [click] to respond", who);
    dm.setTextColor(TFT_YELLOW);
    dm.printText(bar);
}

// Scroll slider — right-edge 4 px strip over message area
static void drawScrollSlider(int scroll, int totalMsgs) {
    const int trackH = EC_VIS * LINE_HEIGHT;   // 7 * 14 = 98 px
    displayManager.fillRect(SLIDER_X, MSG_Y0, SLIDER_W, trackH, 0x1082);
    if (totalMsgs <= EC_VIS) return;           // everything visible, no thumb needed

    int maxScroll = totalMsgs - EC_VIS;
    int thumbH    = max(8, trackH * EC_VIS / totalMsgs);
    int thumbY    = MSG_Y0 + (trackH - thumbH) *
                    (maxScroll - constrain(scroll, 0, maxScroll)) / maxScroll;
    thumbY = constrain(thumbY, MSG_Y0, MSG_Y0 + trackH - thumbH);
    displayManager.fillRect(SLIDER_X, thumbY, SLIDER_W, thumbH,
                             scroll > 0 ? TFT_CYAN : 0x4208);
}

// ── Layout constants for message wrapping ────────────────────────────────────
// Row width: 316 px / 6 px = 52 chars. Slider at x=316 takes 4 px on right.
// TX:  "HH:MM [TX] " = 11 chars prefix  → TX_COLS=41 text chars per row
//      "           " = 11 spaces indent  →  continuation
// RX:  "HH:MM [Name ] " = 15 chars      → RX_COLS=37 text chars per row
//      "               " = 15 spaces     →  continuation
// SYS: "-- " + full-width text (no timestamp, grey)
#define TX_COLS   41
#define RX_COLS   37
#define RX_NAME_W  6

static int msgRows(const EcLogEntry& e) {
    if (e.name[0] == '!') return 1;  // system messages never wrap
    int first = e.isTx ? TX_COLS : RX_COLS;
    return ((int)strlen(e.text) > first) ? 2 : 1;
}

static void drawMessages(int scroll) {
    auto& dm = displayManager;
    int sc = scroll < 0 ? 0 : scroll;

    int endMsg   = (int)g_ecLogFill - 1 - sc;
    int startMsg = endMsg;
    int rowsUsed = 0;
    while (startMsg >= 0) {
        int raw  = (int)g_ecLogWr - (int)g_ecLogFill + startMsg;
        int slot = ((raw % EC_LOG_MAX) + EC_LOG_MAX) % EC_LOG_MAX;
        int r = msgRows(g_ecLog[slot]);
        if (rowsUsed + r > EC_VIS) break;
        rowsUsed += r;
        startMsg--;
    }
    startMsg++;

    int blankRows = EC_VIS - rowsUsed;
    int curRow    = 0;

    // Blank rows at top (fillRect avoids leftover artifacts)
    for (int r = 0; r < blankRows; r++) {
        dm.fillRect(0, MSG_Y0 + LINE_HEIGHT * curRow, SLIDER_X, LINE_HEIGHT, TFT_BLACK);
        curRow++;
    }

    for (int i = startMsg; i <= endMsg && curRow < EC_VIS; i++) {
        int raw  = (int)g_ecLogWr - (int)g_ecLogFill + i;
        int slot = ((raw % EC_LOG_MAX) + EC_LOG_MAX) % EC_LOG_MAX;
        const EcLogEntry& e = g_ecLog[slot];

        char r1[58] = {}, r2[58] = {};
        uint16_t col;

        if (e.name[0] == '!') {
            // System / notice line — grey, no timestamp bracket
            snprintf(r1, sizeof(r1), "-- %-49.49s", e.text);
            col = 0x4208;
        } else if (e.isTx) {
            snprintf(r1, sizeof(r1), "%.5s [TX] %-41.41s", e.ts, e.text);
            if ((int)strlen(e.text) > TX_COLS) {
                if ((int)strlen(e.text) > TX_COLS * 2)
                    snprintf(r2, sizeof(r2), "           %-38.38s...", e.text + TX_COLS);
                else
                    snprintf(r2, sizeof(r2), "           %-41.41s",   e.text + TX_COLS);
            }
            col = TFT_CYAN;
        } else {
            char nm[RX_NAME_W + 1];
            strncpy(nm, e.name[0] ? e.name : "?", RX_NAME_W); nm[RX_NAME_W] = '\0';
            snprintf(r1, sizeof(r1), "%.5s [%-6s] %-37.37s", e.ts, nm, e.text);
            if ((int)strlen(e.text) > RX_COLS) {
                if ((int)strlen(e.text) > RX_COLS * 2)
                    snprintf(r2, sizeof(r2), "               %-34.34s...", e.text + RX_COLS);
                else
                    snprintf(r2, sizeof(r2), "               %-37.37s",   e.text + RX_COLS);
            }
            // Known contact = yellow, unknown sender = green
            col = ecFindContact(e.mac) ? TFT_YELLOW : TFT_GREEN;
        }

        dm.fillRect(0, MSG_Y0 + LINE_HEIGHT * curRow, SLIDER_X, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(4, MSG_Y0 + LINE_HEIGHT * curRow);
        dm.setTextColor(col);
        dm.printText(r1);
        curRow++;

        if (r2[0] && curRow < EC_VIS) {
            dm.fillRect(0, MSG_Y0 + LINE_HEIGHT * curRow, SLIDER_X, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(4, MSG_Y0 + LINE_HEIGHT * curRow);
            dm.setTextColor(col);
            dm.printText(r2);
            curRow++;
        }
    }

    // Clear any leftover rows
    while (curRow < EC_VIS) {
        dm.fillRect(0, MSG_Y0 + LINE_HEIGHT * curRow, SLIDER_X, LINE_HEIGHT, TFT_BLACK);
        curRow++;
    }
}

// Input line: "> text_" + right-aligned character counter
static void drawInputLine(const char* text, int len) {
    if (displayManager.isBlocked()) return;
    auto& dm = displayManager;
    dm.fillRect(0, INPUT_Y, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
    dm.setCursor(4, INPUT_Y);
    dm.setTextColor(0x7BEF);
    dm.printText("> ");
    if (len > 44) {
        dm.setTextColor(0x4208); dm.printText("...");
        dm.setTextColor(TFT_WHITE); dm.printText(text + (len - 44));
    } else {
        dm.setTextColor(TFT_WHITE); dm.printText(text);
    }
    dm.setTextColor(TFT_YELLOW); dm.printText("_");

    // Character counter — only show when user has started typing
    if (len > 0) {
        int remaining = 100 - len;
        char cnt[6]; snprintf(cnt, sizeof(cnt), "%d", remaining);
        int cntX = SCREEN_WIDTH - 4 - (int)strlen(cnt) * 6;
        dm.setCursor(cntX, INPUT_Y);
        dm.setTextColor(remaining < 20 ? TFT_RED : 0x4208);
        dm.printText(cnt);
    }
}

// Footer: unread badge or normal controls
static void drawChatFooter(bool isPublic, bool hasPairReq, int unread) {
    auto& dm = displayManager;
    dm.fillRect(0, FOOT_Y, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
    dm.setCursor(4, FOOT_Y);
    if (unread > 0 && !hasPairReq) {
        char badge[12]; snprintf(badge, sizeof(badge), "[v%d new] ", min(unread, 99));
        dm.setTextColor(TFT_GREEN);  dm.printText(badge);
        dm.setTextColor(0x4208);     dm.printText("[+/-]ch  [hold]exit");
    } else if (!isPublic) {
        dm.setTextColor(0x4208); dm.printText("[+/-]ch  [trk]scroll  [hold trk]exit");
    } else if (hasPairReq) {
        dm.setTextColor(TFT_YELLOW); dm.printText("[click]respond  ");
        dm.setTextColor(0x4208);     dm.printText("[+/-]ch  [hold trk]exit");
    } else {
        dm.setTextColor(0x4208); dm.printText("[+/-]ch  [click]pair  [hold trk]exit");
    }
}

// Full screen redraw
static void drawChatScreen(bool isPublic, int scroll, bool hasPairReq,
                            int unread = 0,
                            const uint8_t* peerMac  = nullptr,
                            const uint8_t* pairMac  = nullptr,
                            const char*    pairName = nullptr,
                            uint32_t chanFlashMs    = 0) {
    if (displayManager.isBlocked()) return;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    drawChatHeader(isPublic, peerMac, scroll, unread, chanFlashMs);
    drawPairBar(hasPairReq, pairMac, pairName);
    drawMessages(scroll);
    drawScrollSlider(scroll, (int)g_ecLogFill);

    dm.setCursor(4, SEP2_Y);
    dm.printSeparator();
    drawChatFooter(isPublic, hasPairReq, unread);
}

// ── Mode picker ───────────────────────────────────────────────────────────────
// Returns: 1=public, 2=private, 0=cancel
static int drawModePicker() {
    if (displayManager.isBlocked()) return 0;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);   dm.println("]  Select mode");
    dm.printSeparator();

    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(TFT_WHITE); dm.println("  1  Public   broadcast ch1  (no encryption)");

    dm.setCursor(4, outputY + LINE_HEIGHT * 3);
    dm.setTextColor(TFT_WHITE); dm.println("  2  Private  1:1 encrypted  (PIN required)");

    dm.setCursor(4, outputY + LINE_HEIGHT * 5);
    dm.setTextColor(0x4208);    dm.println("  q  cancel");

    dm.setCursor(4, outputY + LINE_HEIGHT * 7);
    dm.setTextColor(0x4208);    dm.println("[1/2/q] or trackball click = public");

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) return 0;
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        tb = LockScreenManager::getInstance().interceptTrackball(tb);
        if (k == '1' || tb == TBALL_CLICK) return 1;
        if (k == '2') return 2;
        if (k == 'q' || k == '\x1B') return 0;
    }
}

// ── PIN entry prompt ──────────────────────────────────────────────────────────
// Fills pin[] (max pinMax-1 chars). Returns true if PIN entered, false if cancelled.
static bool enterPin(char* pin, int pinMax, const uint8_t* peerMac) {
    if (displayManager.isBlocked()) return false;
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);   dm.printText("::");
    dm.setTextColor(TFT_YELLOW);dm.println("PRV PAIR]");
    dm.printSeparator();

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             g_ecOwnMac[0], g_ecOwnMac[1], g_ecOwnMac[2],
             g_ecOwnMac[3], g_ecOwnMac[4], g_ecOwnMac[5]);
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(0x4208);    dm.printText("Own:  ");
    dm.setTextColor(TFT_WHITE); dm.println(macStr);

    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             peerMac[0], peerMac[1], peerMac[2],
             peerMac[3], peerMac[4], peerMac[5]);
    dm.setCursor(4, outputY + LINE_HEIGHT * 3);
    dm.setTextColor(0x4208);    dm.printText("Peer: ");
    dm.setTextColor(TFT_WHITE); dm.println(macStr);

    dm.setCursor(4, outputY + LINE_HEIGHT * 5);
    dm.setTextColor(TFT_WHITE); dm.println("Shared PIN (same on both devices):");

    dm.setCursor(4, outputY + LINE_HEIGHT * 8);
    dm.setTextColor(0x4208);    dm.println("[Enter]confirm  [Esc]cancel");

    pin[0] = '\0';
    int len = 0;

    // Draw initial input line
    auto redrawPin = [&]() {
        displayManager.fillRect(0, outputY + LINE_HEIGHT * 6,
                                SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
        displayManager.setCursor(4, outputY + LINE_HEIGHT * 6);
        displayManager.setTextColor(0x7BEF);     displayManager.printText("> ");
        // Show **** for security
        for (int i = 0; i < len; i++) displayManager.printText("*");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("_");
    };
    redrawPin();

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) return false;
        char k = inputHandler.getKeyboardInput();
        if (k == 0) continue;
        if (k == '\x1B' || (k == 'q' && len == 0)) return false;
        if (k == '\r' || k == '\n') {
            if (len > 0) return true;
        } else if (k == '\b' || k == 0x7F) {
            if (len > 0) { pin[--len] = '\0'; redrawPin(); }
        } else if (isPrintable(k) && k != 'q' && len < pinMax - 1) {
            pin[len++] = k;
            pin[len]   = '\0';
            redrawPin();
        }
    }
}

// ── Collect unique RX senders seen in current log ────────────────────────────
// Returns count; fills macs[][6] and names[][13] (max `max` entries)
static int uniqueSenders(uint8_t macs[][6], char names[][13], int max) {
    int count = 0;
    for (int i = 0; i < (int)g_ecLogFill && count < max; i++) {
        int raw  = (int)g_ecLogWr - (int)g_ecLogFill + i;
        int slot = ((raw % EC_LOG_MAX) + EC_LOG_MAX) % EC_LOG_MAX;
        const EcLogEntry& e = g_ecLog[slot];
        if (e.isTx) continue;
        if (e.name[0] == '!') continue;  // skip system entries
        // Check if already in list
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (memcmp(macs[j], e.mac, 6) == 0) { found = true; break; }
        }
        if (!found) {
            memcpy(macs[count], e.mac, 6);
            strncpy(names[count], e.name[0] ? e.name : "", 12);
            names[count][12] = '\0';
            count++;
        }
    }
    return count;
}

// ── Initiator: pick a sender, show PIN, send pair beacon ─────────────────────
// Returns true + fills peerMac/lmk if pairing confirmed, false if cancelled.
static bool doPairInitiate(uint8_t peerMac[6], uint8_t lmk[16]) {
    auto& dm = displayManager;

    // Collect unique senders (max 5)
    uint8_t smacs[5][6];
    char    snames[5][13];
    int     count = uniqueSenders(smacs, snames, 5);

    // Also check if there is a pending pair request we should show first
    // (handled separately in the main loop, so we only show senders here)

    if (count == 0) {
        dm.clearScreen(); dm.setDefaultTextSize();
        dm.setCursor(4, outputY);
        dm.setTextColor(TFT_YELLOW);
        dm.println("No senders seen yet — chat first.");
        uint32_t t0 = millis();
        while (millis() - t0 < 1500) inputHandler.getKeyboardInput();
        return false;
    }

    // ── Sender picker ─────────────────────────────────────────────────────────
    dm.clearScreen(); dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);   dm.println("::PAIR REQUEST]");
    dm.printSeparator();

    for (int i = 0; i < count; i++) {
        dm.setCursor(4, outputY + LINE_HEIGHT * (2 + i));
        dm.setTextColor(TFT_WHITE);
        char row[54];
        char smac[7]; snprintf(smac, sizeof(smac), "%02X%02X%02X",
                               smacs[i][3], smacs[i][4], smacs[i][5]);
        const EcContact* ct = ecFindContact(smacs[i]);
        const char* dname = (ct && ct->name[0]) ? ct->name
                          : (snames[i][0]       ? snames[i] : smac);
        snprintf(row, sizeof(row), "  %d  %-12s  (%s)", i + 1, dname, smac);
        dm.println(row);
    }
    dm.setCursor(4, outputY + LINE_HEIGHT * (2 + count + 1));
    dm.setTextColor(0x4208); dm.println("  q  cancel");
    dm.setCursor(4, outputY + LINE_HEIGHT * (2 + count + 3));
    dm.setTextColor(0x4208); dm.println("Select:");

    int chosen = -1;
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) return false;
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == '\x1B') return false;
        int n = k - '0';
        if (n >= 1 && n <= count) { chosen = n - 1; break; }
    }
    memcpy(peerMac, smacs[chosen], 6);

    // ── Show PIN + send beacon ─────────────────────────────────────────────────
    // Generate 4-digit random PIN — derive LMK immediately so we can register
    // the receiver as an encrypted peer BEFORE they send their confirmation.
    // ESP-NOW will only deliver the "* pair ok" frame if both LMKs match.
    uint32_t rnd = esp_random();
    uint16_t pinNum = 1000 + (rnd % 9000);   // 1000-9999
    char pinStr[8]; snprintf(pinStr, sizeof(pinStr), "%04u", pinNum);
    ecDeriveLmk(pinStr, g_ecOwnMac, peerMac, lmk);
    ecAddEncryptedPeer(peerMac, lmk);

    dm.clearScreen(); dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);    dm.println("::PAIR REQUEST]");
    dm.printSeparator();

    char mac6[18];
    snprintf(mac6, sizeof(mac6), "%02X:%02X:%02X:%02X:%02X:%02X",
             peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(0x4208);    dm.printText("To: ");
    dm.setTextColor(TFT_WHITE); dm.println(mac6);

    dm.setCursor(4, outputY + LINE_HEIGHT * 4);
    dm.setTextColor(TFT_WHITE); dm.println("Share this PIN with them:");

    // Big PIN display (text size 3 = 18×24px per char)
    dm.setTextSize(3);
    int pinX = (SCREEN_WIDTH - 4 * 18) / 2;
    dm.setCursor(pinX, outputY + LINE_HEIGHT * 5 + 4);
    dm.setTextColor(TFT_YELLOW);
    dm.printText(pinStr);
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY + LINE_HEIGHT * 9);
    dm.setTextColor(TFT_CYAN); dm.println("Sending pair request...");
    dm.setCursor(4, outputY + LINE_HEIGHT * 10);
    dm.setTextColor(0x4208);   dm.println("[Enter] proceed  [Esc] cancel");

    // Send pair beacon every 2s.
    // "* pair ok" arrives ONLY if receiver derived the same LMK (matching PIN).
    // Wrong PIN = ESP-NOW drops the encrypted frame = peerConfirmed stays false.
    uint32_t lastBeacon = 0;
    bool     peerConfirmed = false;
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) return false;
        uint32_t now = millis();
        if (now - lastBeacon >= 2000) {
            ecSendPairReq(peerMac);
            lastBeacon = now;
        }
        while (EC_RX_PENDING() > 0) {
            uint8_t slot = g_ecRxRd % EC_RX_MAX;
            const EcRxEntry& r = g_ecRxRing[slot];
            if (memcmp(r.mac, peerMac, 6) == 0 && !peerConfirmed) {
                // Send ack encrypted back — receiver's wait loop checks for this.
                // Only arrives at receiver if both LMKs matched (ESP-NOW validates).
                ecSendToMac(peerMac, "* pin ack");
                peerConfirmed = true;
                dm.setCursor(4, outputY + LINE_HEIGHT * 11);
                dm.setTextColor(TFT_GREEN);
                dm.println("PIN confirmed!  [Enter] to chat");
            }
            g_ecRxRd++;
        }
        char k = inputHandler.getKeyboardInput();
        if (k == '\x1B') return false;
        if (k == '\r' || k == '\n') {
            if (!peerConfirmed) {
                // Warn but allow — could be range issue, not necessarily wrong PIN
                dm.setCursor(4, outputY + LINE_HEIGHT * 11);
                dm.setTextColor(TFT_YELLOW);
                dm.println("No confirmation yet — proceed anyway?");
                dm.setCursor(4, outputY + LINE_HEIGHT * 12);
                dm.setTextColor(0x4208);
                dm.println("[Enter]=yes  [Esc]=cancel");
                while (true) {
                    char c = inputHandler.getKeyboardInput();
                    if (c == '\r' || c == '\n') break;
                    if (c == '\x1B') return false;
                }
            }
            break;
        }
    }
    // LMK already derived above — lmk[] is filled
    return true;
}

// ── Receiver: accept an incoming pair request, enter PIN ─────────────────────
// Protocol:
//   1. Receiver types PIN → derives LMK → registers initiator encrypted → sends "* pair ok"
//   2. Initiator's doPairInitiate receives it (only if LMK matched) → replies "* pin ack"
//   3. Receiver waits 4 s for "* pin ack" — arrived = correct; timeout = wrong PIN
//   4. Up to 3 attempts; all wrong → remove device from contacts, return false
static bool doPairAccept(const uint8_t* requesterMac, const char* requesterName,
                          uint8_t lmk[16]) {
    auto& dm = displayManager;
    dm.clearScreen(); dm.setDefaultTextSize();

    // ── Static header (drawn once for all attempts) ───────────────────────────
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);    dm.println("::ACCEPT PAIR]");
    dm.printSeparator();

    char mac6[18];
    snprintf(mac6, sizeof(mac6), "%02X:%02X:%02X:%02X:%02X:%02X",
             requesterMac[0], requesterMac[1], requesterMac[2],
             requesterMac[3], requesterMac[4], requesterMac[5]);
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(0x4208);    dm.printText("From: ");
    dm.setTextColor(TFT_WHITE);
    char who[28]; snprintf(who, sizeof(who), "%s  (%02X%02X%02X)",
                           requesterName[0] ? requesterName : "???",
                           requesterMac[3], requesterMac[4], requesterMac[5]);
    dm.println(who);

    dm.setCursor(4, outputY + LINE_HEIGHT * 3);
    dm.setTextColor(TFT_WHITE); dm.println("Enter their PIN:");

    dm.setCursor(4, outputY + LINE_HEIGHT * 7);
    dm.setTextColor(0x4208);   dm.println("[Enter]confirm  [Esc/q]cancel");

    // ── Up to 3 attempts ──────────────────────────────────────────────────────
    const int MAX_ATT = 3;
    for (int attempt = 1; attempt <= MAX_ATT; attempt++) {
        // Attempt counter row (row 4)
        dm.fillRect(0, outputY + LINE_HEIGHT * 4, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(4, outputY + LINE_HEIGHT * 4);
        dm.setTextColor(attempt > 1 ? TFT_YELLOW : 0x4208);
        char att[24]; snprintf(att, sizeof(att), "Attempt %d/%d:", attempt, MAX_ATT);
        dm.println(att);

        // PIN input (row 5), status (row 6)
        char pin[8] = {};
        int  len     = 0;
        dm.fillRect(0, outputY + LINE_HEIGHT * 6, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);

        auto redrawPin = [&]() {
            displayManager.fillRect(0, outputY + LINE_HEIGHT * 5, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
            displayManager.setCursor(4, outputY + LINE_HEIGHT * 5);
            displayManager.setTextColor(0x7BEF);    displayManager.printText("> ");
            displayManager.setTextColor(TFT_WHITE); displayManager.printText(pin);
            displayManager.setTextColor(TFT_YELLOW); displayManager.printText("_");
        };
        redrawPin();

        // Read PIN
        bool cancelled = false;
        while (true) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) { cancelled = true; break; }
            char k = inputHandler.getKeyboardInput();
            if (k == 0) continue;
            if (k == '\x1B' || (k == 'q' && len == 0)) { cancelled = true; break; }
            if (k == '\r' || k == '\n') { if (len > 0) break; }
            else if ((k == '\b' || k == 0x7F) && len > 0) { pin[--len] = '\0'; redrawPin(); }
            else if (isPrintable(k) && k != 'q' && len < 7) { pin[len++] = k; pin[len] = '\0'; redrawPin(); }
        }
        if (cancelled) return false;

        // Derive LMK, register initiator as encrypted peer, fire "* pair ok"
        ecDeriveLmk(pin, g_ecOwnMac, requesterMac, lmk);
        ecAddEncryptedPeer(requesterMac, lmk);
        ecSendToMac(requesterMac, "* pair ok");

        // Show "Waiting..." (row 6)
        dm.fillRect(0, outputY + LINE_HEIGHT * 6, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(4, outputY + LINE_HEIGHT * 6);
        dm.setTextColor(TFT_CYAN); dm.println("Waiting for confirmation...");

        // Wait up to 4 s for "* pin ack" from initiator
        uint32_t waitStart = millis();
        bool gotAck = false;
        while (millis() - waitStart < 4000) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) return false;
            inputHandler.getKeyboardInput();
            while (EC_RX_PENDING() > 0) {
                const EcRxEntry& r = g_ecRxRing[g_ecRxRd % EC_RX_MAX];
                if (memcmp(r.mac, requesterMac, 6) == 0 &&
                    strncmp(r.text, "* pin ack", 9) == 0) {
                    gotAck = true;
                }
                g_ecRxRd++;
            }
            if (gotAck) break;
            char k = inputHandler.getKeyboardInput();
            if (k == '\x1B') return false;
        }

        if (gotAck) {
            // Show brief success feedback before handing off to the chat
            dm.fillRect(0, outputY + LINE_HEIGHT * 6, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
            dm.setCursor(4, outputY + LINE_HEIGHT * 6);
            dm.setTextColor(TFT_GREEN); dm.println("PIN correct!  Connecting...");
            uint32_t t0 = millis();
            while (millis() - t0 < 800) inputHandler.getKeyboardInput();
            return true;
        }

        // Timeout — wrong PIN
        dm.fillRect(0, outputY + LINE_HEIGHT * 6, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(4, outputY + LINE_HEIGHT * 6);
        dm.setTextColor(TFT_RED);
        if (attempt < MAX_ATT) {
            char err[40]; snprintf(err, sizeof(err), "Wrong PIN  (%d/%d) — try again", attempt, MAX_ATT);
            dm.println(err);
            uint32_t t0 = millis();
            while (millis() - t0 < 1200) inputHandler.getKeyboardInput();
        }
    }

    // ── All 3 attempts failed ─────────────────────────────────────────────────
    if (sdCardManager.canAccessSD()) ecRemoveContact(requesterMac);

    dm.clearScreen(); dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);   dm.println("::PAIR FAILED]");
    dm.printSeparator();
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(TFT_RED);    dm.println("3/3 wrong PIN.");
    dm.setCursor(4, outputY + LINE_HEIGHT * 3);
    dm.setTextColor(TFT_YELLOW); dm.println("Device removed from contacts.");
    uint32_t t0 = millis();
    while (millis() - t0 < 2500) inputHandler.getKeyboardInput();
    return false;
}

// ── Save-contact prompt (shown after a successful private pairing) ─────────────
// Returns contact name the user typed (may be empty = skip save)
static void promptSaveContact(const uint8_t* peerMac, uint8_t ch, const uint8_t* lmk) {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("ESPCHAT");
    dm.setTextColor(0x7BEF);    dm.println("::SAVE CONTACT]");
    dm.printSeparator();

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
    dm.setCursor(4, outputY + LINE_HEIGHT * 2);
    dm.setTextColor(0x4208);   dm.printText("Peer: ");
    dm.setTextColor(TFT_WHITE); dm.println(macStr);

    dm.setCursor(4, outputY + LINE_HEIGHT * 4);
    dm.setTextColor(TFT_WHITE); dm.println("Save as contact? Enter name:");
    dm.setCursor(4, outputY + LINE_HEIGHT * 6);
    dm.setTextColor(0x4208);   dm.println("[Enter]skip  [Esc]skip");

    char name[13] = {};
    int  len      = 0;

    auto redraw = [&]() {
        displayManager.fillRect(0, outputY + LINE_HEIGHT * 5,
                                SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
        displayManager.setCursor(4, outputY + LINE_HEIGHT * 5);
        displayManager.setTextColor(0x7BEF);    displayManager.printText("> ");
        displayManager.setTextColor(TFT_WHITE); displayManager.printText(name);
        displayManager.setTextColor(TFT_YELLOW);displayManager.printText("_");
    };
    redraw();

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 0) continue;
        if (k == '\x1B' || (k == 'q' && len == 0)) return;   // skip
        if (k == '\r' || k == '\n') {
            if (len > 0) {
                bool saved = ecSaveContact(peerMac, name, ch, lmk);
                if (!saved) {
                    // No SD — save to in-memory contacts so the name is usable
                    // this session and the LMK stays available for re-pairing.
                    bool found = false;
                    for (int i = 0; i < g_ecContactCount; i++) {
                        if (memcmp(g_ecContacts[i].mac, peerMac, 6) == 0) {
                            strncpy(g_ecContacts[i].name, name, 12);
                            g_ecContacts[i].name[12] = '\0';
                            g_ecContacts[i].channel = ch;
                            if (lmk) memcpy(g_ecContacts[i].lmk, lmk, 16);
                            found = true; break;
                        }
                    }
                    if (!found && g_ecContactCount < EC_CONTACT_MAX) {
                        EcContact& c = g_ecContacts[g_ecContactCount++];
                        memcpy(c.mac, peerMac, 6);
                        strncpy(c.name, name, 12); c.name[12] = '\0';
                        c.channel = ch;
                        if (lmk) memcpy(c.lmk, lmk, 16);
                        else memset(c.lmk, 0, 16);
                    }
                    // Warn user — contact lives in RAM only until reboot
                    displayManager.fillRect(0, outputY + LINE_HEIGHT * 7,
                                            SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                    displayManager.setCursor(4, outputY + LINE_HEIGHT * 7);
                    displayManager.setTextColor(TFT_YELLOW);
                    displayManager.println("No SD — active this session only");
                    uint32_t t0 = millis();
                    while (millis() - t0 < 1500) inputHandler.getKeyboardInput();
                }
            }
            return;
        }
        if ((k == '\b' || k == 0x7F) && len > 0) {
            name[--len] = '\0'; redraw();
        } else if (isPrintable(k) && len < 12) {
            name[len++] = k; name[len] = '\0'; redraw();
        }
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────
void runEspchat(char* args) {
    char buf[128] = {};
    if (args && *args) strncpy(buf, args, sizeof(buf) - 1);

    char* tok = strtok(buf, " ");

    // ── stop ──────────────────────────────────────────────────────────────────
    if (tok && strcmp(tok, "stop") == 0) {
        stopEspchatBg();
        return;
    }

    // ── bg [ch] — background mode, channel auto-detected from contacts/config ──
    if (tok && strcmp(tok, "bg") == 0) {
        char* chStr = strtok(nullptr, " ");
        int ch = 0;   // 0 = auto (ecAutoChannel inside startEspchatBg)
        if (chStr) { ch = atoi(chStr); if (ch < 1 || ch > 13) ch = 0; }
        startEspchatBg((uint8_t)ch);
        return;
    }

    // ── Determine foreground mode ──────────────────────────────────────────────
    bool isPublic = true;
    int  initCh   = 1;
    uint8_t peerMac[6] = {};
    bool    hasPeer    = false;
    char    pin[48]    = {};

    if (!tok) {
        // No args — show mode picker
        int choice = drawModePicker();
        if (choice == 0) { displayManager.printCommandScreen(); return; }
        isPublic = (choice == 1);
        initCh   = 0;   // not specified → will use stored public channel
        if (!isPublic) {
            // Private mode from picker:
            // 1) show saved contacts (if any)  2) option to enter MAC manually
            ecLoadContacts();
            auto& dm = displayManager;
            bool  pickedContact = false;

            if (g_ecContactCount > 0) {
                dm.clearScreen(); dm.setDefaultTextSize();
                dm.setCursor(4, outputY);
                dm.setTextColor(0x7BEF);   dm.printText("[");
                dm.setTextColor(TFT_CYAN); dm.printText("ESPCHAT");
                dm.setTextColor(0x7BEF);   dm.println("::PRV SELECT]");
                dm.printSeparator();

                int show = min((int)g_ecContactCount, 5);
                for (int i = 0; i < show; i++) {
                    dm.setCursor(4, outputY + LINE_HEIGHT * (2 + i));
                    dm.setTextColor(TFT_WHITE);
                    char row[54];
                    char smac[7]; snprintf(smac, sizeof(smac), "%02X%02X%02X",
                                           g_ecContacts[i].mac[3],
                                           g_ecContacts[i].mac[4],
                                           g_ecContacts[i].mac[5]);
                    snprintf(row, sizeof(row), "  %d  %-12s  (%s)",
                             i + 1, g_ecContacts[i].name, smac);
                    dm.println(row);
                }
                dm.setCursor(4, outputY + LINE_HEIGHT * (2 + show + 1));
                dm.setTextColor(TFT_YELLOW); dm.println("  +  New (enter MAC)");
                dm.setCursor(4, outputY + LINE_HEIGHT * (2 + show + 2));
                dm.setTextColor(0x4208);    dm.println("  q  cancel");

                while (true) {
                    if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                        dm.printCommandScreen(); return;
                    }
                    char k = inputHandler.getKeyboardInput();
                    if (k == 'q' || k == '\x1B') { dm.printCommandScreen(); return; }
                    if (k == '+') break;  // fall through to MAC input
                    int n = k - '0';
                    if (n >= 1 && n <= show) {
                        memcpy(peerMac, g_ecContacts[n - 1].mac, 6);
                        hasPeer = true; pickedContact = true;
                        break;
                    }
                }
            }

            // If no contact picked (new or no contacts) — show MAC input
            if (!pickedContact) {
                dm.clearScreen(); dm.setDefaultTextSize();
                dm.setCursor(4, outputY);
                dm.setTextColor(TFT_CYAN); dm.println("[ESPCHAT::PRV NEW]");
                dm.printSeparator();
                dm.setCursor(4, outputY + LINE_HEIGHT * 2);
                dm.setTextColor(TFT_WHITE);
                dm.println("Peer MAC  (XX:XX:XX:XX:XX:XX):");
                dm.setCursor(4, outputY + LINE_HEIGHT * 4);
                dm.setTextColor(0x4208);
                dm.println("[Enter]confirm  [q empty]=cancel");

                char macBuf[20] = {}; int macLen = 0;
                auto redrawMac = [&]() {
                    displayManager.fillRect(0, outputY + LINE_HEIGHT * 3,
                                            SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
                    displayManager.setCursor(4, outputY + LINE_HEIGHT * 3);
                    displayManager.setTextColor(0x7BEF); displayManager.printText("> ");
                    displayManager.setTextColor(TFT_WHITE); displayManager.printText(macBuf);
                    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("_");
                };
                redrawMac();
                while (true) {
                    char k = inputHandler.getKeyboardInput();
                    if (k == 0) continue;
                    if (k == '\x1B' || (k == 'q' && macLen == 0)) {
                        dm.printCommandScreen(); return;
                    }
                    if (k == '\r' || k == '\n') {
                        if (parseMac(macBuf, peerMac)) { hasPeer = true; break; }
                        dm.setCursor(4, outputY + LINE_HEIGHT * 5);
                        dm.setTextColor(TFT_RED);
                        dm.println("Invalid MAC — use XX:XX:XX:XX:XX:XX");
                    } else if ((k == '\b' || k == 0x7F) && macLen > 0) {
                        macBuf[--macLen] = '\0'; redrawMac();
                    } else if (isPrintable(k) && macLen < 17) {
                        macBuf[macLen++] = k; macBuf[macLen] = '\0'; redrawMac();
                    }
                }
            }
            if (!hasPeer) { dm.printCommandScreen(); return; }
        }
    } else if (strcmp(tok, "pub") == 0) {
        // ec pub [set <ch>]  |  ec pub [ch]
        char* sub = strtok(nullptr, " ");
        if (sub && strcmp(sub, "set") == 0) {
            // ── pub set [ch] — save/show default public channel ────────────────
            char* chStr = strtok(nullptr, " ");
            if (!chStr) {
                if (sdCardManager.canAccessSD()) ecLoadConfig();
                displayManager.setTextColor(TFT_WHITE);
                displayManager.printText("Public channel: ");
                char tmp[4]; snprintf(tmp, sizeof(tmp), "%d", g_ecPublicChannel);
                displayManager.println(tmp);
                displayManager.printCommandScreen();
                return;
            }
            int ch = atoi(chStr);
            if (ch < 1 || ch > 13) {
                displayManager.println("Channel must be 1-13");
                displayManager.printCommandScreen();
                return;
            }
            g_ecPublicChannel = (uint8_t)ch;
            if (sdCardManager.canAccessSD()) {
                sdCardManager.ensureDir(SD_DIR_ESPCHAT);
                ecSaveConfig();
                displayManager.setTextColor(TFT_CYAN);
                displayManager.printText("Public channel set to ");
                char tmp[4]; snprintf(tmp, sizeof(tmp), "%d", ch);
                displayManager.println(tmp);
            } else {
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("No SD — active this session only");
            }
            displayManager.printCommandScreen();
            return;
        }
        // ec pub [ch] — sub holds the channel number (if provided)
        isPublic = true;
        bool chExplicit = (sub != nullptr);
        if (sub) { initCh = atoi(sub); if (initCh < 1 || initCh > 13) initCh = 1; }
        // Store chExplicit so the "use stored public channel" logic below doesn't
        // override an explicit channel the user typed (even if they typed 1)
        if (!chExplicit) initCh = 0;  // sentinel: 0 = "not specified"
    } else if (strcmp(tok, "prv") == 0) {
        isPublic = false;
        char* macStr = strtok(nullptr, " ");
        if (!macStr || !parseMac(macStr, peerMac)) {
            displayManager.println("Usage: ec prv <XX:XX:XX:XX:XX:XX>");
            displayManager.printCommandScreen();
            return;
        }
        hasPeer = true;
    } else {
        int tryN = atoi(tok);
        if (tryN >= 1 && tryN <= 13) {
            isPublic = true; initCh = tryN;  // explicit channel — no override
        } else {
            displayManager.println("Usage: ec [pub|prv|bg|stop] [ch]");
            displayManager.printCommandScreen();
            return;
        }
    }

    // ── Load config + contacts for public foreground ──────────────────────────
    if (isPublic) {
        if (sdCardManager.canAccessSD()) ecLoadConfig();
        ecLoadContacts();
        // initCh == 0 means user didn't specify → use stored public channel
        // initCh > 0 means explicitly typed → respect it
        if (initCh == 0) initCh = (int)g_ecPublicChannel;
        if (initCh < 1 || initCh > 13) initCh = 1;  // final safety clamp
    }

    // ── PIN entry for private mode ─────────────────────────────────────────────
    uint8_t derivedLmk[16] = {};
    if (!isPublic && hasPeer) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false);
        WiFi.macAddress(g_ecOwnMac);  // populate own MAC for PIN prompt display
        if (!enterPin(pin, sizeof(pin), peerMac)) {
            displayManager.clearScreen();
            displayManager.printCommandScreen();
            return;
        }
        // Derive LMK now so we can save the contact before starting chat
        ecDeriveLmk(pin, g_ecOwnMac, peerMac, derivedLmk);
        // Offer to save as contact (user can press Enter to skip)
        promptSaveContact(peerMac, (uint8_t)initCh, derivedLmk);
    }

    // Safety clamp — initCh must be 1-13 before any WiFi/SD call
    if (initCh < 1 || initCh > 13) initCh = 1;

    // ── Open SD log (per-channel for public, per-contact for private) ─────────
    if (sdCardManager.canAccessSD())
        ecSdLogOpen((uint8_t)initCh, (!isPublic && hasPeer) ? peerMac : nullptr);

    // ── Init ESP-NOW ──────────────────────────────────────────────────────────
    if (!ecCoreInit((uint8_t)initCh, !isPublic,
                    hasPeer ? peerMac : nullptr,
                    pin[0]  ? pin     : nullptr)) {
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("ESP-NOW init failed");
        ecSdLogClose();
        delay(1500);
        displayManager.printCommandScreen();
        return;
    }

    // ── Load message history from SD ──────────────────────────────────────────
    if (sdCardManager.canAccessSD())
        ecSdLogLoad((uint8_t)initCh, (!isPublic && hasPeer) ? peerMac : nullptr);

    // ── Notify on incoming message — private contact = INFO, public = PING ──────
    auto notifyRx = []() {
        if (g_ecLogFill == 0) return;
        int slot = ((int)g_ecLogWr - 1 + EC_LOG_MAX) % EC_LOG_MAX;
        const EcLogEntry& last = g_ecLog[slot];
        if (last.isTx) return;   // only play for received messages
        bool isKnown = (ecFindContact(last.mac) != nullptr);
        NotificationManager::getInstance().notify(isKnown ? NOTIF_INFO : NOTIF_PING);
    };

    // ── Main foreground loop ───────────────────────────────────────────────────
    char inputBuf[101] = {};
    int  inputLen      = 0;
    int  s_scroll      = 0;
    bool needRedraw    = true;
    bool hasPairReq    = false;
    uint8_t pairReqMac[6]   = {};
    char    pairReqName[13] = {};
    bool    goPrivate    = false;
    uint8_t privPeer[6]  = {};
    uint8_t privLmk[16]  = {};
    uint32_t s_holdStart    = 0;
    uint32_t s_chanFlashMs  = 0;
    int      s_unread       = 0;

    drawChatScreen(isPublic, s_scroll, hasPairReq, s_unread,
                   isPublic ? nullptr : peerMac, nullptr, nullptr, s_chanFlashMs);
    drawInputLine(inputBuf, inputLen);

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) needRedraw = true;

        // Long-press trackball (1.5 s) → exit
        {
            bool tpadDown = (digitalRead(BOARD_BOOT_PIN) == LOW);
            if (tpadDown) {
                if (s_holdStart == 0) {
                    s_holdStart = millis();
                    inputHandler.clearPendingClicks();
                } else if (millis() - s_holdStart >= 1500) {
                    inputHandler.clearPendingClicks();
                    break;
                }
            } else {
                s_holdStart = 0;
            }
        }

        if (ecDrainRx()) {
            notifyRx();
            if (s_scroll > 0) s_unread++;   // user scrolled back — badge new messages
            else               s_scroll = 0; // already at bottom, stay there
            needRedraw = true;
        }

        // Drain incoming pair-request ring (public mode only)
        if (isPublic && !hasPairReq && EC_PAIR_PENDING() > 0) {
            ecDrainPairReq(pairReqMac, pairReqName);
            hasPairReq = true;
            NotificationManager::getInstance().notify(NOTIF_WARNING);
            needRedraw = true;
        }

        if (needRedraw && !displayManager.isBlocked()) {
            needRedraw = false;
            drawChatScreen(isPublic, s_scroll, hasPairReq, s_unread,
                           isPublic ? nullptr : peerMac,
                           hasPairReq ? pairReqMac  : nullptr,
                           hasPairReq ? pairReqName : nullptr,
                           s_chanFlashMs);
            drawInputLine(inputBuf, inputLen);
        }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        tb = LockScreenManager::getInstance().interceptTrackball(tb);

        if (tb == TBALL_UP)   { s_scroll++; needRedraw = true; }
        if (tb == TBALL_DOWN) {
            if (s_scroll > 0) { s_scroll--; needRedraw = true; }
            if (s_scroll == 0) { s_unread = 0; needRedraw = true; }
        }
        if (tb == TBALL_CLICK && s_holdStart == 0) {
            if (s_scroll > 0) {
                s_scroll = 0; s_unread = 0; needRedraw = true;
            } else if (isPublic) {
                if (hasPairReq) {
                    if (doPairAccept(pairReqMac, pairReqName, privLmk)) {
                        memcpy(privPeer, pairReqMac, 6);
                        goPrivate = true; hasPairReq = false;
                        break;
                    }
                } else {
                    if (doPairInitiate(privPeer, privLmk)) {
                        goPrivate = true;
                        break;
                    }
                }
                needRedraw = true;
            }
        }

        if (k == 0) continue;
        if (k == '\x1B') break;

        if (k == '\r' || k == '\n') {
            if (inputLen > 0) {
                ecSendMessage(inputBuf);
                inputBuf[0] = '\0'; inputLen = 0;
                s_scroll = 0; s_unread = 0; needRedraw = true;
            }
        } else if (k == '\b' || k == 0x7F) {
            if (inputLen > 0) { inputBuf[--inputLen] = '\0'; drawInputLine(inputBuf, inputLen); }
        } else if (k == '+' || k == '=') {
            if (g_ecChannel < 13) { ecSetChannel(g_ecChannel + 1); s_chanFlashMs = millis(); needRedraw = true; }
        } else if (k == '-') {
            if (g_ecChannel > 1)  { ecSetChannel(g_ecChannel - 1); s_chanFlashMs = millis(); needRedraw = true; }
        } else if (isPrintable(k) && inputLen < (int)sizeof(inputBuf) - 1) {
            inputBuf[inputLen++] = k;
            inputBuf[inputLen]   = '\0';
            drawInputLine(inputBuf, inputLen);
        }
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    ecCoreDeinit();
    ecSdLogClose();

    // ── Transition to private chat after pairing ─────────────────────────────
    if (goPrivate) {
        // Always prompt — works with or without SD (RAM-only fallback inside)
        promptSaveContact(privPeer, g_ecChannel, privLmk);
    }
    if (goPrivate && ecCoreInitWithLmk(g_ecChannel, privPeer, privLmk)) {
        if (sdCardManager.canAccessSD())
            ecSdLogOpen(g_ecChannel, privPeer);
        // Load any existing private chat history
        if (sdCardManager.canAccessSD())
            ecSdLogLoad(g_ecChannel, privPeer);

        // ── Private chat mini-loop ────────────────────────────────────────────
        char pbuf[101] = {}; int plen = 0, pscroll = 0; bool pRedraw = true;
        uint32_t p_holdStart   = 0;
        uint32_t p_chanFlashMs = 0;
        int      p_unread      = 0;
        drawChatScreen(false, pscroll, false, p_unread, privPeer, nullptr, nullptr, p_chanFlashMs);
        drawInputLine(pbuf, plen);

        while (true) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) pRedraw = true;

            // Long-press trackball (1.5 s) → exit
            {
                bool tpadDown = (digitalRead(BOARD_BOOT_PIN) == LOW);
                if (tpadDown) {
                    if (p_holdStart == 0) {
                        p_holdStart = millis();
                        inputHandler.clearPendingClicks();
                    } else if (millis() - p_holdStart >= 1500) {
                        inputHandler.clearPendingClicks();
                        break;
                    }
                } else {
                    p_holdStart = 0;
                }
            }

            if (ecDrainRx()) {
                notifyRx();
                if (pscroll > 0) p_unread++; else pscroll = 0;
                pRedraw = true;
            }
            if (pRedraw && !displayManager.isBlocked()) {
                pRedraw = false;
                drawChatScreen(false, pscroll, false, p_unread,
                               privPeer, nullptr, nullptr, p_chanFlashMs);
                drawInputLine(pbuf, plen);
            }
            char k = inputHandler.getKeyboardInput();
            TrackballEvent tb = inputHandler.getTrackballEvent();
            tb = LockScreenManager::getInstance().interceptTrackball(tb);
            if (tb == TBALL_UP)   { pscroll++; pRedraw = true; }
            if (tb == TBALL_DOWN) {
                if (pscroll > 0) { pscroll--; pRedraw = true; }
                if (pscroll == 0) { p_unread = 0; pRedraw = true; }
            }
            if (tb == TBALL_CLICK && p_holdStart == 0) {
                if (pscroll > 0) { pscroll = 0; p_unread = 0; pRedraw = true; }
            }
            if (k == 0) continue;
            if (k == '\x1B') break;
            if (k == '\r' || k == '\n') {
                if (plen > 0) { ecSendMessage(pbuf); pbuf[0]='\0'; plen=0; pscroll=0; p_unread=0; pRedraw=true; }
            } else if (k == '\b' || k == 0x7F) {
                if (plen > 0) { pbuf[--plen] = '\0'; drawInputLine(pbuf, plen); }
            } else if (k == '+' || k == '=') {
                if (g_ecChannel < 13) { ecSetChannel(g_ecChannel + 1); p_chanFlashMs = millis(); pRedraw = true; }
            } else if (k == '-') {
                if (g_ecChannel > 1)  { ecSetChannel(g_ecChannel - 1); p_chanFlashMs = millis(); pRedraw = true; }
            } else if (isPrintable(k) && plen < 100) {
                pbuf[plen++] = k; pbuf[plen] = '\0'; drawInputLine(pbuf, plen);
            }
        }
        ecCoreDeinit();
        ecSdLogClose();
    }

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("ESPChat session ended.");
    displayManager.printCommandScreen();
}
