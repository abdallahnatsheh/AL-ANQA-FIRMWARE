// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "man_pages.h"
#include <cctype>

extern InputHandling inputHandler;

// ── Man page data ─────────────────────────────────────────────────────────────

struct ManEntry {
    const char* cmd;
    const char* shortName;
    const char* lines[12]; // nullptr-terminated, max 11 content lines
};

static const ManEntry PAGES[] = {

    { "help", "hlp", {
        "SYNTAX   help [command]",
        "",
        "ABOUT    List commands by category.",
        "         help <cmd> for single command detail.",
        "",
        "KEYS     [l] next  [a] prev  [q] quit",
        "EXAMPLE  help wpasniff",
        nullptr
    }},

    { "info", "inf", {
        "SYNTAX   info",
        "",
        "ABOUT    Device info — 3 pages:",
        "         1. Chip/RAM/flash   2. MACs",
        "         3. LoRa/GPS/battery",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        nullptr
    }},

    { "clear", "clr", {
        "SYNTAX   clear",
        "",
        "ABOUT    Clear screen and reset prompt.",
        nullptr
    }},

    { "MATRIX", "matrix", {
        "SYNTAX   MATRIX",
        "",
        "ABOUT    Matrix digital rain animation.",
        "KEYS     [q] exit",
        nullptr
    }},

    { "pwrsave", "psv", {
        "SYNTAX   pwrsave on|off|status",
        "         pwrsave set <option> <value>",
        "",
        "ABOUT    Dim on inactivity + battery level.",
        "         Config saved to /pwrsave.conf.",
        "",
        "OPTIONS  dim_timeout <secs>  dim_lvl <0-255>",
        "         bat_threshold <%>  bat_dim_lvl <0-255>",
        "EXAMPLE  pwrsave set dim_timeout 60",
        nullptr
    }},

    { "show", "sh", {
        "SYNTAX   show <wifi|ble|hosts>",
        "",
        "ABOUT    Re-display the last scan result",
        "         without running a new scan.",
        "",
        "OPTIONS  wifi  — last scanwifi table",
        "         ble   — last scanblue table",
        "         hosts — last netdiscover table",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        "NOTE     'No scan data' if not run yet.",
        nullptr
    }},

    { "scanwifi", "sw", {
        "SYNTAX   scanwifi",
        "",
        "ABOUT    Scan nearby WiFi — SSID, RSSI,",
        "         channel, open/WPA. Hidden SSIDs",
        "         show as ~name from SD cache.",
        "",
        "KEYS     [l]/[a] pages  [u] rescan  [q]",
        "NOTE     Index # used by cw da et hs ws.",
        nullptr
    }},

    { "connectwifi", "cw", {
        "SYNTAX   cw <index>",
        "",
        "ABOUT    Connect to network from last scan.",
        "         Password saved in NVS — not re-asked.",
        "",
        "EXAMPLE  sw  then  cw 2",
        nullptr
    }},

    { "clearwifi", "clrw", {
        "SYNTAX   clearwifi",
        "",
        "ABOUT    Erase all saved WiFi passwords.",
        "NOTE     Does not disconnect active session.",
        nullptr
    }},

    { "wifipass", "wp", {
        "SYNTAX   wifipass",
        "",
        "ABOUT    Browse saved WiFi passwords.",
        "         Reads wpa_supplicant.conf from SD.",
        "         Falls back to NVS if no SD card.",
        "",
        "KEYS     [a]prev [l]next [q]quit",
        nullptr
    }},

    { "wifiexport", "wex", {
        "SYNTAX   wifiexport",
        "",
        "ABOUT    Copy NVS networks to SD card.",
        "         Writes to /wpa_supplicant.conf.",
        "         Skips duplicates already on SD.",
        "         File stays Linux-compatible.",
        "",
        "FILE     /wpa_supplicant.conf",
        nullptr
    }},

    { "wifimon", "wm", {
        "SYNTAX   wm [channel]",
        "",
        "ABOUT    WiFi monitor mode / packet sniffer.",
        "         0 = channel hop, 1-13 = fixed.",
        "",
        "EXAMPLE  wm 6  (fixed)  |  wm 0  (hop)",
        "KEYS     [q] stop",
        nullptr
    }},

    { "deauth", "da", {
        "SYNTAX   da <bssid|#> [ch] [client]",
        "",
        "ABOUT    802.11 deauth to kick clients.",
        "         Broadcast or targeted one client.",
        "",
        "EXAMPLE  da 2",
        "         da AA:BB:CC:DD:EE:FF 6",
        "         da 2 6 CC:DD:EE:FF:00:11",
        "KEYS     [q] stop",
        nullptr
    }},

    { "eviltwin", "et", {
        "SYNTAX   et [ssid]",
        "",
        "ABOUT    Clone AP + captive portal.",
        "         Open=clone MAC. WPA=random MAC.",
        "         Deauth pauses on client connect.",
        "",
        "KEYS     [c] creds  [s] save  [q] quit",
        "FILES    /evilportal/*.html  /logs/eviltwin.csv",
        nullptr
    }},

    { "hiddenssid", "hs", {
        "SYNTAX   hs <idx|bssid> [ch] [silent]",
        "",
        "ABOUT    Reveal hidden SSID via deauth +",
        "         probe-response sniff. Saved to SD,",
        "         shown as ~name in scanwifi table.",
        "",
        "EXAMPLE  hs 3",
        "         hs AA:BB:CC:DD:EE:FF 11 silent",
        "KEYS     [q] stop",
        nullptr
    }},

    { "macchanger", "mc", {
        "SYNTAX   mc [on|off|random|set <mac>]",
        "         mc restore on|off",
        "         mc target wifi|bt|both",
        "",
        "ABOUT    Spoof WiFi/BLE MAC. Auto-random",
        "         on scan/connect when enabled.",
        "         Config: /macchanger.conf",
        "",
        "EXAMPLE  mc on  |  mc random  |  mc off",
        "         mc set 02:AB:CD:EF:12:34",
        "         mc target bt",
        nullptr
    }},

    { "wpasniff", "ws", {
        "SYNTAX   ws <idx|bssid> [ch]",
        "",
        "ABOUT    Capture WPA2 handshake + crack.",
        "         EAPOL sniff + deauth every 4s.",
        "",
        "STEPS    1. sw  2. ws <idx>  3. wait",
        "         4. c  to crack on-device",
        "KEYS     [c] crack  [q] quit",
        "FILES    /wordlist.txt  (SD wordlist)",
        "         /logs/cracked.csv  (results)",
        "NOTE     Built-in 100 pwds if no SD card.",
        nullptr
    }},

    { "netdiscover", "nd", {
        "SYNTAX   netdiscover",
        "",
        "ABOUT    ARP scan local /24 subnet.",
        "         Shows IP, MAC, hostname.",
        "         Requires active WiFi connection.",
        "",
        "KEYS     [l]/[a] pages  [u] rescan  [q]",
        "NOTE     Use index # in ps and ts.",
        nullptr
    }},

    { "portscan", "ps", {
        "SYNTAX   ps <ip|#> <start> <end>",
        "",
        "ABOUT    TCP scan — 4 parallel tasks,",
        "         150ms timeout. b = banner grab.",
        "",
        "EXAMPLE  ps 192.168.1.1 1 1024",
        "         ps 3 80 443  (use ARP index #3)",
        "KEYS     [b] banner  [l]/[a] pages  [q]",
        nullptr
    }},

    { "topscan", "ts", {
        "SYNTAX   ts <ip|#>",
        "",
        "ABOUT    Scan top 26 common ports:",
        "         80 443 22 21 23 25 3389 8080",
        "         3306 5432 6379 27017 and more.",
        "",
        "EXAMPLE  ts 192.168.1.1",
        "         ts 2   (use ARP index #2)",
        nullptr
    }},

    { "ping", "pg", {
        "SYNTAX   pg <ip|hostname>",
        "",
        "ABOUT    ICMP ping — 5 packets.",
        "         Shows min/avg/max RTT + loss %.",
        "",
        "EXAMPLE  pg 192.168.1.1",
        "         pg google.com",
        nullptr
    }},

    { "scanblue", "sbl", {
        "SYNTAX   scanblue",
        "",
        "ABOUT    BLE device scan — name, MAC, RSSI.",
        "         Paginated table.",
        "",
        "KEYS     [l]/[a] pages  [q] quit",
        "NOTE     Run before trackme for a baseline.",
        nullptr
    }},

    { "trackme", "tm", {
        "SYNTAX   tm [silent]",
        "",
        "ABOUT    Anti-tracking scanner. 60s baseline",
        "         learns your devices. Gate3 (200m GPS)",
        "         needed for WARNING/ALERT.",
        "",
        "STEPS    1. gpson  2. tm (start before leaving)",
        "         3. Move 200m to confirm Gate3",
        "KEYS     [w] whitelist  [s] save  [q] quit",
        "FILES    /logs/trackme_known.csv  whitelist",
        nullptr
    }},

    { "sdinfo", "sdi", {
        "SYNTAX   sdinfo",
        "",
        "ABOUT    Show SD card type and capacity.",
        nullptr
    }},

    { "sdls", "ls", {
        "SYNTAX   ls [path]",
        "",
        "ABOUT    List files in SD directory.",
        "         Default path is root /.",
        "",
        "EXAMPLE  ls /logs",
        "         ls /evilportal",
        nullptr
    }},

    { "sdread", "sdr", {
        "SYNTAX   sdr <path>",
        "",
        "ABOUT    Read and display file from SD.",
        "         Long files are paginated.",
        "",
        "EXAMPLE  sdr /logs/cracked.csv",
        "         sdr /pwrsave.conf",
        nullptr
    }},

    { "sdrm", "srm", {
        "SYNTAX   srm <path>",
        "",
        "ABOUT    Delete a file from SD card.",
        "WARNING  No confirmation — immediate.",
        "",
        "EXAMPLE  srm /logs/eviltwin.csv",
        nullptr
    }},

    { "sdformat", "sdf", {
        "SYNTAX   sdf [init]",
        "",
        "ABOUT    Format SD card to FAT32.",
        "WARNING  Destroys all data. Press y.",
        "MODES    sdf init - Format + init",
        nullptr
    }},

    { "gpson", "gon", {
        "SYNTAX   gpson",
        "",
        "ABOUT    Start GPS background task.",
        "         Shows live NMEA + fix status.",
        "         Task keeps running after quit.",
        "",
        "NOTE     Cold fix ~4 min outdoors.",
        "         Run before trackme for GPS data.",
        "         T-Deck Plus only.",
        nullptr
    }},

    { "gpsoff", "gof", {
        "SYNTAX   gpsoff",
        "",
        "ABOUT    Stop the GPS background task.",
        "NOTE     T-Deck Plus only.",
        nullptr
    }},

    { "gpstest", "gt", {
        "SYNTAX   gpstest",
        "",
        "ABOUT    Print GPS coordinates — lat, lon,",
        "         altitude, speed, satellite count.",
        "NOTE     T-Deck Plus only.",
        nullptr
    }},

    { "spktest", "st", {
        "SYNTAX   spktest",
        "",
        "ABOUT    Play tone sequence via I2S speaker",
        "         to verify audio hardware.",
        nullptr
    }},

    { "loratest", "lt", {
        "SYNTAX   loratest",
        "",
        "ABOUT    LoRa SX1262 diagnostic — init,",
        "         TX test, RX monitor, freq switch",
        "         868/915 MHz.",
        "KEYS     [q] stop RX monitor",
        nullptr
    }},
};

