#include "command_manager.h"
#include "display_manager.h"
#include "utils.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "network_scanner.h"
#include "bluetooth_functions.h"
#include "sdcard_manager.h"
#include "wifimon_functions.h"
#include "deauth_functions.h"
#include "trackme.h"
#include "eviltwin.h"
#include "hidden_ssid.h"
#include "handshake_capture.h"
#include "powersave_manager.h"
#include "mac_changer.h"
#include "man_pages.h"
#include "wifi_creds.h"
#include "fast_pair.h"
#include "ble_spam.h"
#include "usb_manager.h"
#include "usb_keyboard.h"
#include "bad_usb.h"
#include "buddy.h"
#include "ble_info.h"
extern DisplayManager     displayManager;
extern ESPInfoPrinter     espInfoPrinter;
extern WiFiFunctions      wifiFunctions;
extern NetworkScanner     networkScanner;
extern BluetoothFunctions bluetoothFunctions;
extern SDCardManager      sdCardManager;
extern WiFiMonitor        wifiMonitor;
extern DeauthAttack       deauthAttack;
extern TrackMeScanner     trackMe;
extern EvilTwin           evilTwin;
extern HiddenSSID         hiddenSSID;
extern HandshakeCapture   handshakeCapture;
extern ManPages           manPages;

// Forward declarations for standalone command functions
void runGpsOn();
void runGpsOff();
void runGpsTest();
void runSpeakerTest();
void runLoraTest();

CommandManager::CommandManager()
    : commandIndex(0), commandCount(0), _cursorPos(0),
      _histCount(0), _histHead(0), _histNav(-1) {
    memset(_hist, 0, sizeof(_hist));
    memset(_histSaved, 0, sizeof(_histSaved));
    resetCommand();
}

void CommandManager::registerCommand(const char* name, const char* shortName, CommandFunction function,
                                     const char* description, bool haveArgs, const char* category,
                                     CompType compType) {
    if (commandCount < sizeof(commands) / sizeof(commands[0])) {
        commands[commandCount++] = {name, shortName, function, description, haveArgs, category, compType};
    }
}

void CommandManager::processInput(char incoming) {
    if (incoming == 0) return;

    if (incoming == '\n' || incoming == '\r') {
        displayManager.newLine();
        executeCommand();
    } else if (incoming == KEY_AUTOCOMPLETE) {
        doAutocomplete();
    } else if (incoming == '\b') {
        if (_cursorPos > 0) {
            for (int i = _cursorPos - 1; i < (int)commandIndex - 1; i++)
                command[i] = command[i + 1];
            commandIndex--;
            command[commandIndex] = '\0';
            _cursorPos--;
            displayManager.redrawCommandLine(command, _cursorPos);
        }
    } else if (commandIndex < bufferSize - 1) {
        for (int i = (int)commandIndex; i > _cursorPos; i--)
            command[i] = command[i - 1];
        command[_cursorPos] = incoming;
        commandIndex++;
        _cursorPos++;
        command[commandIndex] = '\0';
        displayManager.redrawCommandLine(command, _cursorPos);
    }
}

