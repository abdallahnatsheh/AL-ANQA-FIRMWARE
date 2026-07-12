// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "bad_usb.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include "usb_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "utilities.h"
#include "ble_keyboard.h"          // BLE HID target for `ux ble`
#include "bluetooth_functions.h"   // s_bleDevices / s_bleCount (sbl scan cache) for clone picker

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <SD.h>

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;
extern InputHandling  inputHandler;

BadUsb badUsb;

// ── Hyphenated combination table ─────────────────────────────────────────────
// Matches Flipper Zero / Bruce DuckyScript format: "CTRL-ALT DELETE"
const BadUsb::HyphenCombo BadUsb::COMBOS[] = {
    { "CTRL-ALT",       KEY_LEFT_CTRL,  KEY_LEFT_ALT,   0              },
    { "CTRL-SHIFT",     KEY_LEFT_CTRL,  KEY_LEFT_SHIFT, 0              },
    { "CTRL-GUI",       KEY_LEFT_CTRL,  KEY_LEFT_GUI,   0              },
    { "CTRL-ESCAPE",    KEY_LEFT_CTRL,  KEY_ESC,        0              },
    { "ALT-SHIFT",      KEY_LEFT_ALT,   KEY_LEFT_SHIFT, 0              },
    { "ALT-GUI",        KEY_LEFT_ALT,   KEY_LEFT_GUI,   0              },
    { "GUI-SHIFT",      KEY_LEFT_GUI,   KEY_LEFT_SHIFT, 0              },
    { "GUI-SPACE",      KEY_LEFT_GUI,   ' ',            0              },
    { "CTRL-ALT-SHIFT", KEY_LEFT_CTRL,  KEY_LEFT_ALT,   KEY_LEFT_SHIFT },
    { "CTRL-ALT-GUI",   KEY_LEFT_CTRL,  KEY_LEFT_ALT,   KEY_LEFT_GUI   },
    { "ALT-SHIFT-GUI",  KEY_LEFT_ALT,   KEY_LEFT_SHIFT, KEY_LEFT_GUI   },
    { "CTRL-SHIFT-GUI", KEY_LEFT_CTRL,  KEY_LEFT_SHIFT, KEY_LEFT_GUI   },
};
const int BadUsb::COMBOS_COUNT = sizeof(BadUsb::COMBOS) / sizeof(BadUsb::COMBOS[0]);

