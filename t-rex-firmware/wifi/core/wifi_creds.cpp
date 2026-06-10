// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "wifi_creds.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include <SD.h>
#include <Preferences.h>
#include <nvs.h>
#include <esp_idf_version.h>
#include <vector>

extern DisplayManager  displayManager;
extern InputHandling   inputHandler;
extern SDCardManager   sdCardManager;

#define WP_PER_PAGE 5

// ── Parser ────────────────────────────────────────────────────────────────────
// Handles all standard wpa_supplicant.conf formats:
//   psk="plain"              — quoted plain text (T-Rex native, Linux accepts)
//   #psk="plain"             — wpa_passphrase comment — primary recovery path
//   psk=<64 hex chars>       — hashed PMK — marked unusable for ESP32
//   key_mgmt=NONE            — open network
//   scan_ssid=1              — hidden SSID
//   priority=<int>           — connection priority (stored, not used for manual connect)
//   ctrl_interface/update_config/country — global fields, silently skipped

// Safe line reader — never allocates more than maxLen bytes regardless of file content.
// readStringUntil() allocates the full line before we can check length — OOM risk.
static bool readSafeLine(File& f, String& out) {
    out = "";
    bool got = false;
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\n') { got = true; break; }
        if (c == '\r') continue;
        if (out.length() < 256) out += c;
        // line longer than 256: keep consuming until \n so file position stays correct
    }
    return got || out.length() > 0;
}

static std::vector<WifiNetwork> parseWpaSupplicant() {
    std::vector<WifiNetwork> out;
    File f = SD.open(WIFIPASS_PATH, FILE_READ);
    if (!f) return out;

    WifiNetwork cur;
    bool inBlock = false;

    String line;
    while (readSafeLine(f, line)) {
        line.trim();
        if (line.isEmpty()) continue;

        // ── outside a network block ───────────────────────────────────────────
        if (!inBlock) {
            if (line == "network={") { inBlock = true; cur = WifiNetwork(); }
            continue;
        }

        // ── closing brace ─────────────────────────────────────────────────────
        if (line == "}") {
            if (!cur.ssid.isEmpty()) {
                // Dedup: if SSID already present, prefer plain PSK over hashed
                bool merged = false;
                for (auto& n : out) {
                    if (n.ssid != cur.ssid) continue;
                    if (n.isHashed && !cur.isHashed && !cur.psk.isEmpty())
                        n = cur;  // upgrade: replace hex-only entry with plain
                    merged = true;
                    break;
                }
                if (!merged) out.push_back(cur);
            }
            inBlock = false;
            continue;
        }

        // ── #psk= comment (wpa_passphrase plain-text recovery) ────────────────
        // Must be checked before the generic comment skip below.
        if (line.startsWith("#psk=\"") && line.endsWith("\"")) {
            cur.psk      = line.substring(6, line.length() - 1);
            cur.isHashed = false;
            continue;
        }

        // ── skip all other comments ───────────────────────────────────────────
        if (line.startsWith("#")) {
            if (line.startsWith("# bssid=")) cur.bssid = line.substring(8);
            continue;
        }

        // ── standard network block fields ─────────────────────────────────────
        if (line.startsWith("ssid=\"") && line.endsWith("\"")) {
            cur.ssid = line.substring(6, line.length() - 1);

        } else if (line.startsWith("psk=\"") && line.endsWith("\"")) {
            // quoted plain text — only overwrite if #psk= comment hasn't set it
            if (cur.psk.isEmpty()) {
                cur.psk      = line.substring(5, line.length() - 1);
                cur.isHashed = false;
            }

        } else if (line.startsWith("psk=") && line.length() == 68) {
            // 64-char hex PMK — mark unusable only if we have no plain text yet
            if (cur.psk.isEmpty()) cur.isHashed = true;

        } else if (line == "key_mgmt=NONE") {
            cur.open = true;

        } else if (line == "scan_ssid=1") {
            cur.hidden = true;

        } else if (line.startsWith("priority=")) {
            cur.priority = line.substring(9).toInt();
        }
        // all other fields (proto=, pairwise=, auth_alg=, etc.) are ignored
    }
    if (inBlock && !cur.ssid.isEmpty()) out.push_back(cur);  // flush unclosed block
    f.close();
    return out;
}

// ── NVS fallback (no SD) ──────────────────────────────────────────────────────

static std::vector<WifiNetwork> loadNvsNetworks() {
    std::vector<WifiNetwork> out;
    nvs_iterator_t it = nullptr;

#if ESP_IDF_VERSION_MAJOR >= 5
    if (nvs_entry_find("nvs", "wifi", NVS_TYPE_STR, &it) != ESP_OK) return out;
    while (it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        Preferences p; p.begin("wifi", true);
        String pw = p.getString(info.key, ""); p.end();
        WifiNetwork n;
        n.ssid = String(info.key);
        n.psk  = pw;
        n.open = pw.isEmpty();
        out.push_back(n);
        if (nvs_entry_next(&it) != ESP_OK) break;
    }
    nvs_release_iterator(it);  // safe on null; releases handle if loop broke on error
#else
    it = nvs_entry_find("nvs", "wifi", NVS_TYPE_STR);
    while (it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        Preferences p; p.begin("wifi", true);
        String pw = p.getString(info.key, ""); p.end();
        WifiNetwork n;
        n.ssid = String(info.key);
        n.psk  = pw;
        n.open = pw.isEmpty();
        out.push_back(n);
        it = nvs_entry_next(it);
    }
    nvs_release_iterator(it);
#endif
    return out;
}

