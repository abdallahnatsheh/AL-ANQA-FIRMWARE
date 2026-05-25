// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "man_pages.h"
#include "lockscreen_manager.h"
#include <cctype>

extern InputHandling inputHandler;

// ── Man page data ─────────────────────────────────────────────────────────────

static const int MAN_VISIBLE = 9;  // content lines visible at once

struct ManEntry {
    const char* cmd;
    const char* shortName;
    const char* lines[24]; // nullptr-terminated, max 23 content lines
};

static const ManEntry PAGES[] = {

    { "help", "hlp", {
        "SYNTAX   help [command]",
        "",
        "ABOUT    List commands by category.",
        "         help <cmd> for single command detail.",
        "",
        "KEYS     [l] next  [a] prev  [q] quit",
        "NOTE     man pages: tpad UP/DN scrolls.",
        "         [a]/[l] change page.",
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

    { "lock", "lk", {
        "SYNTAX   lock",
        "         lock new | update | clean | wipe",
        "         lock timeout <s>  |  lock status",
        "",
        "ABOUT    No PIN: press Space x3 to unlock.",
        "         PIN: type password then Enter.",
        "",
        "LOCK     Run 'lock' or hold trackpad 3s.",
        "TIMEOUT  lock timeout <s>  (0 = off)",
        "RECOVER  Forgot? Remove SD + reboot,",
        "         Space x3, then: lock wipe",
        nullptr
    }},

    { "tz", "tz", {
        "SYNTAX   tz               — pick from list",
        "         tz status        — show current TZ + time",
        "         tz +3 | tz -5   — set UTC offset directly",
        "         tz +3:30        — half-hour offset",
        "",
        "ABOUT    Scrollable list of 30+ common timezones.",
        "         UP/DN to move, Enter to select, q to quit.",
        "         WiFi: NTP syncs local time automatically.",
        "         No WiFi: GPS used as source (Plus only).",
        "         Saved to /clock.conf on SD.",
        nullptr
    }},

    { "pwrsave", "psv", {
        "SYNTAX   pwrsave on|off|status",
        "         pwrsave set <option> <value>",
        "         pwrsave save|reset",
        "",
        "ABOUT    Dim + screen-off on inactivity, battery-aware dim.",
        "         Config saved to /pwrsave.conf.",
        "",
        "SET      timeout <s>           — inactivity dim delay",
        "         dimto <0-255>         — dim brightness level",
        "         fullto <0-255>        — full brightness level",
        "         screenoff <s>         — screen-off delay",
        "         screenoffmode on|off  — enable screen-off",
        "         batterymode on|off    — battery-aware dim",
        "         batterythreshold <%>  — dim below this %",
        "         batterydim <0-255>     — battery dim level",
        "",
        "EXAMPLE  pwrsave set timeout 60",
        "         pwrsave set dimto 40",
        "         pwrsave set batterymode on",
        "         pwrsave set batterythreshold 20",
        nullptr
    }},

    { "volume", "vol", {
        "SYNTAX   vol [0-100|up|down|off]",
        "",
        "ABOUT    General audio volume for future",
        "         music player / voice recorder.",
        "         vol alone shows current level.",
        "",
        "OPTIONS  up  +10%   down  -10%",
        "         off mute   0-100 exact value",
        "NOTE     Does not affect notification vol.",
        "         Use: nf vol <n> for that.",
        nullptr
    }},

    { "notif", "nf", {
        "SYNTAX   notif [on|off|status]",
        "         notif vol <0-100>",
        "         notif <level> on|off",
        "         notif <level> file <path>",
        "",
        "LEVELS   alert  warning  success  info  ping",
        "ABOUT    Per-level audio notifications.",
        "         Custom MP3 from /notification/*.mp3",
        "         Config saved to /notif.conf",
        "",
        "EXAMPLE  notif alert off",
        "         notif vol 80",
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
        "FILES    /wpa_supplicant.conf",
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

    { "wguard", "wg", {
        "SYNTAX   wg <idx|bssid> [ch]     interactive",
        "         wg <idx|bssid> [ch] bg  background",
        "         wg stop  |  wg view",
        "",
        "ABOUT    Passive WiFi IDS — monitors one AP",
        "         for known attacks in real time.",
        "",
        "STEPS    1. sw  2. wg <idx>  3. monitor",
        "         bg: wg <idx> bg  then  wg view",
        "DETECTS  Deauth storm, bcast deauth, evil twin",
        "         handshake harvest, BSSID clone,",
        "         beacon/auth/probe flood, Karma",
        "KEYS     [s] save  [q] quit",
        "FILES    /logs/wguard/NNN.csv",
        "NOTE     bg blocks WiFi cmds; wg stop first.",
        nullptr
    }},

    { "beaconflood", "bf", {
        "SYNTAX   bf",
        "         bf list | rickroll | seq <base>",
        "         bf file [path] | bf clone",
        "",
        "ABOUT    Inject fake 802.11 beacons. ~90/s.",
        "         list=funny SSIDs  rick=rickroll",
        "         seq=base+N  file=SD  clone=real AP",
        "         clone needs sw first.",
        "",
        "KEYS     [q] stop",
        "NOTE     Cannot run with wguard at same time.",
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

    { "buddy", "bd", {
        "SYNTAX   bd [name]",
        "",
        "ABOUT    Claude Desktop remote via BLE NUS.",
        "         Shows live stats + ASCII pet.",
        "         Approve/deny permission prompts",
        "         from the T-DECK keyboard.",
        "",
        "KEYS     [y] approve  [n] deny",
        "         [spc] pet  [q] quit",
        "NOTE     Claude Desktop > Developer >",
        "         Hardware Buddy. Stats in NVS.",
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

    { "bleinfo", "bi", {
        "SYNTAX   bi <index|mac|all>",
        "",
        "ABOUT    BLE GATT client — enumerate services,",
        "         read characteristics, interact+audit.",
        "",
        "STEPS    1. sbl  2. bi <idx>  3. use keys below",
        "         bi all — connect+save every sbl result",
        "",
        "KEYS     [n] sniff — notify+indicate, saves SD",
        "         [w] write — hex/ASCII to char",
        "         [f] fuzz — seq/random/boundary",
        "         [b] audit — risk-flagged chars only",
        "         [r] wcap — replay captured notif",
        "         [p] pair — bond + MITM + passkey",
        "         [s] save — GATT tree to /logs/bleinfo/",
        "",
        "RISK     ! high   ~ med   orange low",
        "FILES    /logs/bleinfo/<mac>.txt",
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

    { "fastpair", "fp", {
        "SYNTAX   fp [scan|spam|h <idx>|h all]",
        "",
        "ABOUT    Google Fast Pair attack suite.",
        "         scan — BLE scan for FP devices.",
        "         spam — flood Android pairing popups.",
        "         h <idx> — GATT hijack CVE-2025-36911.",
        "         h all  — test all scanned devices.",
        "",
        "KEYS     [h+#] hijack  [s] spam  [q] quit",
        "FILES    /fastpair_keys.csv  /logs/fastpair.csv",
        "NOTE     Device needs pairing mode for h <idx>.",
        nullptr
    }},

    { "blespam", "bs", {
        "SYNTAX   bs [apple|android|ms|samsung|all]",
        "",
        "ABOUT    BLE notification spam suite.",
        "         apple   - iOS popups (AirPods/Beats)",
        "         android - Google Fast Pair popups",
        "         ms      - Windows Swift Pair popup",
        "         samsung - Galaxy manufacturer flood",
        "         all     - cycle all four vendors",
        "",
        "KEYS     [l/a] next/prev type  [q] stop",
        "NOTE     MAC randomized per advertisement.",
        nullptr
    }},

    { "usbmsc", "um", {
        "SYNTAX   usbmsc",
        "",
        "ABOUT    Expose SD card as USB mass storage.",
        "         SD card mounts on the connected PC.",
        "         All SD access on T-Rex is blocked",
        "         while drive is active.",
        "",
        "KEYS     [q] eject and return to normal mode",
        "NOTE     Eject on PC before pressing q.",
        nullptr
    }},

    { "usbkbd", "uk", {
        "SYNTAX   usbkbd",
        "",
        "ABOUT    T-Deck as USB keyboard + mouse.",
        "         Keyboard types into host. Trackball",
        "         moves cursor (accelerated).",
        "",
        "CLICK    tap=left  hold=right  1.5s=exit",
        "NOTE     BS auto-repeats after 500ms hold.",
        nullptr
    }},

    { "jiggle", "jg", {
        "SYNTAX   jiggle",
        "",
        "ABOUT    Mouse jiggler. Nudges cursor +2/-2px",
        "         every 30s to prevent screen lock.",
        "         Cursor returns to original position.",
        "",
        "USE      Plug T-Deck into target PC via USB,",
        "         run jiggle, leave machine unattended.",
        "",
        "EXIT     q",
        nullptr
    }},

    { "usbexec", "ux", {
        "SYNTAX   usbexec demo",
        "         usbexec <path>",
        "",
        "ABOUT    BadUSB / DuckyScript executor.",
        "         demo=Notepad T-Rex art.",
        "         Scripts in /badusb/ on SD.",
        "",
        "CMDS     REM // DELAY DEFAULT_DELAY",
        "         STRING STRINGLN REPEAT F1-F24",
        "         CTRL-ALT GUI-SHIFT (hyphen ok)",
        "         WAIT_FOR_BUTTON_PRESS",
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
        "ABOUT    List SD directory (non-recursive).",
        "         No arg = current directory (cwd).",
        "         Dirs shown in cyan with trailing /.",
        "         Relative paths resolve from cwd.",
        "",
        "EXAMPLE  ls",
        "         ls /logs",
        "         ls badusb",
        nullptr
    }},

    { "cd", "cd", {
        "SYNTAX   cd <dir|..>",
        "         cd /",
        "",
        "ABOUT    Change the current working directory.",
        "         Affects ls, rm, ux path lookup.",
        "         Relative and absolute paths supported.",
        "         cd with no arg or / returns to root.",
        "",
        "EXAMPLE  cd badusb",
        "         cd /logs",
        "         cd ..",
        nullptr
    }},

    { "cat", "cat", {
        "SYNTAX   cat <path>",
        "",
        "ABOUT    Read and display file from SD.",
        "         Scrollable viewer — up to 400 lines.",
        "         Paths resolve from current directory.",
        "",
        "EXAMPLE  cat /logs/cracked.csv",
        "         cat /pwrsave.conf",
        "KEYS     tpad UP/DN scroll  [q] quit",
        nullptr
    }},

    { "rm", "rm", {
        "SYNTAX   rm <path>",
        "",
        "ABOUT    Delete a file from SD card.",
        "WARNING  No confirmation — immediate.",
        "",
        "EXAMPLE  rm /logs/eviltwin.csv",
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
        "ABOUT    I2S speaker hardware test.",
        "         Raw tones at full volume + notif",
        "         level test using your nf settings.",
        "",
        "KEYS     [1]-[6] raw tones  [s] C scale",
        "         [a]lert [w]arning [c]success",
        "         [i]nfo  [p]ing    [q] quit",
        "NOTE     notif keys honour nf vol + MP3.",
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
    "SYNTAX", "ABOUT", "STEPS", "EXAMPLE", "KEYS",
    "NOTE", "FILES", "OPTIONS", "WARNING", "DETECTS",
    "RECOVER", "LOCK", "TIMEOUT", "UNLOCK", "RISK",
    "CMDS", "MODES", "USE", "EXIT", "CLICK", "LEVELS", nullptr
};

// ── Class implementation ──────────────────────────────────────────────────────

ManPages::ManPages(DisplayManager& dm) : _dm(dm) {}

void ManPages::renderPage(int idx, int scrollTop, int total) {
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

    int contentTop = _dm.getCursorY();
    int y = contentTop;

    // Content — render only the visible window
    for (int i = scrollTop; i < scrollTop + MAN_VISIBLE && i < total; i++) {
        const char* line = pg.lines[i];
        _dm.setCursor(10, y);
        if (line[0] != '\0') {
            bool labeled = false;
            for (int k = 0; LABELS[k]; k++) {
                int llen = strlen(LABELS[k]);
                if (strncmp(line, LABELS[k], llen) == 0 && line[llen] == ' ') {
                    _dm.setTextColor(0x7BEF);
                    _dm.printText(LABELS[k]);
                    _dm.setTextColor(TFT_WHITE);
                    _dm.printText(line + llen);
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

    // Scrollbar — only when content overflows
    if (total > MAN_VISIBLE) {
        int barX    = SCREEN_WIDTH - 5;
        int barH    = MAN_VISIBLE * LINE_HEIGHT;
        int thumbH  = max(4, barH * MAN_VISIBLE / total);
        int maxTop  = total - MAN_VISIBLE;
        int thumbY  = contentTop + (maxTop > 0
                        ? (barH - thumbH) * scrollTop / maxTop
                        : 0);
        _dm.fillRect(barX, contentTop, 3, barH, 0x2104);   // track (dark)
        _dm.fillRect(barX, thumbY,     3, thumbH, TFT_CYAN); // thumb
    }

    // Nav bar — fixed position below content area
    int navY = contentTop + MAN_VISIBLE * LINE_HEIGHT + 2;
    _dm.fillRect(5, navY, 310, 1, TFT_CYAN);
    _dm.setCursor(10, navY + 3);
    _dm.setTextColor(0x7BEF);    _dm.printText("tpad ");
    _dm.setTextColor(TFT_GREEN); _dm.printText("UP/DN");
    _dm.setTextColor(0x7BEF);    _dm.printText(" scroll  [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("a");
    _dm.setTextColor(0x7BEF);    _dm.printText("]prev [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("l");
    _dm.setTextColor(0x7BEF);    _dm.printText("]next [");
    _dm.setTextColor(TFT_GREEN); _dm.printText("q");
    _dm.setTextColor(0x7BEF);    _dm.printText("]quit");
    _dm.setTextColor(TFT_WHITE);
}

// Count non-null lines in a ManEntry
static int countLines(const ManEntry& pg) {
    int n = 0;
    while (n < 23 && pg.lines[n]) n++;
    return n;
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

    int scrollTop = 0;
    int total     = countLines(PAGES[found]);
    renderPage(found, scrollTop, total);

    while (true) {
        char         key = inputHandler.getKeyboardInput();
        TrackballEvent evt = inputHandler.getTrackballEvent();

        if (key == 'q' || key == 'Q') { _dm.clearInputText(); return; }

        bool redraw = LockScreenManager::getInstance().consumeJustUnlocked();

        // Scroll within page
        if ((evt == TBALL_UP)   && scrollTop > 0) {
            scrollTop--; redraw = true;
        }
        if ((evt == TBALL_DOWN) && scrollTop < total - MAN_VISIBLE) {
            scrollTop++; redraw = true;
        }

        // Navigate between man pages (keyboard only — trackball left/right excluded, too sensitive)
        if ((key == 'l' || key == 'L') && found < PAGE_COUNT - 1) {
            found++; scrollTop = 0; total = countLines(PAGES[found]); redraw = true;
        }
        if ((key == 'a' || key == 'A') && found > 0) {
            found--; scrollTop = 0; total = countLines(PAGES[found]); redraw = true;
        }

        if (redraw) renderPage(found, scrollTop, total);
    }
}
