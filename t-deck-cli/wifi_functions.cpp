#include <Preferences.h>
#include <vector>
#include <SD.h>
#include <esp_wifi.h>
#include "wifi_functions.h"
#include "lockscreen_manager.h"
#include "wifi_creds.h"
#include "sdcard_manager.h"
#include "input_handling.h"
#include "utils.h"
#include "mac_changer.h"

extern InputHandling inputHandler;

// Scan result cache — populated by scanWiFiNetworks(), used by connect + deauth
static std::vector<NetworkEntry> scanCache;

// ── Hidden SSID cache (loaded from SD after each scan) ────────────────────────
struct HiddenEntry { uint8_t bssid[6]; char ssid[33]; };
static HiddenEntry hiddenCache[64];
static int         hiddenCacheCount = 0;

static void loadHiddenCache() {
    hiddenCacheCount = 0;
    File f = SD.open(SD_LOG_HIDDEN_SSIDS, FILE_READ);
    if (!f) return;
    char buf[96];
    while (f.available() && hiddenCacheCount < 64) {
        int n = 0;
        while (f.available() && n < (int)sizeof(buf) - 1) {
            char c = (char)f.read();
            if (c == '\n') break;
            if (c != '\r') buf[n++] = c;
        }
        buf[n] = '\0';
        if (n == 0) continue;
        char* bssidStr = strtok(buf, ",");
        char* ssidStr  = strtok(nullptr, ",");
        if (!bssidStr || !ssidStr) continue;
        uint8_t b[6];
        if (sscanf(bssidStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            memcpy(hiddenCache[hiddenCacheCount].bssid, b, 6);
            strncpy(hiddenCache[hiddenCacheCount].ssid, ssidStr, 32);
            hiddenCache[hiddenCacheCount].ssid[32] = '\0';
            hiddenCacheCount++;
        }
    }
    f.close();}

static const char* lookupHidden(const uint8_t* bssid) {
    for (int i = 0; i < hiddenCacheCount; i++) {
        if (memcmp(hiddenCache[i].bssid, bssid, 6) == 0) return hiddenCache[i].ssid;
    }
    return nullptr;
}

WiFiFunctions::WiFiFunctions(DisplayManager& displayManager)
    : displayManager(displayManager) {}

// ── scan helpers ──────────────────────────────────────────────────────────────

static uint16_t rssiColor(int rssi) {
    if (rssi >= -60) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    return TFT_RED;
}

static void triggerAsyncScan() {
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true, true); // async=true, show_hidden=true — returns immediately
}

static void populateScanCache(int& count, bool& done) {
    int n = WiFi.scanComplete();
    count = (n < 0) ? 0 : n;
    done  = true;
    scanCache.clear();
    for (int i = 0; i < count; i++) {
        NetworkEntry e;
        strncpy(e.ssid, WiFi.SSID(i).c_str(), sizeof(e.ssid) - 1);
        e.ssid[sizeof(e.ssid) - 1] = '\0';
        e.rssi    = WiFi.RSSI(i);
        e.channel = WiFi.channel(i);
        e.isOpen  = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        uint8_t* b = WiFi.BSSID(i);
        if (b) memcpy(e.bssid, b, 6);
        wifi_ap_record_t* rec = (wifi_ap_record_t*)WiFi.getScanInfoByIndex(i);
        e.wps = rec ? (bool)rec->wps : false;
        scanCache.push_back(e);
    }
    WiFi.scanDelete();
}

static void renderScanPage(DisplayManager& dm, int page, int perPage, int total, int totalPages) {
    dm.clearScreen();
    dm.setCursor(10, outputY);
    dm.setDefaultTextSize();

    char pgBuf[8]; snprintf(pgBuf, sizeof(pgBuf), "%02d/%02d", page + 1, totalPages);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("SCAN");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("WIFI");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    dm.setTextColor(0x7BEF);     dm.println(pgBuf);
    dm.printSeparator();
    dm.setTextColor(TFT_WHITE);

    int start = page * perPage;
    int end   = min(start + perPage, total);

    for (int i = start; i < end; i++) {
        const NetworkEntry& e = scanCache[i];

        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
        dm.printText(idx);

        if (e.ssid[0] == '\0') {
            const char* known = lookupHidden(e.bssid);
            if (known && known[0] != '\0') {
                char trunc[14]; strncpy(trunc, known, 13); trunc[13] = '\0';
                char padded[18]; snprintf(padded, sizeof(padded), " ~%-13s", trunc);
                dm.setTextColor(TFT_CYAN);
                dm.printText(padded);
            } else {
                dm.setTextColor(0x7BEF);
                dm.printText(" <hidden>      ");
            }
        } else {
            char ssid[16];
            strncpy(ssid, e.ssid, 14);
            ssid[14] = '\0';
            if (strlen(e.ssid) > 14) { ssid[12] = '.'; ssid[13] = '.'; }
            char padded[18]; snprintf(padded, sizeof(padded), " %-14s", ssid);
            dm.setTextColor(TFT_WHITE);
            dm.printText(padded);
        }

        dm.setTextColor(rssiColor(e.rssi));
        char rssiStr[6]; snprintf(rssiStr, sizeof(rssiStr), "%4d", e.rssi);
        dm.printText(rssiStr);

        dm.setTextColor(e.isOpen ? TFT_MAGENTA : 0x7BEF);
        dm.printText(e.isOpen ? " OPEN" : " WPA");
        if (e.wps) { dm.setTextColor(TFT_CYAN); dm.printText(" WPS"); }
        dm.println();
    }

    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.printDefaultTableHelpInstructions();
}

