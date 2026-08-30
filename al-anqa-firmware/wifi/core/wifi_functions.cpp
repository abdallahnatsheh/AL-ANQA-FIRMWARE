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
#include "layout.h"

extern InputHandling inputHandler;

// Scan result cache — populated by runWifiManager()/showWiFiResults(), used by connect + deauth
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

// Classify an AP's security from its authmode. WPA2_WPA3 = transition mode (a
// WPA3-capable AP that still accepts WPA2 → downgradeable target for w3d).
static uint8_t classifySec(wifi_auth_mode_t am, bool isOpen) {
    switch (am) {
        case WIFI_AUTH_OPEN:          return WSEC_OPEN;
        case WIFI_AUTH_WEP:           return WSEC_WEP;
        case WIFI_AUTH_WPA_PSK:       return WSEC_WPA;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:  return WSEC_WPA2;
        case WIFI_AUTH_WPA3_PSK:      return WSEC_WPA3;
        case WIFI_AUTH_WPA2_WPA3_PSK: return WSEC_TD;
        default:                      return isOpen ? WSEC_OPEN : WSEC_WPA2;
    }
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
        e.sec = classifySec(rec ? rec->authmode : (e.isOpen ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK), e.isOpen);
        scanCache.push_back(e);
    }
    WiFi.scanDelete();
}

// Draw one network's cells starting at the current cursor: SSID, RSSI, security
// tag (OPEN/WEP/WPA/WPA2/WPA3/TD — TD = WPA3 transition/downgradeable, yellow),
// and WPS. Caller draws the leading index/marker + a trailing newline.
static void drawNetRowCells(DisplayManager& dm, const NetworkEntry& e) {
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

    // Security tag — [TD] transition-mode (WPA3-capable, still WPA2 = downgradeable)
    // is highlighted yellow as the w3d target.
    const char* tag; uint16_t tcol;
    switch (e.sec) {
        case WSEC_OPEN: tag = " OPEN"; tcol = TFT_MAGENTA; break;
        case WSEC_WEP:  tag = " WEP";  tcol = TFT_RED;     break;
        case WSEC_WPA:  tag = " WPA";  tcol = 0x7BEF;      break;
        case WSEC_WPA2: tag = " WPA2"; tcol = 0x7BEF;      break;
        case WSEC_WPA3: tag = " WPA3";    tcol = TFT_GREEN;   break;
        case WSEC_TD:   tag = " WPA3/TD"; tcol = TFT_YELLOW;  break;
        default:        tag = " WPA";  tcol = 0x7BEF;      break;
    }
    dm.setTextColor(tcol);
    dm.printText(tag);
    if (e.wps) { dm.setTextColor(TFT_CYAN); dm.printText(" WPS"); }
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
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
        dm.printText(idx);
        drawNetRowCells(dm, scanCache[i]);
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

// ── WiFi manager (sw) ───────────────────────────────────────────────────────────

// Logical radio state for the manager: option-A "off" = disassociated + STA idle
// (GDMA-safe, never WIFI_OFF), so the radio stays initialised for the next scan.
static bool s_radioOff = false;

static const int MGR_LIST_TOP = 74;   // first list row Y
static const int MGR_VISIBLE  = 8;    // rows shown at once (leaves room for the footer)

static void drawMgrStatus(DisplayManager& dm, int y) {
    dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    dm.setCursor(10, y);
    if (s_radioOff) {
        dm.setTextColor(0x7BEF);  dm.printText("Radio: ");
        dm.setTextColor(TFT_RED); dm.printText("OFF (idle)");
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        dm.setTextColor(TFT_GREEN); dm.printText("* ");
        char ssid[15]; snprintf(ssid, sizeof(ssid), "%.14s", WiFi.SSID().c_str());
        dm.setTextColor(TFT_WHITE); dm.printText(ssid);
        dm.printText(" ");
        dm.setTextColor(0x7BEF);    dm.printText(WiFi.localIP().toString().c_str());
        int r = WiFi.RSSI();
        char rs[7]; snprintf(rs, sizeof(rs), " %d", r);
        dm.setTextColor(rssiColor(r)); dm.printText(rs);
    } else {
        dm.setTextColor(TFT_YELLOW); dm.printText("Not connected");
    }
}

static void renderManager(DisplayManager& dm, int sel, int top, int total) {
    dm.clearScreen();
    dm.setCursor(10, outputY);
    dm.setDefaultTextSize();

    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("WIFI");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("MGR");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    char cnt[16]; snprintf(cnt, sizeof(cnt), "%d nets", total);
    dm.setTextColor(0x7BEF);     dm.println(cnt);

    drawMgrStatus(dm, outputY + LINE_HEIGHT);          // status line (y=52)
    dm.setCursor(0, outputY + 2 * LINE_HEIGHT);        // separator (y=66)
    dm.printSeparator();

    if (total == 0) {
        dm.setCursor(10, MGR_LIST_TOP);
        dm.setTextColor(0x7BEF);
        dm.println(s_radioOff ? "Radio off — press [o]" : "No networks. Press [u] to rescan.");
    } else {
        int end = min(top + MGR_VISIBLE, total);
        for (int i = top; i < end; i++) {
            int  y      = MGR_LIST_TOP + (i - top) * LINE_HEIGHT;
            bool selrow = (i == sel);
            if (selrow) dm.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);  // highlight bar
            dm.setCursor(2, y);
            dm.setTextColor(selrow ? TFT_YELLOW : 0x7BEF);
            dm.printText(selrow ? ">" : " ");
            char idx[5]; snprintf(idx, sizeof(idx), "%2d", i);
            dm.setTextColor(TFT_YELLOW);
            dm.printText(idx);
            drawNetRowCells(dm, scanCache[i]);
        }
    }

    // Footer — a separator rule with a clear gap above the tips
    dm.setCursor(0, layoutFooterY(48));
    dm.printSeparator();
    dm.setCursor(6, layoutFooterY(40));
    dm.setTextColor(0x7BEF);
    dm.printText("trkbl=sel  click/ent=connect");
    dm.setCursor(6, layoutFooterY(26));
    dm.printText("[d]isc [f]orget [o]n/off [u] [q]");
    dm.setTextColor(TFT_WHITE);
}

