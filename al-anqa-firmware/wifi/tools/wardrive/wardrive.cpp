// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wardrive / wd — continuous WiFi scan + GPS → WiGLE WiFi-1.4 CSV.
// Synchronous scan + WiGLE-logging method adapted from Bruce firmware
// (BruceDevices/firmware, src/modules/gps/wardriving.cpp). See NOTICES.

#include "wardrive.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"

extern DisplayManager displayManager;   // used by both Plus impl and base-board stub
extern SDCardManager  sdCardManager;

#ifdef BOARD_TDECK_PLUS

#include "gps_manager.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include <WiFi.h>
#include <SD.h>
#include <vector>
#include <Arduino.h>

extern InputHandling  inputHandler;

// ── WiGLE auth-mode mapping ────────────────────────────────────────────────────
// WiGLE-style bracketed capabilities; close enough for wigle.net ingest.
static const char* wigleAuth(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:            return "[ESS]";
        case WIFI_AUTH_WEP:             return "[WEP][ESS]";
        case WIFI_AUTH_WPA_PSK:         return "[WPA-PSK-CCMP+TKIP][ESS]";
        case WIFI_AUTH_WPA2_PSK:        return "[WPA2-PSK-CCMP][ESS]";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "[WPA-WPA2-PSK-CCMP+TKIP][ESS]";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "[WPA2-EAP-CCMP][ESS]";
        case WIFI_AUTH_WPA3_PSK:        return "[WPA3-SAE-CCMP][ESS]";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "[WPA2-WPA3-PSK-CCMP][ESS]";
        default:                        return "[ESS]";
    }
}

// Dedup table — one row per BSSID per session. PSRAM, freed on exit (rule 5c).
#define WD_MAX_BSSIDS 1024

