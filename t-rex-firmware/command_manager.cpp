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
#include "ble_keyboard.h"
#include "bad_usb.h"
#include "buddy.h"
#include "ble_info.h"
#include "notification_manager.h"
#include "wguard.h"
#include "beacon_flood.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
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
extern WGuard             wGuard;
extern ManPages           manPages;

// Forward declarations for standalone command functions
void runGps(char* a);
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

    // Strip trailing spaces so "lock " and "cat /f " match the same as without them
    while (commandIndex > 0 && command[commandIndex - 1] == ' ')
        command[--commandIndex] = '\0';

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
            // Block WiFi commands while wguard is running in background
            if (wGuard.isBackground() &&
                strcmp(commands[i].category, "WiFi") == 0 &&
                strcmp(commands[i].name, "wguard") != 0) {
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("WGUARD bg active — WiFi locked.");
                displayManager.setTextColor(0x4208);
                displayManager.println("Run 'wg stop' first.");
                displayManager.setTextColor(TFT_WHITE);
                displayManager.printCommandScreen();
                resetCommand();
                return;
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

// ── Argument hint table ───────────────────────────────────────────────────────
// cmd  = first word of command buffer
// prev = word immediately before the current token ("" = must equal cmd, i.e. first arg level)
// hints = space-separated completions for that context
struct ArgHintEntry { const char* cmd; const char* prev; const char* hints; };
static const ArgHintEntry kArgHints[] = {
    // pwrsave / psv
    { "pwrsave",     "",              "on off status save reset set" },
    { "psv",         "",              "on off status save reset set" },
    { "pwrsave",     "set",           "timeout dimto fullto screenoff screenoffmode batterymode batterythreshold batterydim" },
    { "psv",         "set",           "timeout dimto fullto screenoff screenoffmode batterymode batterythreshold batterydim" },
    { "pwrsave",     "screenoffmode", "on off" },
    { "psv",         "screenoffmode", "on off" },
    { "pwrsave",     "batterymode",   "on off" },
    { "psv",         "batterymode",   "on off" },
    // lock / lk
    { "lock",        "",              "new update clean wipe timeout status" },
    { "lk",          "",              "new update clean wipe timeout status" },
    // tz
    { "tz",          "",              "status" },
    // macchanger / mc  (no "status" — no-args shows status, but "mc status" is not a keyword)
    { "macchanger",  "",              "on off random set restore target" },
    { "mc",          "",              "on off random set restore target" },
    { "macchanger",  "target",        "wifi bt both" },
    { "mc",          "target",        "wifi bt both" },
    { "macchanger",  "restore",       "on off" },
    { "mc",          "restore",       "on off" },
    // wguard / wg
    { "wguard",      "",              "stop view" },
    { "wg",          "",              "stop view" },
    // beaconflood / bf
    { "beaconflood", "",              "list rickroll seq file clone" },
    { "bf",          "",              "list rickroll seq file clone" },
    // show / sh
    { "show",        "",              "wifi ble hosts" },
    { "sh",          "",              "wifi ble hosts" },
    // volume / vol
    { "volume",      "",              "up down off" },
    { "vol",         "",              "up down off" },
    // notif / nf — first level includes level names; second level after each level
    { "notif",       "",              "on off vol status alert warning success info ping" },
    { "nf",          "",              "on off vol status alert warning success info ping" },
    { "notif",       "alert",         "on off file" },
    { "nf",          "alert",         "on off file" },
    { "notif",       "warning",       "on off file" },
    { "nf",          "warning",       "on off file" },
    { "notif",       "success",       "on off file" },
    { "nf",          "success",       "on off file" },
    { "notif",       "info",          "on off file" },
    { "nf",          "info",          "on off file" },
    { "notif",       "ping",          "on off file" },
    { "nf",          "ping",          "on off file" },
    // trackme / tm  ("silent" IS a valid first arg: tm [silent])
    { "trackme",     "",              "silent" },
    { "tm",          "",              "silent" },
    // blespam / bs
    { "blespam",     "",              "apple android ms samsung all" },
    { "bs",          "",              "apple android ms samsung all" },
    // fastpair / fp
    { "fastpair",    "",              "scan spam h" },
    { "fp",          "",              "scan spam h" },
    // hiddenssid / hs — first arg is index/bssid (dynamic), no static hints
    // sdformat / sdf
    { "sdformat",    "",              "init" },
    { "sdf",         "",              "init" },
    // gps
    { "gps",         "",              "on off test" },
    { nullptr, nullptr, nullptr }
};

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
    bool argHintsMode = false;

    if (wordStart == 0) {
        // ── Complete command names + short names ──────────────────────────────
        // Prefer the full name if it matches; fall back to shortName if different.
        // This lets "ls", "ps", "psv" etc. complete even though they are shortNames.
        for (int i = 0; i < commandCount && matchCount < kMaxMatch; i++) {
            const char* candidate = nullptr;
            if (strncmp(commands[i].name, prefix, prefixLen) == 0) {
                candidate = commands[i].name;
            } else if (strcmp(commands[i].shortName, commands[i].name) != 0 &&
                       strncmp(commands[i].shortName, prefix, prefixLen) == 0) {
                candidate = commands[i].shortName;
            }
            if (candidate) {
                bool dup = false;
                for (int j = 0; j < matchCount; j++)
                    if (strcmp(matches[j], candidate) == 0) { dup = true; break; }
                if (!dup) { strncpy(matches[matchCount], candidate, 127); matches[matchCount++][127] = '\0'; }
            }
        }
    } else {
        // ── Extract first word (command name) ────────────────────────────────
        char cmdWord[32] = "";
        {
            int i = 0;
            while (i < (int)sizeof(cmdWord)-1 && command[i] && command[i] != ' ') {
                cmdWord[i] = command[i];
                i++;
            }
            cmdWord[i] = '\0';
        }

        // ── CompType for this command ─────────────────────────────────────────
        CompType activeComp = COMP_NONE;
        for (int i = 0; i < commandCount; i++) {
            if (strcmp(commands[i].name, cmdWord) == 0 || strcmp(commands[i].shortName, cmdWord) == 0) {
                activeComp = commands[i].compType;
                break;
            }
        }

        // ── Word immediately before the current token (context for 2nd-level hints) ─
        char prevWord[32] = "";
        {
            int pe = wordStart;
            while (pe > 0 && command[pe-1] == ' ') pe--;
            int ps = pe;
            while (ps > 0 && command[ps-1] != ' ') ps--;
            int len = pe - ps;
            if (len > 0 && len < (int)sizeof(prevWord)) {
                strncpy(prevWord, command + ps, len);
                prevWord[len] = '\0';
            }
        }

        if (activeComp != COMP_NONE) {
            // ── File / dir completion ─────────────────────────────────────────
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
                searchDir[sizeof(searchDir) - 1]  = '\0';
                filePrefix[sizeof(filePrefix) - 1] = '\0';

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
        } else {
            // ── Arg hint completion ───────────────────────────────────────────
            bool isManHelp = (strcmp(cmdWord,"man")==0  || strcmp(cmdWord,"mn")==0 ||
                              strcmp(cmdWord,"help")==0 || strcmp(cmdWord,"hlp")==0);
            if (isManHelp) {
                // Complete against registered command names
                for (int i = 0; i < commandCount && matchCount < kMaxMatch; i++) {
                    if (strncmp(commands[i].name, prefix, prefixLen) == 0) {
                        bool dup = false;
                        for (int j = 0; j < matchCount; j++)
                            if (strcmp(matches[j], commands[i].name) == 0) { dup = true; break; }
                        if (!dup) { strncpy(matches[matchCount], commands[i].name, 127); matches[matchCount++][127] = '\0'; }
                    }
                }
                if (matchCount) argHintsMode = true;
            } else {
                for (int h = 0; kArgHints[h].cmd; h++) {
                    if (strcmp(kArgHints[h].cmd, cmdWord) != 0) continue;
                    // Match context: empty prev = first-level (prevWord must equal cmdWord)
                    if (kArgHints[h].prev[0]) {
                        if (strcmp(kArgHints[h].prev, prevWord) != 0) continue;
                    } else {
                        if (strcmp(prevWord, cmdWord) != 0) continue;
                    }
                    char hintsBuf[128];
                    strncpy(hintsBuf, kArgHints[h].hints, sizeof(hintsBuf)-1);
                    hintsBuf[sizeof(hintsBuf)-1] = '\0';
                    char* tok = strtok(hintsBuf, " ");
                    while (tok && matchCount < kMaxMatch) {
                        if (strncmp(tok, prefix, prefixLen) == 0) {
                            bool dup = false;
                            for (int j = 0; j < matchCount; j++)
                                if (strcmp(matches[j], tok) == 0) { dup = true; break; }
                            if (!dup) { strncpy(matches[matchCount], tok, 127); matches[matchCount++][127] = '\0'; }
                        }
                        tok = strtok(nullptr, " ");
                    }
                }
                if (matchCount) argHintsMode = true;
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

    // ── Single match: append space at any level ───────────────────────────────
    if (matchCount == 1) {
        if (command[_cursorPos] == '\0' && commandIndex < (int)bufferSize - 1) {
            command[commandIndex++] = ' ';
            _cursorPos++;
            command[commandIndex] = '\0';
            // After completing a command name (wordStart==0) with file args,
            // preview CWD — so cd shows dirs, cat shows files, etc.
            if (wordStart == 0 && sdCardManager.isReady()) {
                CompType ct = COMP_NONE;
                for (int i = 0; i < commandCount; i++) {
                    if (strcmp(commands[i].name, matches[0]) == 0 || strcmp(commands[i].shortName, matches[0]) == 0) {
                        ct = commands[i].compType; break;
                    }
                }
                if (ct != COMP_NONE) {
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
        if (isDir)             displayManager.setTextColor(TFT_CYAN);
        else if (wordStart==0) displayManager.setTextColor(TFT_GREEN);
        else if (argHintsMode) displayManager.setTextColor(TFT_YELLOW);
        else                   displayManager.setTextColor(TFT_WHITE);
        displayManager.println(matches[i]);
        displayManager.setTextColor(TFT_WHITE);
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

static void handleShowCmd(char* a) {
    if      (!a || !*a)              { displayManager.println("Usage: show <wifi|ble|hosts>"); displayManager.printCommandScreen(); }
    else if (strcmp(a, "wifi")  == 0) wifiFunctions.showWiFiResults();
    else if (strcmp(a, "ble")   == 0) bluetoothFunctions.showBleResults();
    else if (strcmp(a, "hosts") == 0) networkScanner.showHostResults();
    else                             { displayManager.println("Usage: show <wifi|ble|hosts>"); displayManager.printCommandScreen(); }
}

static void handleUsbExecCmd(char* a) {
    if (a && *a && strcmp(a, "demo") != 0) {
        char path[128]; sdCardManager.resolvePath(a, path, sizeof(path));
        badUsb.start(path);
    } else { badUsb.start(a); }
}

static void handleVolumeCmd(char* a) {
    static uint8_t s_vol = 70;
    if (a && *a) {
        if      (strcmp(a, "up")   == 0) s_vol = min(100, (int)s_vol + 10);
        else if (strcmp(a, "down") == 0) s_vol = s_vol > 10 ? s_vol - 10 : 0;
        else if (strcmp(a, "off")  == 0) s_vol = 0;
        else { int v = atoi(a); if (v >= 0 && v <= 100) s_vol = (uint8_t)v; }
    }
    displayManager.setTextColor(0x7BEF); displayManager.printText("Volume  ");
    displayManager.setTextColor(TFT_WHITE);
    char b[16]; snprintf(b, sizeof(b), "%d%%", s_vol); displayManager.println(b);
    displayManager.printCommandScreen();
}

static void handleWGuardCmd(char* a) {
    if (!a || !*a) {
        // No args: if bg active enter live view, else show usage
        if (wGuard.isBackground()) { wGuard.enterView(); return; }
        displayManager.println("Usage: wg <idx|bssid> [ch] [bg]  |  wg stop");
        displayManager.printCommandScreen();
        return;
    }
    if (strcmp(a, "stop") == 0) { wGuard.stopBackground(); return; }
    if (strcmp(a, "view") == 0) {
        if (wGuard.isBackground()) { wGuard.enterView(); return; }
        displayManager.println("wguard not running in background");
        displayManager.printCommandScreen();
        return;
    }
    // Strip trailing " bg" if present
    char buf[64]; strncpy(buf, a, 63); buf[63] = '\0';
    char* last = strrchr(buf, ' ');
    if (last && strcmp(last + 1, "bg") == 0) { *last = '\0'; wGuard.beginBackground(buf); }
    else wGuard.start(a);
}

void CommandManager::setupCommands() {
    // ── System ────────────────────────────────────────────────────────────────
    registerCommand("show",        "sh",     [](char* a) { handleShowCmd(a); },                                                "Show last scan: wifi|ble|hosts",          true,  "System");
    registerCommand("help",        "hlp",    [](char* a) { Utils::printHelp(a); },                                          "Help [cmd]",                              true,  "System");
    registerCommand("man",         "mn",     [](char* a) { manPages.show(a); },                                               "Manual: man <command>",                   true,  "System");
    registerCommand("info",        "inf",    [](char* a) { espInfoPrinter.printESPInfo(); },                                 "Device info (IP, MAC, battery)",          false, "System");
    registerCommand("clear",       "clr",    [](char* a) { displayManager.tdeck_begin(); },                                  "Clear screen",                            false, "System");
    registerCommand("MATRIX",      "matrix", [](char* a) { displayManager.launchMatrixAnimation(); },                       "Matrix rain animation",                   false, "System");
    registerCommand("pwrsave",     "psv",    [](char* a) { PowerSaveManager::handleCommand(a); },                         "Power save: on/off/set/status",  true,  "System");
    registerCommand("lock",        "lk",     [](char* a) { LockScreenManager::getInstance().cmd(a); },                       "Screen lock  [new|update|clean|timeout <s>|status]", true,  "System");
    registerCommand("tz",          "tz",     [](char* a) { runTzCmd(a); },                                                    "Timezone  [+3 | -5:30 | <posix> | status]",          true,  "System");
    registerCommand("volume",      "vol",    [](char* a) { handleVolumeCmd(a); },                                             "General volume: vol [0-100|up|down|off]",   true,  "System");
    registerCommand("notif",       "nf",     [](char* a) { NotificationManager::handleNotifCmd(a); },                        "Notifications: nf [on|off|vol <n>|<lvl> on|off|file <f>]", true, "System");
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
    registerCommand("wguard",      "wg",     [](char* a) { handleWGuardCmd(a); },                                           "WiFi IDS: wg <idx> [bg|stop]",                       true,  "WiFi");
    registerCommand("beaconflood", "bf",     [](char* a) { runBeaconFlood(a); },                                              "Beacon flood: bf [list|seq <base>|file [path]]",     true,  "WiFi");
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
    registerCommand("cat",         "cat",    [](char* a) { if (a&&*a) sdCardManager.readFile(a); else { displayManager.println("Usage: cat <path>"); displayManager.printCommandScreen(); } }, "Read file from SD",    true,  "SD Card", COMP_ANY);
    registerCommand("rm",          "rm",     [](char* a) { if (a&&*a) sdCardManager.removeFile(a); else { displayManager.println("Usage: rm <path>"); displayManager.printCommandScreen(); } }, "Delete file from SD", true,  "SD Card", COMP_FILE);
    registerCommand("sdformat",    "sdf",    [](char* a) { sdCardManager.formatCommand(a); },                               "Format SD to FAT: sdf [init]",            true,  "SD Card");
    // ── USB ───────────────────────────────────────────────────────────────────
    registerCommand("usbmsc",      "um",     [](char* a) { usbManager.startMSC(); },                                                              "Expose SD card as USB drive",             false, "USB");
    registerCommand("usbkbd",      "uk",     [](char* a) { usbKeyboard.start(); },                                                               "T-DECK as USB keyboard+mouse",            false, "USB");
    registerCommand("btkbd",       "bk",     [](char* a) { bleKeyboard.start(); },                                                               "T-DECK as BLE keyboard+mouse",            false, "Bluetooth");
    registerCommand("usbexec",     "ux",     [](char* a) { handleUsbExecCmd(a); },                                              "BadUSB/DuckyScript executor",             true,  "USB",  COMP_FILE);
    registerCommand("jiggle",      "jg",     [](char* a) { usbKeyboard.jiggle(); },                                             "Mouse jiggler — prevent screen lock",     false, "USB");
    // ── Diagnostics ───────────────────────────────────────────────────────────
    registerCommand("gps",         "gps",    [](char* a) { runGps(a); },                                                "GPS: gps on|off|test",                    true,  "Diagnostics");
    registerCommand("spktest",     "st",     [](char* a) { runSpeakerTest(); },                                              "Speaker tone test",                       false, "Diagnostics");
    registerCommand("loratest",    "lt",     [](char* a) { runLoraTest(); },                                                 "LoRa SX1262 diagnostic",                  false, "Diagnostics");
}