void WiFiFunctions::connectSelected(int idx) {
    if (idx < 0 || idx >= (int)scanCache.size()) return;
    char idxStr[8]; snprintf(idxStr, sizeof(idxStr), "%d", idx);
    connectToWiFiCommand(idxStr);   // reuses the full password/save/connect flow
    s_radioOff = false;             // a connect attempt turns the radio on
}

void WiFiFunctions::forgetSelected(int idx) {
    if (idx < 0 || idx >= (int)scanCache.size()) return;
    String ss = String(scanCache[idx].ssid);
    if (ss.isEmpty()) {
        const char* r = lookupHidden(scanCache[idx].bssid);
        if (r) ss = String(r);
    }
    displayManager.fillRect(0, layoutFooterY(38), SCREEN_WIDTH, 38, TFT_BLACK);
    displayManager.setCursor(6, layoutFooterY(30));
    if (ss.isEmpty()) {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.printText("Hidden net — can't forget");
    } else {
        bool ok = forgetNetwork(ss);
        char msg[40];
        snprintf(msg, sizeof(msg), "%s %.20s", ok ? "Forgot" : "Not saved:", ss.c_str());
        displayManager.setTextColor(ok ? TFT_GREEN : TFT_YELLOW);
        displayManager.printText(msg);
    }
    displayManager.setTextColor(TFT_WHITE);
    delay(1100);   // brief toast, then the caller repaints
}

