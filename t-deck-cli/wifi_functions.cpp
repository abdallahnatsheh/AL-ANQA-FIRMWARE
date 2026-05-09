#include <Preferences.h>
#include <vector>
#include "wifi_functions.h"
#include "input_handling.h"
#include "utils.h"

extern InputHandling inputHandler;

// Scan result cache — populated by scanWiFiNetworks(), used by connect + deauth
static std::vector<NetworkEntry> scanCache;

WiFiFunctions::WiFiFunctions(DisplayManager& displayManager)
    : displayManager(displayManager) {}

// ── scan helpers ──────────────────────────────────────────────────────────────

static uint16_t rssiColor(int rssi) {
    if (rssi >= -60) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    return TFT_RED;
}

static void triggerAsyncScan() {
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
        scanCache.push_back(e);
    }
    WiFi.scanDelete();
}

static void renderScanPage(DisplayManager& dm, int page, int perPage, int total, int totalPages) {
    dm.clearScreen();
    dm.setCursor(10, outputY);
    dm.setDefaultTextSize();

    char title[40];
    snprintf(title, sizeof(title), "WiFi Networks  [%d/%d]", page + 1, totalPages);
    dm.setTextColor(TFT_CYAN);
    dm.println(title);
    dm.setTextColor(0x7BEF);
    dm.println("-------------------------------");
    dm.setTextColor(TFT_WHITE);

    int start = page * perPage;
    int end   = min(start + perPage, total);

    for (int i = start; i < end; i++) {
        const NetworkEntry& e = scanCache[i];

        char ssid[16];
        strncpy(ssid, e.ssid, 14);
        ssid[14] = '\0';
        if (strlen(e.ssid) > 14) { ssid[12] = '.'; ssid[13] = '.'; }

        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
        dm.printText(idx);

        dm.setTextColor(TFT_WHITE);
        char padded[18]; snprintf(padded, sizeof(padded), " %-14s", ssid);
        dm.printText(padded);

        dm.setTextColor(rssiColor(e.rssi));
        char rssiStr[6]; snprintf(rssiStr, sizeof(rssiStr), "%4d", e.rssi);
        dm.printText(rssiStr);

        dm.setTextColor(e.isOpen ? TFT_MAGENTA : 0x7BEF);
        dm.println(e.isOpen ? " OPEN" : " WPA");
    }

    dm.setTextColor(0x7BEF);
    dm.println("-------------------------------");
    dm.setTextColor(TFT_WHITE);
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
    return true;
}

void WiFiFunctions::scanWiFiNetworks() {
    const int perPage = 6;

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
    if (!networkScanExecuted || scanCache.empty()) {
        displayManager.println("Run scanwifi first.");
        displayManager.printCommandScreen();
        return;
    }

    int idx = -1;
    if (sscanf(args, "%d", &idx) != 1 || idx < 0 || idx >= numberOfNetworks) {
        displayManager.println("Invalid index.");
        displayManager.printCommandScreen();
        return;
    }

    const NetworkEntry& net = scanCache[idx];
    String ssid(net.ssid);
    String password = "";

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Connect to WiFi --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("SSID: ");
    displayManager.println(ssid);

    if (!net.isOpen) {
        password = getWiFiPassword(ssid);
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

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_CYAN);
    displayManager.printText("Connecting");
    displayManager.setTextColor(TFT_WHITE);

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("T-DECK");
    WiFi.begin(ssid.c_str(), net.isOpen ? nullptr : password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            displayManager.println();
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Timed out. Check password.");
            displayManager.setTextColor(TFT_WHITE);
            WiFi.disconnect(true);
            delay(2000);
            displayManager.tdeck_begin();
            return;
        }
        delay(500);
        displayManager.printText(".");
    }

    displayManager.println();
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Connected!");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("IP: ");
    displayManager.println(WiFi.localIP().toString());
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