// ── Render ────────────────────────────────────────────────────────────────────

static void renderWpPage(const std::vector<WifiNetwork>& nets, int page, bool fromSD) {
    int total      = (int)nets.size();
    int totalPages = max(1, (total + WP_PER_PAGE - 1) / WP_PER_PAGE);

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    char hdr[8]; snprintf(hdr, sizeof(hdr), "%02d/%02d", page + 1, totalPages);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("WIFI");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("PASS");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("|");
    if (fromSD) {
        displayManager.setTextColor(TFT_GREEN);  displayManager.printText("SD");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("+");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("NVS");
    } else {
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("NVS");
    }
    displayManager.setTextColor(0x7BEF);     displayManager.printText("]  ");
    displayManager.setTextColor(0x7BEF);     displayManager.println(hdr);
    displayManager.printSeparator();

    if (total == 0) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No saved credentials.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.println("Connect to a network first.");
    } else {
        int start = page * WP_PER_PAGE;
        int end   = min(start + WP_PER_PAGE, total);
        for (int i = start; i < end; i++) {
            const WifiNetwork& n = nets[i];
            displayManager.setCursor(10, displayManager.getCursorY());

            char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.printText(idx);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText("[");
            displayManager.setTextColor(n.fromSD ? TFT_GREEN : TFT_YELLOW);
            displayManager.printText(n.fromSD ? "S" : "N");
            displayManager.setTextColor(0x7BEF);
            displayManager.printText("] ");

            // SSID — hidden networks get ~ prefix in cyan
            char ssid[13];
            snprintf(ssid, sizeof(ssid), "%s%.10s", n.hidden ? "~" : "", n.ssid.c_str());
            displayManager.setTextColor(n.hidden ? TFT_CYAN : TFT_WHITE);
            displayManager.printText(ssid);
            displayManager.printText(" ");

            // credential status
            if (n.open) {
                displayManager.setTextColor(TFT_GREEN);
                displayManager.println("[open]");
            } else if (n.isHashed) {
                displayManager.setTextColor(TFT_RED);
                displayManager.println("[hex-psk]");
            } else {
                displayManager.setTextColor(0x7BEF);
                displayManager.println(n.psk.c_str());
            }

            // BSSID / priority sub-line
            if (!n.bssid.isEmpty() || n.priority > 0) {
                displayManager.setCursor(18, displayManager.getCursorY());
                displayManager.setTextColor(0x4208);
                char sub[32] = "";
                if (!n.bssid.isEmpty() && n.priority > 0)
                    snprintf(sub, sizeof(sub), "%s  p:%d", n.bssid.c_str(), n.priority);
                else if (!n.bssid.isEmpty())
                    snprintf(sub, sizeof(sub), "%s", n.bssid.c_str());
                else
                    snprintf(sub, sizeof(sub), "priority:%d", n.priority);
                displayManager.println(sub);
            }
        }
    }

    displayManager.printSeparator();
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[l]next [a]prev [q]quit");
    displayManager.setTextColor(TFT_WHITE);
}

// ── Export NVS → wpa_supplicant.conf ─────────────────────────────────────────

void wifiExportCommand() {
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("EXPORT");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("WIFI");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");
    displayManager.printSeparator();

    if (!sdCardManager.isReady()) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No SD card.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    auto nets = loadNvsNetworks();
    if (nets.empty()) {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No NVS networks found.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    int ok = 0;
    for (auto& n : nets) {
        displayManager.setCursor(10, displayManager.getCursorY());
        char label[18];
        snprintf(label, sizeof(label), "%-16s", n.ssid.c_str());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText(label);
        int r = appendWpaNetwork(n);
        if (r == 1) {
            displayManager.setTextColor(TFT_GREEN);
            displayManager.println("OK");
            ok++;
        } else if (r == 0) {
            displayManager.setTextColor(0x7BEF);
            displayManager.println("EXISTS");
            ok++;
        } else if (r == -2) {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("WRITE ERR");
        } else {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("NO SD");
        }
        displayManager.setTextColor(TFT_WHITE);
    }

    displayManager.printSeparator();
    displayManager.setCursor(10, displayManager.getCursorY());
    char summary[32];
    snprintf(summary, sizeof(summary), "%d/%d exported", ok, (int)nets.size());
    displayManager.setTextColor(ok == (int)nets.size() ? TFT_GREEN : TFT_YELLOW);
    displayManager.println(summary);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
}

// ── Public API ────────────────────────────────────────────────────────────────

void wifiPassCommand() {
    auto sdNets  = parseWpaSupplicant();
    auto nvsNets = loadNvsNetworks();

    for (auto& n : sdNets)  n.fromSD = true;

    // merge: SD first, then NVS entries not already in SD (dedup by SSID)
    std::vector<WifiNetwork> nets = sdNets;
    for (auto& n : nvsNets) {
        bool dup = false;
        for (auto& s : sdNets) if (s.ssid == n.ssid) { dup = true; break; }
        if (!dup) nets.push_back(n);
    }

    bool hasSD     = !sdNets.empty();
    int page       = 0;
    int totalPages = max(1, ((int)nets.size() + WP_PER_PAGE - 1) / WP_PER_PAGE);
    renderWpPage(nets, page, hasSD);

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if ((k == 'l' || k == 'L') && page < totalPages - 1) { page++; renderWpPage(nets, page, hasSD); }
        else if ((k == 'a' || k == 'A') && page > 0)          { page--; renderWpPage(nets, page, hasSD); }
        else if (LockScreenManager::getInstance().consumeJustUnlocked()) renderWpPage(nets, page, hasSD);
    }
    displayManager.tdeck_begin();
}