static bool runAsyncScan(DisplayManager& dm, int& count, bool& done) {
    triggerAsyncScan();
    uint32_t frame = 0;
    const char spinner[] = "|/-\\";
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        char buf[28];
        snprintf(buf, sizeof(buf), "Scanning WiFi... %c", spinner[frame++ % 4]);
        dm.fillRect(10, outputY, SCREEN_WIDTH - 10, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(10, outputY);
        dm.setTextColor(TFT_CYAN);
        dm.printText(buf);
        vTaskDelay(pdMS_TO_TICKS(200));
        if (inputHandler.getKeyboardInput() == 'q') {
            WiFi.scanDelete();
            return false; // aborted
        }
    }
    populateScanCache(count, done);
    loadHiddenCache();
    return true;
}

void WiFiFunctions::scanWiFiNetworks() {
    const int perPage = 10;

    MacChanger::getInstance().applyIfEnabled();

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("Scanning WiFi...");

    if (!runAsyncScan(displayManager, numberOfNetworks, networkScanExecuted)) {
        displayManager.printCommandScreen();
        return;
    }

    int totalPages  = max(1, (numberOfNetworks + perPage - 1) / perPage);
    int currentPage = 0;

    while (true) {
        renderScanPage(displayManager, currentPage, perPage, numberOfNetworks, totalPages);

        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
            if (k == 'u' || k == 'U') {
                displayManager.clearScreen();
                displayManager.setCursor(10, outputY);
                displayManager.setTextColor(TFT_CYAN);
                displayManager.println("Scanning WiFi...");
                if (!runAsyncScan(displayManager, numberOfNetworks, networkScanExecuted)) {
                    displayManager.printCommandScreen();
                    return;
                }
                totalPages  = max(1, (numberOfNetworks + perPage - 1) / perPage);
                currentPage = 0;
                break;
            }
        }
    }
}

void WiFiFunctions::showWiFiResults() {
    if (!networkScanExecuted || scanCache.empty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No scan data. Run scanwifi first.");
        displayManager.printCommandScreen();
        return;
    }
    const int perPage  = 10;
    int total          = (int)scanCache.size();
    int totalPages     = max(1, (total + perPage - 1) / perPage);
    int currentPage    = 0;
    while (true) {
        renderScanPage(displayManager, currentPage, perPage, total, totalPages);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        }
    }
}

// ── credentials ───────────────────────────────────────────────────────────────

void WiFiFunctions::storeWiFiCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString(ssid.c_str(), password);
    prefs.end();
}

String WiFiFunctions::getWiFiPassword(const String& ssid) {
    Preferences prefs;
    prefs.begin("wifi", true);
    String pw = prefs.getString(ssid.c_str(), "");
    prefs.end();
    return pw;
}

void WiFiFunctions::clearAllWiFiCredentials() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("All WiFi credentials cleared.");
    delay(2000);
    displayManager.tdeck_begin();
}

// ── password entry ────────────────────────────────────────────────────────────

String WiFiFunctions::readPassword() {
    String pw = "";
    while (true) {
        char c = inputHandler.getKeyboardInput();
        if (!c) continue;
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (pw.length() > 0) {
                pw.remove(pw.length() - 1);
                displayManager.backspaceChar();
            }
        } else if (isPrintable(c) && c != ' ' && pw.length() < 100) {
            pw += c;
            displayManager.printText('*');
        }
    }
    displayManager.println();
    return pw;
}

// ── connect ───────────────────────────────────────────────────────────────────