void CommandManager::processTrackball(TrackballEvent evt) {
    switch (evt) {
        case TBALL_LEFT:
            if (_cursorPos > 0) {
                _cursorPos--;
                displayManager.redrawCommandLine(command, _cursorPos);
            }
            break;
        case TBALL_RIGHT:
            if (_cursorPos < (int)commandIndex) {
                _cursorPos++;
                displayManager.redrawCommandLine(command, _cursorPos);
            }
            break;
        case TBALL_UP:
            if (_histCount == 0) break;
            if (_histNav == -1) {
                strncpy(_histSaved, command, bufferSize - 1);
                _histSaved[bufferSize - 1] = '\0';
                _histNav = 0;
            } else if (_histNav < _histCount - 1) {
                _histNav++;
            } else {
                break;
            }
            {
                int idx = (_histHead - 1 - _histNav + kHistSize * 2) % kHistSize;
                strncpy(command, _hist[idx], bufferSize - 1);
                command[bufferSize - 1] = '\0';
                commandIndex = strlen(command);
                _cursorPos   = commandIndex;
                displayManager.redrawCommandLine(command, _cursorPos);
            }
            break;
        case TBALL_DOWN:
            if (_histNav == -1) break;
            if (_histNav > 0) {
                _histNav--;
                int idx = (_histHead - 1 - _histNav + kHistSize * 2) % kHistSize;
                strncpy(command, _hist[idx], bufferSize - 1);
                command[bufferSize - 1] = '\0';
                commandIndex = strlen(command);
                _cursorPos   = commandIndex;
                displayManager.redrawCommandLine(command, _cursorPos);
            } else {
                _histNav = -1;
                strncpy(command, _histSaved, bufferSize - 1);
                command[bufferSize - 1] = '\0';
                commandIndex = strlen(command);
                _cursorPos   = commandIndex;
                displayManager.redrawCommandLine(command, _cursorPos);
            }
            break;
        case TBALL_CLICK:
            displayManager.newLine();
            executeCommand();
            break;
        default:
            break;
    }
}

void CommandManager::executeCommand() {
    // Push to history (skip empty, skip duplicate of last)
    if (commandIndex > 0) {
        bool isDup = (_histCount > 0) &&
                     (strcmp(command, _hist[(_histHead - 1 + kHistSize) % kHistSize]) == 0);
        if (!isDup) {
            strncpy(_hist[_histHead], command, bufferSize - 1);
            _hist[_histHead][bufferSize - 1] = '\0';
            _histHead = (_histHead + 1) % kHistSize;
            if (_histCount < kHistSize) _histCount++;
        }
    }
    _histNav = -1;

    displayManager.clearInputText();
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    for (int i = 0; i < commandCount; i++) {
        if ((commands[i].haveArgs && (Utils::matchesCmd(command, commands[i].name) || Utils::matchesCmd(command, commands[i].shortName))) ||
            (!commands[i].haveArgs && strcmp(command, commands[i].name) == 0) ||
            (!commands[i].haveArgs && strcmp(command, commands[i].shortName) == 0)) {

            char* args = NULL;

            if (commands[i].haveArgs) {
                const char* cmdPrefix = (Utils::matchesCmd(command, commands[i].shortName)) ? commands[i].shortName : commands[i].name;
                args = command + strlen(cmdPrefix);
                if (*args == ' ') {
                    args++;
                }
                if (args[0] == '\0') {
                    args = NULL;
                }
            }
            commands[i].function(args);
            resetCommand();
            return;
        }
    }
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_RED);    displayManager.printText("ERR");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::CMD]  '");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText(command);
    displayManager.setTextColor(0x7BEF);     displayManager.println("' not found");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);     displayManager.println("type help for commands");
    displayManager.printCommandScreen();
    resetCommand();
}


void CommandManager::resetCommand() {
    memset(command, 0, bufferSize);
    commandIndex = 0;
    _cursorPos   = 0;
}