WifiNetwork getWifiNetwork(const String& ssid) {
    auto nets = parseWpaSupplicant();
    for (auto& n : nets)
        if (n.ssid == ssid) return n;
    return WifiNetwork();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Escape " → \" and strip control characters so values never break the format
static String sanitize(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\n' || c == '\r') continue;
        if (c == '"')  out += '\\';
        out += c;
    }
    return out;
}

// Copy conf → .bak once before T-Rex ever modifies the file
static void ensureBackup() {
    if (SD.exists("/wpa_supplicant.bak")) return;
    if (!SD.exists(WIFIPASS_PATH))        return;
    File src = SD.open(WIFIPASS_PATH,        FILE_READ);
    File dst = SD.open("/wpa_supplicant.bak", FILE_WRITE);
    if (src && dst) { while (src.available()) dst.write(src.read()); }
    if (src) src.close();
    if (dst) dst.close();
}

int appendWpaNetwork(const WifiNetwork& net) {
    // ── input validation ──────────────────────────────────────────────────────
    if (net.ssid.isEmpty())                    return -1;
    if (!net.open && net.psk.isEmpty())        return -1;
    if (!sdCardManager.isReady())              return -1;

    String ssid = sanitize(net.ssid);
    String psk  = sanitize(net.psk);

    // ── duplicate / upgrade check ────────────────────────────────────────────────
    // Primary: SSID match. Allow append only when upgrading a hashed-only entry to
    // plain (e.g. after user re-enters password for a Linux update_config=1 file).
    // Secondary: BSSID match (only when SSID absent) — same AP already saved, skip.
    if (SD.exists(WIFIPASS_PATH)) {
        auto existing = parseWpaSupplicant();
        bool ssidInFile = false;
        for (const auto& n : existing) {
            if (n.ssid != net.ssid) continue;
            ssidInFile = true;
            if (!n.isHashed) return 0;   // plain PSK already saved — true duplicate
            if (net.isHashed) return 0;  // both hashed — nothing to improve
            break;                        // existing hashed, we have plain — allow append
        }
        if (!ssidInFile && !net.bssid.isEmpty()) {
            for (const auto& n : existing)
                if (n.bssid == net.bssid) return 0;
        }
    }

    // ── backup existing file before first T-Rex modification ─────────────────
    ensureBackup();

    // ── build complete block in memory before touching the file ──────────────
    // Writing as a single string minimises partial-write risk on power loss
    String block = "";
    bool isNew = !SD.exists(WIFIPASS_PATH);
    if (isNew)
        block += "# wpa_supplicant.conf — generated by T-Rex\n"
                 "# Compatible with Linux wpa_supplicant\n"
                 "# Restore from wpa_supplicant.bak if this file is ever corrupted\n"
                 "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
                 "update_config=1\n";

    block += "\nnetwork={\n";
    block += "    ssid=\"" + ssid + "\"\n";

    if (!net.bssid.isEmpty())
        block += "    # bssid=" + net.bssid + "\n";

    if (net.open) {
        block += "    key_mgmt=NONE\n";
    } else {
        block += "    #psk=\"" + psk + "\"\n";
        block += "    psk=\""  + psk + "\"\n";
    }

    if (net.hidden)   block += "    scan_ssid=1\n";
    if (net.priority) block += "    priority=" + String(net.priority) + "\n";

    block += "}\n";

    // ── single write ──────────────────────────────────────────────────────────
    // FILE_APPEND silently fails when the file doesn't exist on a fresh FAT32
    // card on some ESP32 SD library builds — use FILE_WRITE for new files only.
    // Retry once: active WiFi connection keeps GDMA busy and can cause a
    // transient SPI open failure even when the card is healthy.
    auto openMode = isNew ? FILE_WRITE : FILE_APPEND;
    File a = SD.open(WIFIPASS_PATH, openMode);
    if (!a) {
        delay(80);
        a = SD.open(WIFIPASS_PATH, openMode);
        if (!a) return -2;
    }
    a.print(block);
    a.close();
    return 1;
}