// ── Built-in demo script ──────────────────────────────────────────────────────
// Opens Notepad via Win+R then draws the T-Rex ASCII art.
// Flipper Zero / standard DuckyScript v1.0 compatible format.
static const char* const DEMO_LINES[] = {
    "REM T-Rex BadUSB Demo",
    "DEFAULT_DELAY 50",
    "GUI r",
    "DELAY 700",
    "STRING notepad",
    "ENTER",
    "DELAY 2000",
    // art lines — blank lines use ENTER only, content lines use STRING + ENTER
    "ENTER",
    "ENTER",
    "ENTER",
    "STRING                                                                .",
    "ENTER",
    "STRING                                                            =#+.:*##==#-",
    "ENTER",
    "STRING                                                         -#*##. #*  -*#*#######.",
    "ENTER",
    "STRING                                                       .##++#=.:-: .--*######*##.",
    "ENTER",
    "STRING                                                       ##-+######+#############+:",
    "ENTER",
    "STRING                                                      .#+=+-+#####=:+####+-.##**=",
    "ENTER",
    "STRING                                                      ##:+##      .  --==#=-.  -",
    "ENTER",
    "STRING                                                    =##- +###.....        .",
    "ENTER",
    "STRING                                               #######+:..####+:.        .",
    "ENTER",
    "STRING                                             ########=..  .*==.+# ..    .",
    "ENTER",
    "STRING                                            ####*##*:.. ..   ...-#:-... .-",
    "ENTER",
    "STRING                          =:            .*#*#**+-##+.  ..       .+#=+-.. +",
    "ENTER",
    "STRING                         #.            ###*.-:...###+.. ..    .: .###=+*:-*",
    "ENTER",
    "STRING                        **           -#####-.... ###:....    ..#.  .#+#=:=-",
    "ENTER",
    "STRING                        -#-        +#-#####:    +#+    ...  ...-:     ..",
    "ENTER",
    "STRING                         -*##***####::=###*:.   #=-        .. .-",
    "ENTER",
    "STRING                          ..-=+==-:. .=*##+:.    . #+#.     . ..-##:",
    "ENTER",
    "STRING                               . .    .+#+..       .# *     ..   :.=:",
    "ENTER",
    "STRING                                 .  *#- .. .               ...",
    "ENTER",
    "STRING                                   :+...  .            .. .. .",
    "ENTER",
    "STRING                                   :+.  .              ..",
    "ENTER",
    "STRING                                  -*.                 .",
    "ENTER",
    "STRING                                  +=: .                .. :.",
    "ENTER",
    "STRING                                  #.*= .               ..=.=-",
    "ENTER",
    "STRING                               -=#-:#+=.#+.              -#=.-==-.",
    "ENTER",
    "STRING                            ... . . -- .   ............. ..+.  ..  ....... .",
    "ENTER",
    "ENTER",
    "STRING                      *##########          .##########.  :##########:  ###=   +###",
    "ENTER",
    "STRING                          *##:             .###    ###:  :##*           #### ####",
    "ENTER",
    "STRING                          *##:    :######  .##########:  :########:      =#####-",
    "ENTER",
    "STRING                          *##:     ......  .########:    :###.....      -#######.",
    "ENTER",
    "STRING                          *##:             .###  .####.  :##########:  ####  :####",
    "ENTER",
    "STRING                          =##.              ##*    ###.  .##########.  ###    .###",
    "ENTER",
    "ENTER",
    "STRING                       +-+.*:=:+-+-*-=.. .*.+++=:-*-=-+=- *:.  =:++*+=.*+==++=-=**-.",
    "ENTER",
    "ENTER",
    "ENTER",
    "ENTER",
    nullptr
};
static const int DEMO_COUNT = (sizeof(DEMO_LINES) / sizeof(DEMO_LINES[0])) - 1;

// ── begin() ───────────────────────────────────────────────────────────────────
// g_hid_keyboard is already registered by usbKeyboard.begin() — nothing to do here.
void BadUsb::begin() {}