void CommandManager::doAutocomplete() {
    // Find start of the word being completed (scan left from cursor to last space)
    int wordStart = _cursorPos;
    while (wordStart > 0 && command[wordStart - 1] != ' ') wordStart--;

    int  prefixLen = _cursorPos - wordStart;
    char prefix[128];
    strncpy(prefix, command + wordStart, prefixLen);
    prefix[prefixLen] = '\0';

    static const int kMaxMatch = 16;
    char matches[kMaxMatch][128];
    int  matchCount = 0;

    if (wordStart == 0) {
        // ── Complete command names ────────────────────────────────────────────
        for (int i = 0; i < commandCount && matchCount < kMaxMatch; i++) {
            if (strncmp(commands[i].name, prefix, prefixLen) == 0) {
                bool dup = false;
                for (int j = 0; j < matchCount; j++)
                    if (strcmp(matches[j], commands[i].name) == 0) { dup = true; break; }
                if (!dup) { strncpy(matches[matchCount], commands[i].name, 127); matches[matchCount++][127] = '\0'; }
            }
        }
    } else {
        // ── Complete file/dir names from SD ───────────────────────────────────
        // Find which command is active (first word before wordStart)
        CompType activeComp = COMP_ANY;
        {
            int ce = wordStart;
            while (ce > 0 && command[ce-1] == ' ') ce--;
            int cs = ce;
            while (cs > 0 && command[cs-1] != ' ') cs--;
            char cmdWord[64] = "";
            strncpy(cmdWord, command + cs, ce - cs); cmdWord[ce - cs] = '\0';
            for (int i = 0; i < commandCount; i++) {
                if (strcmp(commands[i].name, cmdWord) == 0 || strcmp(commands[i].shortName, cmdWord) == 0) {
                    activeComp = commands[i].compType;
                    break;
                }
            }
        }
        if (activeComp == COMP_NONE) return;  // command takes no file args

        bool dirsOnly  = (activeComp == COMP_DIR);
        bool filesOnly = (activeComp == COMP_FILE);

        if (sdCardManager.isReady()) {
            char searchDir[128];
            char filePrefix[64] = "";
            const char* lastSlash = strrchr(prefix, '/');
            if (lastSlash) {
                int pathLen = (lastSlash - prefix) + 1;
                char pathPart[128];
                strncpy(pathPart, prefix, pathLen); pathPart[pathLen] = '\0';
                sdCardManager.resolvePath(pathPart, searchDir, sizeof(searchDir));
                strncpy(filePrefix, lastSlash + 1, sizeof(filePrefix) - 1);
            } else {
                strncpy(searchDir,  sdCardManager.getCwd(), sizeof(searchDir) - 1);
                strncpy(filePrefix, prefix,                 sizeof(filePrefix) - 1);
            }
            searchDir[sizeof(searchDir) - 1]   = '\0';
            filePrefix[sizeof(filePrefix) - 1]  = '\0';

            char raw[kMaxMatch][64];
            int  rawCount = sdCardManager.listCompletions(searchDir, filePrefix, raw, kMaxMatch, dirsOnly, filesOnly);
            for (int i = 0; i < rawCount && matchCount < kMaxMatch; i++) {
                char full[128] = "";
                if (lastSlash) {
                    int pathLen = (lastSlash - prefix) + 1;
                    strncpy(full, prefix, pathLen); full[pathLen] = '\0';
                }
                strncat(full, raw[i], sizeof(full) - strlen(full) - 1);
                strncpy(matches[matchCount], full, 127); matches[matchCount++][127] = '\0';
            }
        }
    }

    if (matchCount == 0) return;  // no match — do nothing (Linux beep equivalent)

    // ── Find common prefix among all matches ──────────────────────────────────
    char common[128];
    strncpy(common, matches[0], sizeof(common) - 1); common[sizeof(common) - 1] = '\0';
    int commonLen = strlen(common);
    for (int i = 1; i < matchCount; i++) {
        int j = 0;
        while (j < commonLen && common[j] == matches[i][j]) j++;
        commonLen = j;
    }
    common[commonLen] = '\0';

    // ── Insert common prefix into command buffer (replace current prefix) ─────
    if (commonLen > prefixLen) {
        int tailLen = commandIndex - _cursorPos;
        if (wordStart + commonLen + tailLen < (int)bufferSize - 1) {
            memmove(command + wordStart + commonLen, command + _cursorPos, tailLen + 1);
            memcpy(command + wordStart, common, commonLen);
            commandIndex = wordStart + commonLen + tailLen;
            _cursorPos   = wordStart + commonLen;
            command[commandIndex] = '\0';
        }
        prefixLen = commonLen;
    }

    // ── Single match ──────────────────────────────────────────────────────────
    if (matchCount == 1) {
        bool addedSpace = false;
        if (wordStart == 0 && command[_cursorPos] == '\0' && commandIndex < (int)bufferSize - 1) {
            command[commandIndex++] = ' ';
            _cursorPos++;
            command[commandIndex] = '\0';
            addedSpace = true;
        }
        // After completing a command name, show CWD contents filtered by the
        // command's CompType — so cd shows dirs only, sdr shows files only, etc.
        if (addedSpace && sdCardManager.isReady()) {
            // Look up the completed command's CompType
            CompType ct = COMP_NONE;
            for (int i = 0; i < commandCount; i++) {
                if (strcmp(commands[i].name, matches[0]) == 0 || strcmp(commands[i].shortName, matches[0]) == 0) {
                    ct = commands[i].compType; break;
                }
            }
            if (ct == COMP_NONE) {
                displayManager.redrawCommandLine(command, _cursorPos);
                return;
            }
            char raw[kMaxMatch][64];
            int rawCount = sdCardManager.listCompletions(sdCardManager.getCwd(), "", raw, kMaxMatch,
                                                         ct == COMP_DIR, ct == COMP_FILE);
            if (rawCount > 0) {
                int shown = rawCount < 8 ? rawCount : 8;
                displayManager.setCursor(10, displayManager.getCursorY());
                displayManager.println("");
                for (int i = 0; i < shown; i++) {
                    displayManager.setCursor(10, displayManager.getCursorY());
                    bool isD = (raw[i][strlen(raw[i]) - 1] == '/');
                    displayManager.setTextColor(isD ? TFT_CYAN : TFT_WHITE);
                    displayManager.println(raw[i]);
                    displayManager.setTextColor(TFT_WHITE);
                }
                if (rawCount > 8) {
                    displayManager.setCursor(10, displayManager.getCursorY());
                    displayManager.setTextColor(0x4208);
                    char more[24]; snprintf(more, sizeof(more), "... %d more", rawCount - 8);
                    displayManager.println(more);
                    displayManager.setTextColor(TFT_WHITE);
                }
                displayManager.printCommandScreen();
            }
        }
        displayManager.redrawCommandLine(command, _cursorPos);
        return;
    }

    // ── Multiple matches: print them, redraw prompt below ────────────────────
    int shown = matchCount < 8 ? matchCount : 8;  // cap at 8 visible entries
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("");
    for (int i = 0; i < shown; i++) {
        displayManager.setCursor(10, displayManager.getCursorY());
        bool isDir = (matches[i][strlen(matches[i]) - 1] == '/');
        if (isDir) {
            displayManager.setTextColor(TFT_CYAN);
            displayManager.println(matches[i]);
            displayManager.setTextColor(TFT_WHITE);
        } else if (wordStart == 0) {
            displayManager.setTextColor(TFT_GREEN);
            displayManager.println(matches[i]);
            displayManager.setTextColor(TFT_WHITE);
        } else {
            displayManager.println(matches[i]);
        }
    }
    if (matchCount > 8) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x4208);
        char more[24]; snprintf(more, sizeof(more), "... %d more", matchCount - 8);
        displayManager.println(more);
        displayManager.setTextColor(TFT_WHITE);
    }
    displayManager.printCommandScreen();
    displayManager.redrawCommandLine(command, _cursorPos);
}

