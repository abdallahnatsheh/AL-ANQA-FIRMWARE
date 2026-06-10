// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "espchat.h"
#include "espchat_core.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include "notification_manager.h"
#include <WiFi.h>

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;

static uint32_t s_lastPoll   = 0;
static uint32_t s_popupUntil = 0;

// ── Start background mode ─────────────────────────────────────────────────────
// Behaviour:
//   SD present  → load contacts.csv → add all contacts as encrypted peers
//                 + broadcast peer  → receives BOTH private and public messages
//   No SD card  → no contacts → broadcast peer only → public messages only
void startEspchatBg(uint8_t ch) {
    if (g_ecBgActive) stopEspchatBg();

    bool hasSd = sdCardManager.canAccessSD();

    if (hasSd) {
        ecLoadConfig();    // reads public_channel from /apps/espchat/config.conf
        ecLoadContacts();  // fills g_ecContacts[] with LMKs
        // No ecSdLogOpen here — bg routes each message to its own file via ecSdLogDirect
    } else {
        g_ecContactCount  = 0;
        g_ecPublicChannel = 1;
    }

    // ch==0 means auto: pick most common contact channel, else public_channel
    if (ch == 0) ch = ecAutoChannel();

    if (!ecCoreInit(ch, false, nullptr, nullptr)) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("ESPChat BG: ESP-NOW init failed");
        displayManager.printCommandScreen();
        return;
    }

    g_ecBgActive = true;
    s_popupUntil = 0;
    s_lastPoll   = 0;

    displayManager.setEcActive(true);

    // Status line
    displayManager.setTextColor(TFT_CYAN);
    displayManager.printText("[EC] BG ch");
    char tmp[4]; snprintf(tmp, sizeof(tmp), "%d", ch);
    displayManager.printText(tmp);
    displayManager.printText("  ");
    if (!hasSd || g_ecContactCount == 0) {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.printText("public only");
    } else {
        displayManager.setTextColor(TFT_WHITE);
        char ct[28];
        snprintf(ct, sizeof(ct), "pub+prv (%d contact%s)",
                 g_ecContactCount, g_ecContactCount == 1 ? "" : "s");
        displayManager.printText(ct);
    }
    displayManager.setTextColor(0x4208);
    displayManager.println("  [ec stop]");
    displayManager.printCommandScreen();
}

// ── Stop ──────────────────────────────────────────────────────────────────────
void stopEspchatBg() {
    if (!g_ecBgActive) return;
    g_ecBgActive = false;
    ecCoreDeinit();
    displayManager.setEcActive(false);
}

// ── Poll (called from getKeyboardInput every ≤10ms) ───────────────────────────
void pollEspchatBg() {
    if (!g_ecBgActive) return;
    uint32_t now = millis();

    if (now - s_lastPoll >= 200) {
        s_lastPoll = now;

        if (ecDrainRx() && !displayManager.isBlocked()) {
            // Newest entry (ecDrainRx appended it, so it's just before g_ecLogWr)
            int slot = ((int)g_ecLogWr - 1 + EC_LOG_MAX) % EC_LOG_MAX;
            const EcLogEntry& e = g_ecLog[slot];
            if (!e.isTx) {
                // Is this sender a known contact? → private; otherwise → public
                const EcContact* ct = ecFindContact(e.mac);
                bool isPrivate = (ct != nullptr);

                // Prefer the name from contacts.csv over what the sender transmitted
                const char* senderName = (ct && ct->name[0])
                                         ? ct->name
                                         : (e.name[0] ? e.name : "???");

                NotificationManager::getInstance().notify(isPrivate ? NOTIF_INFO : NOTIF_PING);

                // Popup bar: cyan for private, green for public
                uint16_t bgCol = isPrivate ? 0x0318 : 0x0240;  // dark-cyan / dark-green
                displayManager.fillRect(0, 222, SCREEN_WIDTH, 16, bgCol);
                displayManager.setCursor(4, 223);
                displayManager.setTextColor(isPrivate ? TFT_CYAN : TFT_GREEN);
                displayManager.printText(isPrivate ? "[EC PRV] " : "[EC PUB] ");
                displayManager.setTextColor(TFT_WHITE);
                char popup[44];
                snprintf(popup, sizeof(popup), "%s: %.28s", senderName, e.text);
                displayManager.printText(popup);
                s_popupUntil = now + 4000;
            }
        }
    }

    if (s_popupUntil && now >= s_popupUntil) {
        s_popupUntil = 0;
        if (!displayManager.isBlocked())
            displayManager.printCommandScreen();
    }
}

bool isEspchatBgActive() { return g_ecBgActive; }