// ── bleWaitForHost() ──────────────────────────────────────────────────────────
// BLE path: advertise as a HID keyboard and block until a host pairs/connects.
// Returns false if the operator aborts (q / hold trackball) before a host connects.
bool BadUsb::bleWaitForHost(const char* cloneMacStr, uint8_t cloneType, const char* cloneName) {
    DisplayManager& dm = displayManager;
    bool clone = (cloneMacStr && *cloneMacStr);
    bleKeyboard.badusbBegin(cloneMacStr, cloneType, cloneName);  // spoof target addr if cloning

    if (clone) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED);   dm.printText("CLONE ");
        dm.setTextColor(TFT_WHITE); dm.println(cloneMacStr);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("Waiting host auto-reconnect...");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("(real device must be off). q=x");
    } else {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.printText("Advertising: ");
        dm.println((cloneName && *cloneName) ? cloneName : "T-REX-KBD");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("Connect from target (no PIN,");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("no bond). q/hold=cancel.");
    }
    int      wy      = dm.getCursorY();
    uint32_t lastMs  = 0;
    uint8_t  spinIdx = 0;
    const char* spinChars = "|/-\\";
    bool     clickHeld   = false;
    uint32_t clickDownMs = 0;

    while (!bleKeyboard.badusbConnected()) {
        uint32_t now = millis();
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { bleKeyboard.badusbEnd(); return false; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) lastMs = 0;

        // Hold trackball centre ≥1.5s to cancel (matches btkbd)
        bool up = (bool)digitalRead(BOARD_BOOT_PIN);
        if (!up && !clickHeld)                                  { clickDownMs = now; clickHeld = true; }
        else if (up && clickHeld)                               { clickHeld = false; }
        else if (!up && clickHeld && now - clickDownMs >= 1500) { bleKeyboard.badusbEnd(); return false; }

        if (now - lastMs >= 250) {
            lastMs = now;
            dm.fillRect(0, wy, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(10, wy);
            char s[2] = { spinChars[spinIdx++ & 3], 0 };
            dm.setTextColor(TFT_CYAN);   dm.printText("Waiting ");
            dm.setTextColor(TFT_YELLOW); dm.println(s);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    dm.fillRect(0, wy, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    dm.setCursor(10, wy);
    dm.setTextColor(TFT_GREEN); dm.println("Host connected!");
    vTaskDelay(pdMS_TO_TICKS(400));
    return true;
}

// ── start() ───────────────────────────────────────────────────────────────────
void BadUsb::start(char* args, bool ble, const char* cloneMacStr, uint8_t cloneType,
                   const char* cloneName) {
    DisplayManager& dm = displayManager;
    _ble = ble;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText(_ble ? "BLE" : "USB");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText((cloneMacStr && *cloneMacStr) ? "CLONE" : "EXEC");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    while (args && (*args == ' ' || *args == '\t')) args++;

    if (!args || *args == '\0') {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.println("Usage:");
        dm.setCursor(10, dm.getCursorY()); dm.println("  ux [ble] demo | ux [ble] <script>");
        dm.setCursor(10, dm.getCursorY()); dm.println("  ux ble clone <mac|#> <script>");
        dm.setCursor(10, dm.getCursorY()); dm.println("  ux ble name \"X Kbd\" <script>");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("BLE=connect; clone=spoof bonded");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("kbd; name=fake advertised name.");
        dm.printCommandScreen(); return;
    }

    // ── Establish the transport + pick the sink ───────────────────────────────
    if (_ble) {
        if (!bleWaitForHost(cloneMacStr, cloneType, cloneName)) {  // advertise/spoof + wait; aborted
            dm.clearScreen(); dm.setCursor(10, outputY);
            dm.setTextColor(TFT_YELLOW); dm.println("Cancelled.");
            vTaskDelay(pdMS_TO_TICKS(1200));
            dm.printCommandScreen(); return;
        }
        _sink = g_bleHidSink;
    } else {
        if (!usbManager.isConnected()) {
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_RED);  dm.println("Not connected to PC.");
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(0x7BEF);   dm.println("Plug in USB cable first.");
            vTaskDelay(pdMS_TO_TICKS(2500));
            dm.printCommandScreen(); return;
        }
        _sink = g_usbHidSink;
    }

    // Flush any stale HID state before sending new keystrokes
    _sink->releaseAll();
    vTaskDelay(pdMS_TO_TICKS(_ble ? 200 : 500));

    _aborted          = false;
    _defaultCharDelay = 8;
    _nextCharDelay    = -1;

    if (strcmp(args, "demo") == 0) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW); dm.println("Running demo...");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);     dm.println("q to abort.");
        vTaskDelay(pdMS_TO_TICKS(1000));
        runDemo();
    } else {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);  dm.printText("Script: "); dm.println(args);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);     dm.println("q to abort.");
        vTaskDelay(pdMS_TO_TICKS(800));
        runFile(args);
    }

    _sink->releaseAll();
    if (_ble) bleKeyboard.badusbEnd();           // tear down NimBLE HID, idle the stack

    dm.clearScreen(); dm.setCursor(10, outputY);
    dm.setTextColor(_aborted ? TFT_YELLOW : TFT_GREEN);
    dm.println(_aborted ? "Aborted." : "Done.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── Interactive `ux ble` guided flow ──────────────────────────────────────────
void BadUsb::drawBleHeader(const char* label) {
    DisplayManager& dm = displayManager;
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText(label);
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();
}

void BadUsb::drawBleFooter(const char* hint) {
    DisplayManager& dm = displayManager;
    dm.setCursor(0, 210);   dm.printSeparator();
    dm.setCursor(6, 214);   dm.setTextColor(0x7BEF); dm.printText(hint);
    dm.setTextColor(TFT_WHITE);
}

int BadUsb::blePickMode() {
    DisplayManager& dm = displayManager;
    const char* items[2] = { "Connect (fresh keyboard)", "Spoof (clone bonded dev)" };
    int sel = 0; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("BADBLE");
            dm.setCursor(10, outputY + LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Select mode:");
            for (int i = 0; i < 2; i++) {
                int y = outputY + (3 + i) * LINE_HEIGHT; bool s = (i == sel);
                if (s) dm.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(6, y);
                dm.setTextColor(s ? TFT_YELLOW : 0x7BEF); dm.printText(s ? ">" : " ");
                dm.setTextColor(s ? TFT_WHITE : 0x7BEF);  dm.println(items[i]);
            }
            drawBleFooter("trkbl=sel  ent=pick  q=cancel");
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)   { if (sel > 0) { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN) { if (sel < 1) { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) return sel;
        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q')       return -1;
        if (k == '\r' || k == '\n')     return sel;
        if (k == '1') return 0;
        if (k == '2') return 1;
    }
}

int BadUsb::blePickTarget() {
    DisplayManager& dm = displayManager;
    if (s_bleCount <= 0) {
        drawBleHeader("TARGET");
        dm.setCursor(10, outputY + 2 * LINE_HEIGHT);
        dm.setTextColor(TFT_YELLOW); dm.println("No BLE devices scanned.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);     dm.println("Run 'sbl' first, then retry.");
        drawBleFooter("any key = back");
        while (true) {
            if (inputHandler.getKeyboardInput()) return -1;
            if (inputHandler.getTrackballEvent() != TBALL_NONE) return -1;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    int sel = 0, top = 0; const int VIS = 8; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("TARGET");
            dm.setCursor(10, outputY + LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Pick device to clone:");
            if (sel < top)        top = sel;
            if (sel >= top + VIS) top = sel - VIS + 1;
            for (int r = 0; r < VIS && top + r < s_bleCount; r++) {
                int i = top + r; int y = outputY + (3 + r) * LINE_HEIGHT; bool s = (i == sel);
                if (s) dm.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(2, y);
                dm.setTextColor(s ? TFT_YELLOW : 0x7BEF); dm.printText(s ? ">" : " ");
                const char* lbl = s_bleDevices[i].name[0] ? s_bleDevices[i].name : s_bleDevices[i].addr;
                char line[34];
                snprintf(line, sizeof(line), "%-18.18s %ddBm", lbl, s_bleDevices[i].rssi);
                dm.setTextColor(s ? TFT_WHITE : 0x7BEF); dm.println(line);
            }
            drawBleFooter("trkbl=sel  ent=pick  q=back");
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)   { if (sel > 0)               { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN) { if (sel < s_bleCount - 1)  { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) return sel;
        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q')   return -1;
        if (k == '\r' || k == '\n') return sel;
    }
}

bool BadUsb::blePickScript(char* out, size_t n) {
    DisplayManager& dm = displayManager;
    static String files[24]; int fc = 0;
    files[fc++] = "demo";                             // index 0 = built-in demo
    if (sdCardManager.isReady()) {
        File dir = SD.open("/apps/badusb/scripts");
        if (dir) {
            File f = dir.openNextFile();
            while (f && fc < 24) {
                if (!f.isDirectory()) files[fc++] = String(f.name());
                f = dir.openNextFile();
            }
            dir.close();
        }
    }
    int sel = 0, top = 0; const int VIS = 9; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("SCRIPT");
            dm.setCursor(10, outputY + LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Pick payload:");
            if (sel < top)        top = sel;
            if (sel >= top + VIS) top = sel - VIS + 1;
            for (int r = 0; r < VIS && top + r < fc; r++) {
                int i = top + r; int y = outputY + (3 + r) * LINE_HEIGHT; bool s = (i == sel);
                if (s) dm.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(2, y);
                dm.setTextColor(s ? TFT_YELLOW : 0x7BEF); dm.printText(s ? ">" : " ");
                const char* nm = files[i].c_str();
                const char* base = strrchr(nm, '/'); if (base) nm = base + 1;  // strip any path
                dm.setTextColor(s ? TFT_WHITE : 0x7BEF);
                dm.println(i == 0 ? "demo (built-in art)" : nm);
            }
            drawBleFooter("trkbl=sel  ent=pick  q=back");
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        bool pick = (tb == TBALL_CLICK);
        if (tb == TBALL_UP)   { if (sel > 0)        { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN) { if (sel < fc - 1)   { sel++; redraw = true; } continue; }
        char k = inputHandler.getKeyboardInput();
        if (!k && !pick) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q') return false;
        if (pick || k == '\r' || k == '\n') {
            if (sel == 0) { strncpy(out, "demo", n - 1); out[n - 1] = '\0'; }
            else {
                const char* nm = files[sel].c_str();
                if (nm[0] == '/') { strncpy(out, nm, n - 1); out[n - 1] = '\0'; }  // already full path
                else {
                    const char* base = strrchr(nm, '/'); if (base) nm = base + 1;
                    snprintf(out, n, "/apps/badusb/scripts/%s", nm);
                }
            }
            return true;
        }
    }
}

void BadUsb::blePromptName(char* buf, size_t n) {
    DisplayManager& dm = displayManager;
    int len = strlen(buf);
    bool full = true;   // full chrome redraw vs. just the name line
    int  ny  = 0;
    while (true) {
        if (full) {
            drawBleHeader("NAME");
            dm.setCursor(10, outputY + LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Advertised name:");
            ny = outputY + 3 * LINE_HEIGHT;
            drawBleFooter("type/bksp  Enter=use  q=keep");
            full = false;
        }
        dm.fillRect(0, ny - 1, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(10, ny);
        dm.setTextColor(TFT_WHITE); dm.printText("> ");
        dm.println(buf[0] ? buf : "(empty)");
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (!k) {
                if (LockScreenManager::getInstance().consumeJustUnlocked()) full = true;
                vTaskDelay(pdMS_TO_TICKS(20));
                if (full) break;   // redraw chrome
                continue;
            }
            if (k == '\r' || k == '\n' || k == 'q' || k == 'Q') return;   // accept current buffer
            if (k == '\b') { if (len > 0) { buf[--len] = '\0'; } break; }
            if (k >= 0x20 && k < 0x7F && len < (int)n - 1) { buf[len++] = k; buf[len] = '\0'; break; }
        }
    }
}

void BadUsb::startInteractive() {
    int mode = blePickMode();
    if (mode < 0) { displayManager.printCommandScreen(); return; }

    static char macBuf[18]; static char nameBuf[24];
    const char* cloneMac = nullptr; uint8_t cloneType = 1; const char* cloneName = nullptr;
    nameBuf[0] = '\0';

    if (mode == 1) {                                   // spoof → pick a target to clone
        int idx = blePickTarget();
        if (idx < 0) { displayManager.printCommandScreen(); return; }
        strncpy(macBuf, s_bleDevices[idx].addr, sizeof(macBuf) - 1); macBuf[sizeof(macBuf) - 1] = '\0';
        cloneMac  = macBuf;
        cloneType = s_bleDevices[idx].addrType;
        strncpy(nameBuf, s_bleDevices[idx].name, sizeof(nameBuf) - 1); nameBuf[sizeof(nameBuf) - 1] = '\0';
    }
    if (nameBuf[0] == '\0') { strncpy(nameBuf, "T-REX-KBD", sizeof(nameBuf) - 1); nameBuf[sizeof(nameBuf) - 1] = '\0'; }

    blePromptName(nameBuf, sizeof(nameBuf));           // keep the cloned/default name or edit it
    cloneName = nameBuf[0] ? nameBuf : nullptr;

    static char scriptPath[128];
    if (!blePickScript(scriptPath, sizeof(scriptPath))) { displayManager.printCommandScreen(); return; }

    start(scriptPath, true, cloneMac, cloneType, cloneName);   // hand off to the existing runner
}

// ── runDemo() ─────────────────────────────────────────────────────────────────
void BadUsb::runDemo() {
    runLines(DEMO_LINES, DEMO_COUNT);
}

// ── runLines() — shared executor for demo (array) ─────────────────────────────
void BadUsb::runLines(const char* const* lines, int count) {
    int      defaultDelay = 0;
    String   lastLine     = "";

    for (int i = 0; i < count && !_aborted; i++) {
        const char* raw = lines[i];
        if (!raw || *raw == '\0') continue;

        // REPEAT is handled at loop level so it can re-run lastLine
        if (strncmp(raw, "REPEAT", 6) == 0 && (raw[6] == ' ' || raw[6] == '\0')) {
            int n = (raw[6] == ' ') ? atoi(raw + 7) : 1;
            if (n <= 0) n = 1;
            for (int r = 0; r < n && !_aborted; r++) {
                if (!executeLine(lastLine.c_str(), defaultDelay)) break;
                if (defaultDelay > 0 && !scriptDelay(defaultDelay)) break;
            }
            continue;
        }

        lastLine = raw;
        if (!executeLine(raw, defaultDelay)) break;
        if (defaultDelay > 0 && !scriptDelay(defaultDelay)) break;
    }
}

// ── runFile() ─────────────────────────────────────────────────────────────────
void BadUsb::runFile(const char* path) {
    if (!sdCardManager.isReady()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No SD card mounted.");
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    File f = SD.open(path);
    if (!f) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.printText("Not found: ");
        displayManager.println(path);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    int    defaultDelay = 0;
    String lastLine     = "";

    while (f.available() && !_aborted) {
        String line = f.readStringUntil('\n');
        // Strip CR (CRLF files from Windows)
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        line.trim();
        if (line.length() == 0) continue;

        // REPEAT at loop level so we can re-run lastLine
        if (line.startsWith("REPEAT")) {
            String rest = line.substring(6);
            rest.trim();
            int n = rest.toInt();
            if (n <= 0) n = 1;
            for (int r = 0; r < n && !_aborted; r++) {
                if (!executeLine(lastLine.c_str(), defaultDelay)) break;
                if (defaultDelay > 0 && !scriptDelay(defaultDelay)) break;
            }
            continue;
        }

        lastLine = line;
        if (!executeLine(line.c_str(), defaultDelay)) break;
        if (defaultDelay > 0 && !scriptDelay(defaultDelay)) break;
    }

    f.close();
}

// ── executeLine() ─────────────────────────────────────────────────────────────
bool BadUsb::executeLine(const char* rawLine, int& defaultDelay) {
    while (*rawLine == ' ' || *rawLine == '\t') rawLine++;
    if (*rawLine == '\0') return true;

    // ── Comments ──────────────────────────────────────────────────────────────
    if (strncmp(rawLine, "REM", 3) == 0 && (rawLine[3] == ' ' || rawLine[3] == '\0')) return true;
    if (strncmp(rawLine, "//",  2) == 0)                                               return true;

    // ── STRING / STRINGLN ─────────────────────────────────────────────────────
    if (strncmp(rawLine, "STRINGLN ", 9) == 0) {
        typeString(rawLine + 9);
        pressSpecialKey(KEY_RETURN);
        _nextCharDelay = -1;
        return true;
    }
    if (strncmp(rawLine, "STRING ", 7) == 0) {
        typeString(rawLine + 7);
        _nextCharDelay = -1;
        return true;
    }

    // ── DELAY ─────────────────────────────────────────────────────────────────
    if (strncmp(rawLine, "DELAY ", 6) == 0)
        return scriptDelay((uint32_t)atoi(rawLine + 6));

    // ── DEFAULT_DELAY / DEFAULTDELAY ──────────────────────────────────────────
    if (strncmp(rawLine, "DEFAULT_DELAY ", 14) == 0) {
        defaultDelay = atoi(rawLine + 14); return true;
    }
    if (strncmp(rawLine, "DEFAULTDELAY ", 13) == 0) {
        defaultDelay = atoi(rawLine + 13); return true;
    }

    // ── STRING_DELAY / STRINGDELAY — one-shot char delay for next STRING ──────
    if (strncmp(rawLine, "STRING_DELAY ", 13) == 0) {
        _nextCharDelay = atoi(rawLine + 13); return true;
    }
    if (strncmp(rawLine, "STRINGDELAY ", 12) == 0) {
        _nextCharDelay = atoi(rawLine + 12); return true;
    }

    // ── DEFAULT_STRING_DELAY / DEFAULTSTRINGDELAY ─────────────────────────────
    if (strncmp(rawLine, "DEFAULT_STRING_DELAY ", 21) == 0) {
        _defaultCharDelay = atoi(rawLine + 21); return true;
    }
    if (strncmp(rawLine, "DEFAULTSTRINGDELAY ", 19) == 0) {
        _defaultCharDelay = atoi(rawLine + 19); return true;
    }

    // ── WAIT_FOR_BUTTON_PRESS — waits for trackball click ─────────────────────
    if (strncmp(rawLine, "WAIT_FOR_BUTTON_PRESS", 21) == 0) {
        // BOARD_BOOT_PIN (GPIO0) active-low — same as usbkbd trackball click
        while (true) {
            if (digitalRead(BOARD_BOOT_PIN) == LOW) {
                // Wait for release
                while (digitalRead(BOARD_BOOT_PIN) == LOW) vTaskDelay(pdMS_TO_TICKS(10));
                return true;
            }
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { _aborted = true; return false; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // ── Tokenize for key/modifier commands ────────────────────────────────────
    char  buf[256];
    strncpy(buf, rawLine, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* tokens[8];
    int   nTok = 0;
    char* tok  = strtok(buf, " \t");
    while (tok && nTok < 8) { tokens[nTok++] = tok; tok = strtok(nullptr, " \t"); }
    if (nTok == 0) return true;

    // ── Hyphenated combo: "CTRL-ALT DELETE", "GUI-SHIFT s", etc. ─────────────
    // Matches Flipper Zero format. Correctly presses the argument key too
    // (Bruce's implementation has a bug where the arg key is not sent for combos).
    const HyphenCombo* hc = findHyphenCombo(tokens[0]);
    if (hc) {
        _sink->press(hc->k1);
        _sink->press(hc->k2);
        if (hc->k3) _sink->press(hc->k3);
        if (nTok > 1) {
            uint8_t argKey = resolveSpecialKey(tokens[1]);
            if (argKey) _sink->press(argKey);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        _sink->releaseAll();
        return true;
    }

    // ── Space-separated modifier combo or single key ──────────────────────────
    // e.g. "CTRL ALT DELETE", "GUI r", "ENTER", "F5"
    uint8_t mods[4];
    int     nMods    = 0;
    uint8_t finalKey = 0;

    for (int i = 0; i < nTok; i++) {
        if (isModifier(tokens[i])) {
            if (nMods < 4) mods[nMods++] = modifierCode(tokens[i]);
        } else {
            finalKey = resolveSpecialKey(tokens[i]);
        }
    }

    pressCombo(mods, nMods, finalKey);
    return true;
}

// ── scriptDelay() ─────────────────────────────────────────────────────────────
bool BadUsb::scriptDelay(uint32_t ms) {
    uint32_t end = millis() + ms;
    while (millis() < end) {
        LockScreenManager::getInstance().consumeJustUnlocked(); // display blocked; script keeps running
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { _aborted = true; return false; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

// ── typeString() ──────────────────────────────────────────────────────────────
void BadUsb::typeString(const char* s) {
    int charDelay = (_nextCharDelay >= 0) ? _nextCharDelay : _defaultCharDelay;
    while (*s) {
        uint8_t c = (uint8_t)*s++;
        if (c >= 0x20 && c < 0x7F) {
            _sink->printChar((char)c);
        } else if (c == '\n') {
            _sink->press(KEY_RETURN);
            vTaskDelay(pdMS_TO_TICKS(20));
            _sink->releaseAll();
        }
        if (charDelay > 0) vTaskDelay(pdMS_TO_TICKS(charDelay));
    }
}

// ── pressSpecialKey() ─────────────────────────────────────────────────────────
void BadUsb::pressSpecialKey(uint8_t keyCode) {
    _sink->press(keyCode);
    vTaskDelay(pdMS_TO_TICKS(30));
    _sink->releaseAll();
}

// ── pressCombo() ──────────────────────────────────────────────────────────────
void BadUsb::pressCombo(uint8_t mods[], int nMods, uint8_t key) {
    for (int i = 0; i < nMods; i++) _sink->press(mods[i]);
    if (key) _sink->press(key);
    vTaskDelay(pdMS_TO_TICKS(50));
    _sink->releaseAll();
}

// ── findHyphenCombo() ─────────────────────────────────────────────────────────
const BadUsb::HyphenCombo* BadUsb::findHyphenCombo(const char* token) {
    for (int i = 0; i < COMBOS_COUNT; i++) {
        if (!strcmp(token, COMBOS[i].cmd)) return &COMBOS[i];
    }
    return nullptr;
}

// ── resolveSpecialKey() ───────────────────────────────────────────────────────
uint8_t BadUsb::resolveSpecialKey(const char* token) {
    if (!strcmp(token, "ENTER")       || !strcmp(token, "RETURN"))     return KEY_RETURN;
    if (!strcmp(token, "BACKSPACE"))                                    return KEY_BACKSPACE;
    if (!strcmp(token, "TAB"))                                          return KEY_TAB;
    if (!strcmp(token, "ESC")         || !strcmp(token, "ESCAPE"))     return KEY_ESC;
    if (!strcmp(token, "DELETE")      || !strcmp(token, "DEL"))        return KEY_DELETE;
    if (!strcmp(token, "HOME"))                                         return KEY_HOME;
    if (!strcmp(token, "END"))                                          return KEY_END;
    if (!strcmp(token, "INSERT"))                                       return KEY_INSERT;
    if (!strcmp(token, "PAGEUP"))                                       return KEY_PAGE_UP;
    if (!strcmp(token, "PAGEDOWN"))                                     return KEY_PAGE_DOWN;
    if (!strcmp(token, "UP")          || !strcmp(token, "UPARROW"))    return KEY_UP_ARROW;
    if (!strcmp(token, "DOWN")        || !strcmp(token, "DOWNARROW"))  return KEY_DOWN_ARROW;
    if (!strcmp(token, "LEFT")        || !strcmp(token, "LEFTARROW"))  return KEY_LEFT_ARROW;
    if (!strcmp(token, "RIGHT")       || !strcmp(token, "RIGHTARROW")) return KEY_RIGHT_ARROW;
    if (!strcmp(token, "SPACE"))                                        return ' ';
    if (!strcmp(token, "CAPSLOCK"))                                     return KEY_CAPS_LOCK;
    // NUMLOCK, SCROLLLOCK, PRINTSCREEN, PAUSE, MENU not in this library — silently ignored

    // F1–F24 (KEY_F1=0xC2 through KEY_F12=0xCD are sequential;
    //          KEY_F13=0xF0 through KEY_F24=0xFB are also sequential)
    if (token[0] == 'F' && token[1] != '\0' && isdigit((unsigned char)token[1])) {
        int n = atoi(token + 1);
        if (n >= 1  && n <= 12) return KEY_F1  + (n - 1);
        if (n >= 13 && n <= 24) return KEY_F13 + (n - 13);
    }

    // Single character — lower-cased so CTRL+C and CTRL+c map to the same physical key
    if (strlen(token) == 1) return (uint8_t)tolower((unsigned char)token[0]);

    return 0; // unknown — pressCombo skips key=0
}

// ── isModifier() ──────────────────────────────────────────────────────────────
bool BadUsb::isModifier(const char* token) {
    return !strcmp(token, "CTRL")    || !strcmp(token, "CONTROL") ||
           !strcmp(token, "ALT")     ||
           !strcmp(token, "SHIFT")   ||
           !strcmp(token, "GUI")     || !strcmp(token, "WINDOWS") ||
           !strcmp(token, "COMMAND") || !strcmp(token, "CMD");
}

// ── modifierCode() ────────────────────────────────────────────────────────────
uint8_t BadUsb::modifierCode(const char* token) {
    if (!strcmp(token, "CTRL")    || !strcmp(token, "CONTROL"))               return KEY_LEFT_CTRL;
    if (!strcmp(token, "ALT"))                                                 return KEY_LEFT_ALT;
    if (!strcmp(token, "SHIFT"))                                               return KEY_LEFT_SHIFT;
    if (!strcmp(token, "GUI")     || !strcmp(token, "WINDOWS") ||
        !strcmp(token, "COMMAND") || !strcmp(token, "CMD"))                    return KEY_LEFT_GUI;
    return 0;
}