void CommandManager::setupCommands() {
    // ── System ────────────────────────────────────────────────────────────────
    registerCommand("show",        "sh",     [](char* a) {
        if      (!a || !*a)              { displayManager.println("Usage: show <wifi|ble|hosts>"); displayManager.printCommandScreen(); }
        else if (strcmp(a,"wifi")  == 0) { wifiFunctions.showWiFiResults(); }
        else if (strcmp(a,"ble")   == 0) { bluetoothFunctions.showBleResults(); }
        else if (strcmp(a,"hosts") == 0) { networkScanner.showHostResults(); }
        else                             { displayManager.println("Usage: show <wifi|ble|hosts>"); displayManager.printCommandScreen(); }
    },                                                                                                 "Show last scan: wifi|ble|hosts",          true,  "System");
    registerCommand("help",        "hlp",    [](char* a) { Utils::printHelp(a); },                                          "Help [cmd]",                              true,  "System");
    registerCommand("man",         "mn",     [](char* a) { manPages.show(a); },                                               "Manual: man <command>",                   true,  "System");
    registerCommand("info",        "inf",    [](char* a) { espInfoPrinter.printESPInfo(); },                                 "Device info (IP, MAC, battery)",          false, "System");
    registerCommand("clear",       "clr",    [](char* a) { displayManager.tdeck_begin(); },                                  "Clear screen",                            false, "System");
    registerCommand("MATRIX",      "matrix", [](char* a) { displayManager.launchMatrixAnimation(); },                       "Matrix rain animation",                   false, "System");
    registerCommand("pwrsave",     "psv",    [](char* a) { PowerSaveManager::handleCommand(a); },                         "Power save: on/off/set/status",  true,  "System");
    // ── WiFi ──────────────────────────────────────────────────────────────────
    registerCommand("scanwifi",    "sw",     [](char* a) { wifiFunctions.scanWiFiNetworks(); },                              "Scan WiFi networks",                      false, "WiFi");
    registerCommand("connectwifi", "cw",     [](char* a) { wifiFunctions.connectToWiFiCommand(a); },                        "Connect to WiFi: cw <index>",             true,  "WiFi");
    registerCommand("clearwifi",   "clrw",   [](char* a) { wifiFunctions.clearAllWiFiCredentials(); },                      "Clear saved WiFi credentials",            false, "WiFi");
    registerCommand("wifimon",     "wm",     [](char* a) { wifiMonitor.start(a && *a ? atoi(a) : 0); },                    "WiFi monitor [ch 1-13, 0=hop]",           true,  "WiFi");
    registerCommand("deauth",      "da",     [](char* a) { deauthAttack.start(a); },                                        "Deauth: da <bssid> [ch] [client]",        true,  "WiFi");
    registerCommand("eviltwin",    "et",     [](char* a) { evilTwin.start(a); },                                            "Evil Twin AP + captive portal",           true,  "WiFi");
    registerCommand("hiddenssid",  "hs",     [](char* a) { hiddenSSID.start(a); },                                          "Uncover hidden SSID: hs <idx|bssid> [ch] [silent]", true,  "WiFi");
    registerCommand("macchanger",  "mc",     [](char* a) { MacChanger::getInstance().handleCommand(a); },                   "MAC spoof: mc on/off/random/set <mac>",              true,  "WiFi");
    registerCommand("wpasniff",    "ws",     [](char* a) { handshakeCapture.start(a); },                                      "WPA2 handshake: ws <idx|bssid> [ch]",                true,  "WiFi");
    registerCommand("wifipass",    "wp",     [](char* a) { wifiPassCommand(); },                                               "Saved WiFi passwords",                    false, "WiFi");
    registerCommand("wifiexport",  "wex",    [](char* a) { wifiExportCommand(); },                                             "Export NVS networks to wpa_supplicant",   false, "WiFi");
    // ── Network ───────────────────────────────────────────────────────────────
    registerCommand("netdiscover", "nd",     [](char* a) { networkScanner.networkDiscovery(); },                             "ARP scan local subnet",                   false, "Network");
    registerCommand("portscan",    "ps",     [](char* a) { networkScanner.networkPortScan(a); },                            "Port scan: ps <ip|#> <start> <end>",      true,  "Network");
    registerCommand("topscan",     "ts",     [](char* a) { networkScanner.topPortScan(a); },                                "Top 26 ports: ts <ip|#>",                 true,  "Network");
    registerCommand("ping",        "pg",     [](char* a) { networkScanner.pingHost(a); },                                   "Ping: pg <ip or hostname>",               true,  "Network");
    // ── Bluetooth ─────────────────────────────────────────────────────────────
    registerCommand("scanblue",    "sbl",    [](char* a) { bluetoothFunctions.scanBluetoothDevices(); },                    "BLE device scan",                         false, "Bluetooth");
    registerCommand("bleinfo",     "bi",     [](char* a) { runBleInfo(a); },                                                         "GATT enumeration: bi <index|mac>",        true,  "Bluetooth");
    registerCommand("trackme",     "tm",     [](char* a) { trackMe.start(a && (strncmp(a,"s",1)==0||strncmp(a,"silent",6)==0)); }, "[EXP] Anti-tracking: tm [silent]",   true, "Bluetooth");
    registerCommand("fastpair",    "fp",     [](char* a) { fastPair.command(a); },                                                "Fast Pair attack: fp [scan|spam|h <idx>]", true, "Bluetooth");
    registerCommand("blespam",    "bs",     [](char* a) { bleSpam.command(a); },                                                 "BLE spam: bs [apple|android|ms|samsung|all]", true, "Bluetooth");
    registerCommand("buddy",      "bd",     [](char* a) { buddyCommand(a); },                                                        "Claude Desktop remote: bd [name]",            true,  "Bluetooth");
    // ── SD Card ───────────────────────────────────────────────────────────────
    registerCommand("sdinfo",      "sdi",    [](char* a) { sdCardManager.printInfo(); },                                    "SD card type and size",                   false, "SD Card");
    registerCommand("sdls",        "ls",     [](char* a) { sdCardManager.listDirectory(a && *a ? a : nullptr); },           "List SD dir [path] — default: cwd",       true,  "SD Card", COMP_ANY);
    registerCommand("cd",          "cd",     [](char* a) { sdCardManager.cdCommand(a); },                                   "Change SD directory: cd <dir|..>",        true,  "SD Card", COMP_DIR);
    registerCommand("sdread",      "sdr",    [](char* a) { if (a&&*a) sdCardManager.readFile(a); else { displayManager.println("Usage: sdr <path>"); displayManager.printCommandScreen(); } }, "Read file from SD",    true,  "SD Card", COMP_FILE);
    registerCommand("sdrm",        "srm",    [](char* a) { if (a&&*a) sdCardManager.removeFile(a); else { displayManager.println("Usage: srm <path>"); displayManager.printCommandScreen(); } }, "Delete file from SD", true,  "SD Card", COMP_FILE);
    registerCommand("sdformat",    "sdf",    [](char* a) { sdCardManager.formatCommand(a); },                               "Format SD to FAT: sdf [init]",            true,  "SD Card");
    // ── USB ───────────────────────────────────────────────────────────────────
    registerCommand("usbmsc",      "um",     [](char* a) { usbManager.startMSC(); },                                                              "Expose SD card as USB drive",             false, "USB");
    registerCommand("usbkbd",      "uk",     [](char* a) { usbKeyboard.start(); },                                                               "T-DECK as USB keyboard+mouse",            false, "USB");
    registerCommand("usbexec",     "ux",     [](char* a) {
        if (a && *a && strcmp(a, "demo") != 0) {
            char path[128]; sdCardManager.resolvePath(a, path, sizeof(path));
            badUsb.start(path);
        } else { badUsb.start(a); }
    },                                                                                                                       "BadUSB/DuckyScript executor",             true,  "USB",  COMP_FILE);
    // ── Diagnostics ───────────────────────────────────────────────────────────
    registerCommand("gpson",       "gon",    [](char* a) { runGpsOn(); },                                                   "GPS background task + live status",       false, "Diagnostics");
    registerCommand("gpsoff",      "gof",    [](char* a) { runGpsOff(); },                                                  "Stop GPS background task",                false, "Diagnostics");
    registerCommand("gpstest",     "gt",     [](char* a) { runGpsTest(); },                                                  "GPS coordinate test",                     false, "Diagnostics");
    registerCommand("spktest",     "st",     [](char* a) { runSpeakerTest(); },                                              "Speaker tone test",                       false, "Diagnostics");
    registerCommand("loratest",    "lt",     [](char* a) { runLoraTest(); },                                                 "LoRa SX1262 diagnostic",                  false, "Diagnostics");
}