void WiFiFunctions::connectToWiFiCommand(char* args) {
    if (!args || !*args) {
        displayManager.println("Usage: cw <index> or cw <ssid>");
        displayManager.printCommandScreen();
        return;
    }

    String  ssid;
    String  password  = "";
    bool    isOpen    = false;
    bool    isHidden  = false;
    uint8_t bssid[6]  = {0};
    bool    hasBssid  = false;

    // ── resolve by scan index ─────────────────────────────────────────────────
    int idx = -1;
    if (sscanf(args, "%d", &idx) == 1 && idx >= 0 && networkScanExecuted && idx < numberOfNetworks) {
        const NetworkEntry& net = scanCache[idx];
        ssid     = String(net.ssid);
        isOpen   = net.isOpen;
        memcpy(bssid, net.bssid, 6);
        hasBssid = true;
        // hidden network revealed by hiddenssid — ssid is "" in scan cache
        if (ssid.isEmpty()) {
            const char* resolved = lookupHidden(net.bssid);
            if (resolved && resolved[0] != '\0') {
                ssid = String(resolved); isHidden = true;
            } else {
                displayManager.println("Hidden SSID unknown.");
                displayManager.println("Run hiddenssid first,");
                displayManager.println("or: cw <ssid>");
                displayManager.printCommandScreen();
                return;
            }
        }

    // ── resolve by SSID name (hidden / known, no scan needed) ─────────────────
    } else {
        ssid = String(args);
        ssid.trim();
        if (ssid.isEmpty()) {
            displayManager.println("Usage: cw <index> or cw <ssid>");
            displayManager.printCommandScreen();
            return;
        }
        WifiNetwork saved = getWifiNetwork(ssid);
        if (!saved.ssid.isEmpty()) {
            isOpen   = saved.open;
            isHidden = saved.hidden;
        }
    }

    // ── header ────────────────────────────────────────────────────────────────
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println(isHidden ? "-- Connect (hidden) --" : "-- Connect to WiFi --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("SSID: ");
    displayManager.println(ssid);

    // ── password resolution ───────────────────────────────────────────────────
    if (!isOpen) {
        password = getWiFiPassword(ssid);
        if (password.isEmpty()) {
            WifiNetwork sdNet = getWifiNetwork(ssid);
            if (!sdNet.ssid.isEmpty() && !sdNet.isHashed && !sdNet.open)  {
                password = sdNet.psk;
                storeWiFiCredentials(ssid, password);
            }
        }
        if (password.isEmpty()) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("Password (q=cancel):");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.setCursor(10, displayManager.getCursorY());
            password = readPassword();
            if (password == "q" || password == "Q") {
                displayManager.printCommandScreen();
                return;
            }
            if (password.length() < 8) {
                displayManager.setTextColor(TFT_RED);
                displayManager.println("Min 8 characters.");
                displayManager.setTextColor(TFT_WHITE);
                delay(2000);
                displayManager.tdeck_begin();
                return;
            }
            storeWiFiCredentials(ssid, password);
        } else {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("Using saved password.");
            displayManager.setTextColor(TFT_WHITE);
        }
    }

    // ── connect ───────────────────────────────────────────────────────────────
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_CYAN);
    displayManager.printText("Connecting");
    displayManager.setTextColor(TFT_WHITE);

    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_STA);
    MacChanger::getInstance().applyIfEnabled();
    WiFi.setHostname("T-DECK");
    WiFi.begin(ssid.c_str(), isOpen ? nullptr : password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            displayManager.println();
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Timed out. Check password.");
            displayManager.setTextColor(TFT_WHITE);
            WiFi.disconnect(false);
            delay(2000);
            displayManager.tdeck_begin();
            return;
        }
        delay(500);
        displayManager.printText(".");
    }

    // ── success ───────────────────────────────────────────────────────────────
    displayManager.println();
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Connected!");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("IP: ");
    displayManager.println(WiFi.localIP().toString());

    WifiNetwork newNet;
    newNet.ssid   = ssid;
    newNet.psk    = password;
    newNet.open   = isOpen;
    newNet.hidden = isHidden;
    if (hasBssid) {
        char bssidStr[18];
        snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        newNet.bssid = String(bssidStr);
    }
    int sdResult = appendWpaNetwork(newNet);
    displayManager.setCursor(10, displayManager.getCursorY());
    if (sdResult == 1) {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Saved to wpa_supplicant.conf");
    } else if (sdResult == 0) {
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Already in wpa_supplicant.conf");
    } else if (sdResult == -2) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("SD write failed");
    } else {
        displayManager.setTextColor(0x7BEF);
        displayManager.println("NVS only (no SD card)");
    }
    displayManager.setTextColor(TFT_WHITE);
    delay(3000);
    displayManager.tdeck_begin();
}

// ── accessors used by deauth ──────────────────────────────────────────────────

bool WiFiFunctions::isScanDone() const { return networkScanExecuted; }
int  WiFiFunctions::getNetworkCount() const { return numberOfNetworks; }

bool WiFiFunctions::getNetworkInfo(int index, uint8_t* bssidOut, int* channelOut) {
    if (!networkScanExecuted || index < 0 || index >= (int)scanCache.size()) return false;
    memcpy(bssidOut, scanCache[index].bssid, 6);
    *channelOut = scanCache[index].channel;
    return true;
}

bool WiFiFunctions::getNetworkSSID(int index, char* ssidOut) const {
    if (!networkScanExecuted || index < 0 || index >= (int)scanCache.size()) return false;
    strncpy(ssidOut, scanCache[index].ssid, 32);
    ssidOut[32] = '\0';
    return true;
}

bool WiFiFunctions::getNetworkOpen(int index) const {
    if (!networkScanExecuted || index < 0 || index >= (int)scanCache.size()) return true;
    return scanCache[index].isOpen;
}

void WiFiFunctions::refreshHiddenCache()              { loadHiddenCache(); }
bool WiFiFunctions::isHiddenKnown(const uint8_t* b) const { return lookupHidden(b) != nullptr; }