void runWardrive(char* args) {
    (void)args;
    DisplayManager& dm = displayManager;
    GpsManager&     gm = GpsManager::instance();

    // ── Session file is created LAZILY — only on the first AP actually logged ──
    // so sessions that log nothing (no GPS fix, quick quit) don't leave empty CSVs.
    bool sdOk        = sdCardManager.canAccessSD();
    bool fileCreated = false;
    char filePath[48] = "";

    auto ensureFile = [&]() -> bool {
        if (fileCreated) return true;
        if (!sdOk)       return false;
        sdCardManager.ensureDir(SD_DIR_WARDRIVE);
        uint16_t num = 1;
        char probe[48];
        while (num < 999) {
            snprintf(probe, sizeof(probe), SD_DIR_WARDRIVE "/%03u.csv", num);
            if (!SD.exists(probe)) break;
            num++;
        }
        snprintf(filePath, sizeof(filePath), SD_DIR_WARDRIVE "/%03u.csv", num);
        File f = SD.open(filePath, FILE_WRITE);
        if (!f) { sdOk = false; return false; }
        // WiGLE WiFi-1.4 pre-header + column header
        f.println("WigleWifi-1.4,appRelease=AL-ANQA,model=T-Deck-Plus,release=2026,"
                  "device=ESP32-S3,display=ST7789,board=LilyGo,brand=LilyGo");
        f.println("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
                  "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
        f.close();
        fileCreated = true;
        return true;
    };

    // Dedup table (PSRAM, freed on exit)
    uint8_t (*seen)[6] = (uint8_t(*)[6])ps_malloc(WD_MAX_BSSIDS * 6);
    if (!seen) seen = (uint8_t(*)[6])malloc(WD_MAX_BSSIDS * 6);
    uint16_t seenCount = 0;

    // ── Ensure GPS is running ─────────────────────────────────────────────────
    if (!gm.isRunning()) gm.start();

    // ── WiFi STA scan mode ────────────────────────────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);    // don't re-associate mid-session

    uint32_t logged = 0, scans = 0, lastSeenCount = 0;
    uint32_t minHeap = ESP.getFreeHeap();   // lowest free heap seen (leak detector)

    // Last-sweep gate diagnostic — answers "it scanned, why didn't it log?" in one glance:
    // n = APs the sweep returned · fix = GPS valid at log time · new = rows staged ·
    // wr = rows actually written to SD. (new==0 with n>0 ⇒ all dups or no fix.)
    int      lastN = 0; bool lastFix = false; uint32_t lastNew = 0, lastWrote = 0;
    bool     scanningNow = false;   // true only while a blocking sync sweep is in flight (UI cue)

    auto drawScreen = [&]() {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(0x7BEF);     dm.printText("[");
        dm.setTextColor(TFT_CYAN);   dm.printText("WAR");
        dm.setTextColor(0x7BEF);     dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("DRIVE");
        dm.setTextColor(0x7BEF);     dm.println("]");
        dm.printSeparator();

        char buf[52];
        dm.setCursor(10, dm.getCursorY());
        if      (!sdOk)       { dm.setTextColor(TFT_RED);  dm.println("No SD — not logging"); }
        else if (fileCreated) { dm.setTextColor(0x7BEF); dm.printText("File "); dm.setTextColor(TFT_WHITE); dm.println(filePath); }
        else                  { dm.setTextColor(0x7BEF); dm.println("File: created on first AP"); }

        // GPS status line
        dm.setCursor(10, dm.getCursorY());
        uint32_t sats = gm.satellites();
        if (gm.isValid()) {
            dm.setTextColor(TFT_GREEN);
            snprintf(buf, sizeof(buf), "FIX  %u sat  LOGGING", (unsigned)sats);
            dm.println(buf);
            dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
            snprintf(buf, sizeof(buf), "%+.6f, %+.6f", (double)gm.lat(), (double)gm.lon());
            dm.println(buf);
        } else {
            dm.setTextColor(TFT_YELLOW);
            snprintf(buf, sizeof(buf), "GPS searching  %u sat", (unsigned)sats);
            dm.println(buf);
            dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
            dm.println("not logging until fix (go outside)");
        }
        dm.printSeparator();

        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_CYAN);  dm.printText("Logged ");
        dm.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "%lu APs", (unsigned long)logged); dm.println(buf);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_CYAN);  dm.printText("Scans ");
        dm.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "%lu  (last saw %lu)", (unsigned long)scans, (unsigned long)lastSeenCount);
        dm.println(buf);
        // Per-sweep gate trace — if APs are seen but nothing logs, this shows which gate failed.
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);
        snprintf(buf, sizeof(buf), "sweep n=%d fix=%d new=%lu wr=%lu",
                 lastN, lastFix ? 1 : 0, (unsigned long)lastNew, (unsigned long)lastWrote);
        dm.println(buf);
        // Diagnostic: free heap (KB). Falling steadily across scans ⇒ a leak is
        // killing scans; ~stable ⇒ not memory. Min = lowest ever seen this session.
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_CYAN);  dm.printText("Heap ");
        dm.setTextColor(TFT_WHITE);
        snprintf(buf, sizeof(buf), "%luk  min %luk",
                 (unsigned long)(ESP.getFreeHeap() / 1024),
                 (unsigned long)(minHeap / 1024));
        dm.println(buf);
        dm.printSeparator();
        dm.setCursor(10, dm.getCursorY());
        if (scanningNow) { dm.setTextColor(TFT_YELLOW); dm.println("Scanning... ([q] after)"); }
        else             { dm.setTextColor(0x7BEF);     dm.println("[q] stop"); }
    };
    drawScreen();

    uint32_t lastDraw = millis();
    bool running = true;

    // ── Phase 1: wait for the first GPS fix with the WiFi radio IDLE ──────────
    // GpsManager does a one-time NVS *flash* write on its first fix (saveGpsFixFlag).
    // A flash write while a WiFi scan is in flight corrupts the scan engine — every
    // later scan then returns 0 (the "90 APs, then 0 after fix" bug). So we don't
    // scan at all until a fix exists; by then the flash write is already done.
    while (running && !gm.isValid()) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { running = false; break; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { drawScreen(); lastDraw = millis(); }
        if (millis() - lastDraw >= 1000) { drawScreen(); lastDraw = millis(); }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // Let the first-fix NVS save fully complete before any scan starts (closes the
    // tiny race between _valid=true and saveGpsFixFlag() in the GPS task).
    if (running) { vTaskDelay(pdMS_TO_TICKS(1000)); drawScreen(); lastDraw = millis(); }

    // ── Phase 2: GPS has a fix → SYNCHRONOUS scan + log (Bruce's proven method) ─
    // A blocking WiFi.scanNetworks() per sweep. Sync can't get wedged in a half-started async
    // state (the bug class the async version kept hitting), so there is NO scan state machine,
    // watchdog, or esp_wifi_scan_stop() to get wrong — the reliability comes from not using async
    // at all (same as Bruce/Marauder). Cost: the UI is frozen for the ~3-4s the scan blocks —
    // acceptable for wardriving (you're moving, not typing); shown as "Scanning..." so it doesn't
    // look hung, and [q] is honoured between sweeps. Project-specific wins are kept: Phase 1
    // fix-gate (NVS-collision fix), lazy file, PSRAM dedup, hidden-AP logging (show_hidden=true),
    // GDMA-safe staged write, and the gate diagnostic.
    while (running) {
        // One blocking sweep. show_hidden=true keeps hidden APs (logged with empty SSID).
        scanningNow = true; drawScreen(); lastDraw = millis();
        int n = WiFi.scanNetworks(false /*sync*/, true /*show_hidden*/);
        scanningNow = false;

        scans++;
        lastSeenCount = (n > 0) ? (uint32_t)n : 0;     // n<0 (WIFI_SCAN_FAILED) → treated as empty
        if (ESP.getFreeHeap() < minHeap) minHeap = ESP.getFreeHeap();

        // Stage WiGLE rows for new BSSIDs in RAM. The sync scan has fully finished here (radio
        // idle), but we still stage then write only after scanDelete() to honour the project's
        // GDMA rule (no SD writes overlapping live scan buffers).
        bool haveFix = gm.isValid();
        lastN = n; lastFix = haveFix;
        std::vector<String> pending;
        if (n > 0 && haveFix && sdOk) {
            double lat = gm.lat(), lon = gm.lon(), alt = gm.altitude();
            char ts[24];
            if (gm.timeValid() && gm.dateValid())
                snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u",
                         (unsigned)gm.year(), (unsigned)gm.month(), (unsigned)gm.day(),
                         (unsigned)gm.hour(), (unsigned)gm.minute(), (unsigned)gm.second());
            else
                ClockManager::instance().getTimestamp(ts, sizeof(ts));

            for (int i = 0; i < n; i++) {
                uint8_t* b = WiFi.BSSID(i);
                if (!b) continue;
                // Dedup: skip BSSIDs already logged this session
                bool dup = false;
                for (uint16_t j = 0; j < seenCount; j++)
                    if (memcmp(seen[j], b, 6) == 0) { dup = true; break; }
                if (dup) continue;
                if (seen && seenCount < WD_MAX_BSSIDS) { memcpy(seen[seenCount++], b, 6); }

                // SSID: strip commas/newlines so the CSV row stays well-formed
                char ssid[33];
                strncpy(ssid, WiFi.SSID(i).c_str(), sizeof(ssid) - 1);
                ssid[sizeof(ssid) - 1] = '\0';
                for (char* p = ssid; *p; p++) if (*p == ',' || *p == '\r' || *p == '\n') *p = ' ';

                // AccuracyMeters = HDOP (Bruce's convention). HDOP unknown (no DOP sentence parsed
                // yet) → fall back to a sat-count guess so the column is never a bogus 0.
                float hd  = gm.hdop();
                float acc = (hd > 0.0f) ? hd : ((gm.satellites() >= 6) ? 8.0f : 20.0f);

                // WiGLE WiFi-1.4 row: MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,
                // Lat,Lon,AltitudeMeters,AccuracyMeters,Type. MAC lowercase.
                char line[200];
                snprintf(line, sizeof(line),
                         "%02x:%02x:%02x:%02x:%02x:%02x,%s,%s,%s,%d,%d,%.6f,%.6f,%.1f,%.1f,WIFI",
                         b[0], b[1], b[2], b[3], b[4], b[5],
                         ssid, wigleAuth(WiFi.encryptionType(i)), ts,
                         WiFi.channel(i), WiFi.RSSI(i), lat, lon, alt, acc);
                pending.push_back(String(line));

                if ((i & 0x1F) == 0) vTaskDelay(1);   // yield periodically (Bruce) — WDT safety
            }
        }

        lastNew   = (uint32_t)pending.size();
        lastWrote = 0;

        // Scan results consumed → free them, settle, THEN write SD (radio idle). The file is
        // created here on the first real row, so no-AP sessions leave no file.
        WiFi.scanDelete();
        vTaskDelay(pdMS_TO_TICKS(120));   // Bruce's post-scanDelete settle before SD I/O
        if (!pending.empty() && ensureFile()) {
            for (size_t i = 0; i < pending.size(); i++) {
                // Count only rows that actually hit the card — a failed append must not
                // inflate the "Logged" total (that masked the real "wrote nothing" state).
                if (sdCardManager.appendLine(filePath, pending[i].c_str())) {
                    logged++; lastWrote++;
                }
            }
        }
        drawScreen(); lastDraw = millis();

        // Pace ~1s before the next sweep while staying responsive to [q] and lock-unlock.
        uint32_t pauseUntil = millis() + 1000;
        while (running && millis() < pauseUntil) {
            char kk = inputHandler.getKeyboardInput();
            if (kk == 'q' || kk == 'Q') { running = false; break; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) { drawScreen(); lastDraw = millis(); }
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    // ── Cleanup — GPS task stays running (like the gps command) ───────────────
    WiFi.scanDelete();
    if (seen) free(seen);

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN);
    char done[40];
    snprintf(done, sizeof(done), "Wardrive done — %lu APs", (unsigned long)logged);
    dm.println(done);
    dm.setCursor(10, dm.getCursorY()); dm.setTextColor(0x7BEF);
    if (fileCreated) dm.println(filePath);
    else             dm.println("No APs logged — no file written");
    vTaskDelay(pdMS_TO_TICKS(1800));
    dm.printCommandScreen();
}

#else  // ── base T-Deck (no GPS) ──────────────────────────────────────────────

void runWardrive(char* args) {
    (void)args;
    displayManager.println("Wardrive needs GPS — T-Deck Plus only.");
    displayManager.printCommandScreen();
}

#endif // BOARD_TDECK_PLUS