void WiFiFunctions::runWifiManager(char* args) {
    // ── subcommands: radio on/off (non-interactive) ───────────────────────────
    if (args && *args) {
        String a(args); a.trim(); a.toLowerCase();
        if (a == "on") {
            WiFi.mode(WIFI_STA); s_radioOff = false;
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_GREEN);
            displayManager.println("WiFi radio ON");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printCommandScreen();
            return;
        }
        if (a == "off") {
            WiFi.disconnect(false); s_radioOff = true;   // option A: STA idle, GDMA-safe
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("WiFi radio OFF (idle)");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printCommandScreen();
            return;
        }
        Utils::printUsage("scanwifi");   // unknown arg → the command's own help
        displayManager.printCommandScreen();
        return;
    }

    // ── interactive manager ───────────────────────────────────────────────────
    MacChanger::getInstance().applyIfEnabled();
    s_radioOff = false;

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("Scanning WiFi...");
    if (!runAsyncScan(displayManager, numberOfNetworks, networkScanExecuted)) {
        displayManager.printCommandScreen();
        return;
    }

    int  sel = 0, top = 0;
    bool redraw = true;
    while (true) {
        if (redraw) {
            int total = (int)scanCache.size();
            if (sel >= total)               sel = max(0, total - 1);
            if (sel < top)                  top = sel;
            if (sel >= top + MGR_VISIBLE)   top = sel - MGR_VISIBLE + 1;
            if (top < 0)                    top = 0;
            renderManager(displayManager, sel, top, total);
            redraw = false;
        }

        // Trackball: U/D select, CLICK connect (poll directly, like wm/netspy)
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)    { if (sel > 0)                          { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < (int)scanCache.size() - 1)  { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { if (!scanCache.empty() && !s_radioOff){ connectSelected(sel); redraw = true; } continue; }

        char k = inputHandler.getKeyboardInput();
        if (!k) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
            continue;
        }
        if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
        else if (k == '\r' || k == '\n') { if (!scanCache.empty() && !s_radioOff) { connectSelected(sel); redraw = true; } }
        else if (k == 'l' || k == 'L')   { sel = min((int)scanCache.size() - 1, sel + MGR_VISIBLE); if (sel < 0) sel = 0; redraw = true; }
        else if (k == 'a' || k == 'A')   { sel = max(0, sel - MGR_VISIBLE); redraw = true; }
        else if (k == 'd' || k == 'D')   { WiFi.disconnect(false); redraw = true; }
        else if (k == 'o' || k == 'O')   {
            if (s_radioOff) { WiFi.mode(WIFI_STA);  s_radioOff = false; }
            else            { WiFi.disconnect(false); s_radioOff = true; }
            redraw = true;
        }
        else if (k == 'f' || k == 'F')   { if (!scanCache.empty()) { forgetSelected(sel); redraw = true; } }
        else if (k == 'u' || k == 'U')   {
            if (s_radioOff) { WiFi.mode(WIFI_STA); s_radioOff = false; }
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.println("Scanning WiFi...");
            if (!runAsyncScan(displayManager, numberOfNetworks, networkScanExecuted)) {
                displayManager.printCommandScreen();
                return;
            }
            sel = 0; top = 0; redraw = true;
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

// Forget one network: drop its saved password from NVS and its block from
// wpa_supplicant.conf. Returns true if it existed in either store.
bool WiFiFunctions::forgetNetwork(const String& ssid) {
    if (ssid.isEmpty()) return false;
    Preferences prefs;
    prefs.begin("wifi", false);
    bool hadNvs = prefs.remove(ssid.c_str());   // false if key absent
    prefs.end();
    int r = removeWpaNetwork(ssid);             // wifi_creds: 1=removed
    return hadNvs || r == 1;
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
    // Save Y before the poll loop — getKeyboardInput() triggers status-bar
    // updates which leave tft.getCursorY() at y<30 (battery icon area).
    int32_t inputY = displayManager.getCursorY();

    while (true) {
        char c = inputHandler.getKeyboardInput();
        if (!c) continue;
        if (c == '\n' || c == '\r') break;
        if (c == '\b') {
            if (pw.length() > 0) pw.remove(pw.length() - 1);
        } else if (isPrintable(c) && pw.length() < 100) {
            pw += c;
        }
        // Redraw masked line at fixed inputY — status-bar cursor corruption
        // cannot reach here because we never use getCursorY() inside the loop.
        displayManager.fillRect(10, inputY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
        displayManager.setCursor(10, inputY);
        displayManager.setTextColor(TFT_WHITE);
        for (size_t i = 0; i < pw.length(); i++) displayManager.printText(pw[i]);
    }
    // Leave cursor below the input line for subsequent prints
    displayManager.setCursor(10, inputY + LINE_HEIGHT + 2);
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
            // Don't save yet — only save to NVS after successful connection
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

    // Save to NVS only on successful connection
    if (!isOpen) storeWiFiCredentials(ssid, password);

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

bool WiFiFunctions::getNetworkWps(int index) const {
    if (!networkScanExecuted || index < 0 || index >= (int)scanCache.size()) return false;
    return scanCache[index].wps;
}

int WiFiFunctions::getNetworkSec(int index) const {
    if (!networkScanExecuted || index < 0 || index >= (int)scanCache.size()) return -1;
    return scanCache[index].sec;
}

void WiFiFunctions::refreshHiddenCache()              { loadHiddenCache(); }
bool WiFiFunctions::isHiddenKnown(const uint8_t* b) const { return lookupHidden(b) != nullptr; }