static const int PAGE_COUNT = (int)(sizeof(PAGES) / sizeof(PAGES[0]));

// ── Label keywords colored grey in output ─────────────────────────────────────

static const char* LABELS[] = {
    "SYNTAX", "ABOUT", "STEPS", "EXAMPLE",
    "KEYS", "NOTE", "FILES", "OPTIONS", "WARNING", nullptr
};

// ── Class implementation ──────────────────────────────────────────────────────

ManPages::ManPages(DisplayManager& dm) : _dm(dm) {}

void ManPages::renderPage(int idx) {
    const ManEntry& pg = PAGES[idx];

    _dm.clearScreen();
    _dm.setCursor(10, outputY);

    // Header: [MAN::CMDNAME]  idx/total
    char upname[20]; int ci = 0;
    for (const char* p = pg.cmd; *p && ci < 19; p++, ci++)
        upname[ci] = toupper((unsigned char)*p);
    upname[ci] = '\0';
    char pgbuf[8]; snprintf(pgbuf, sizeof(pgbuf), "%d/%d", idx + 1, PAGE_COUNT);

    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("MAN");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.printText(upname);
    _dm.setTextColor(0x7BEF);    _dm.printText("]  ");
    _dm.setTextColor(0x7BEF);    _dm.println(pgbuf);
    _dm.printSeparator();

    int y = _dm.getCursorY();

    for (int i = 0; i < 12 && pg.lines[i]; i++) {
        const char* line = pg.lines[i];
        _dm.setCursor(10, y);

        if (line[0] != '\0') {
            // Check for label keyword (e.g. "SYNTAX   ...")
            bool labeled = false;
            for (int k = 0; LABELS[k]; k++) {
                int llen = strlen(LABELS[k]);
                if (strncmp(line, LABELS[k], llen) == 0 && line[llen] == ' ') {
                    _dm.setTextColor(0x7BEF);
                    _dm.printText(LABELS[k]);
                    _dm.setTextColor(TFT_WHITE);
                    _dm.printText(line + llen); // includes padding spaces
                    labeled = true;
                    break;
                }
            }
            if (!labeled) {
                _dm.setTextColor(TFT_WHITE);
                _dm.printText(line);
            }
        }
        y += LINE_HEIGHT;
    }

    // Nav bar
    _dm.fillRect(5, y + 1, 310, 1, TFT_CYAN);
    _dm.setCursor(10, y + 4);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_GREEN); _dm.printText("a");
    _dm.setTextColor(0x7BEF);    _dm.printText("]prev [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("l");
    _dm.setTextColor(0x7BEF);    _dm.printText("]next [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("q");
    _dm.setTextColor(0x7BEF);    _dm.printText("]quit");
    _dm.setTextColor(TFT_WHITE);
}

void ManPages::show(char* args) {
    if (!args || !*args) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Usage: man <command>");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Example: man wpasniff");
        _dm.printCommandScreen();
        return;
    }

    int found = -1;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (strcmp(args, PAGES[i].cmd) == 0 || strcmp(args, PAGES[i].shortName) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        _dm.setCursor(10, _dm.getCursorY());
        char buf[52]; snprintf(buf, sizeof(buf), "No manual entry for '%s'.", args);
        _dm.println(buf);
        _dm.printCommandScreen();
        return;
    }

    while (true) {
        renderPage(found);
        while (true) {
            char key = inputHandler.getKeyboardInput();
            if (key == 'q' || key == 'Q') { _dm.clearInputText(); return; }
            if ((key == 'l' || key == 'L') && found < PAGE_COUNT - 1) { found++; break; }
            if ((key == 'a' || key == 'A') && found > 0)              { found--; break; }
        }
    }
}
