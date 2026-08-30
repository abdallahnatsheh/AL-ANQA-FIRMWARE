// AL-ANQA — offensive security firmware for LilyGo T-DECK
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
#include "ble_info.h"              // runBleInfo — [i] inspect a target's GATT from the clone picker
#include "espchat.h"               // stopEspchatBg() — must stop before WiFi mode change
#include "macwatch.h"              // stopMacwatchBg() — must stop before BLE bring-up
#include "layout.h"                // layoutFooterY — bottom-anchored footer positions

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>

// HID usage 0x53 (NumLock) encoded for USBHIDKeyboard::press()
// press(k>=0x88) → pressRaw(k-0x88); 0x53+0x88=0xDB
static constexpr uint8_t KEY_USB_NUMLOCK = 0xDB;

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
// Opens Notepad via Win+R then draws the Al-Anqa phoenix ASCII art.
// Flipper Zero / standard DuckyScript v1.0 compatible format.
// Per-OS editor-open preambles — runDemo() picks one based on the probed OS,
// runs it, then types the shared art body below.
//   Windows: Win+R -> notepad
//   macOS:   Cmd+Space (Spotlight) -> TextEdit -> Cmd+N (new document)
//   Linux:   Ctrl+Alt+T (terminal) -> `cat <<'PHX'` heredoc; the art rows are fed
//            as heredoc input and CLOSE_LINUX types the closing delimiter after,
//            so `cat` echoes the whole phoenix block to the terminal (no editor
//            needed). '#'-leading rows are literal inside a single-quoted heredoc.
static const char* const OPEN_WIN[] = {
    "GUI r", "DELAY 700", "STRING notepad", "ENTER", "DELAY 2000", nullptr
};
static const char* const OPEN_MAC[] = {
    "GUI-SPACE", "DELAY 600", "STRING TextEdit", "DELAY 400", "ENTER",
    "DELAY 2500", "GUI n", "DELAY 1200", nullptr
};
static const char* const OPEN_LINUX[] = {
    "CTRL-ALT t", "DELAY 1500", "STRING cat <<'PHX'", "ENTER", "DELAY 300", nullptr
};
// Linux closer — ends the heredoc so `cat` prints the buffered art block.
static const char* const CLOSE_LINUX[] = {
    "STRING PHX", "ENTER", nullptr
};
static int arrLen(const char* const* a) { int n = 0; while (a[n]) n++; return n; }

// Shared art body — typed after the OS-specific editor is open.
static const char* const DEMO_LINES[] = {
    "REM Al-Anqa BadUSB Demo",
    "DEFAULT_DELAY 50",
    // art lines — full-block phoenix + AL-ANQA wordmark (from ascii-art.txt)
    "ENTER",
    "ENTER",
    "STRING ####################################################################################################",
    "ENTER",
    "STRING ###########################+-##########################################-+###########################",
    "ENTER",
    "STRING ######################-###+ ############################################ +###-######################",
    "ENTER",
    "STRING #####################. #-# .############################################. #+# -#####################",
    "ENTER",
    "STRING #####################  -+-  ####################+###+###################  -+-  #####################",
    "ENTER",
    "STRING ####################+   -+  .#######++#########- +-+###################.  +-   #####################",
    "ENTER",
    "STRING #####################+   +.  +.+#############-##.    -##############+.-  .-   +#####################",
    "ENTER",
    "STRING #################### ++ -++      -############-         ##########-      ++- ++ ####################",
    "ENTER",
    "STRING ####################.   -.++.  .+    +#######+        ..  +###+    +.  .++.-   .####################",
    "ENTER",
    "STRING ####################.#-  -+#   --+.-  -####### .  .+++####+##+  -.+--   ++-  -#.####################",
    "ENTER",
    "STRING #####################      + .-  +.--  #######-   .++########  --.+  -. +      #####################",
    "ENTER",
    "STRING #######################+---+++   .- -.  ######-     +#######  .- -.   +++---+#######################",
    "ENTER",
    "STRING #####################+      -++  .-- --   .+##.      -##+.   -- --.  ++-      +#####################",
    "ENTER",
    "STRING #######################+-.    -+#---+- -+.-   - .    -   -.+- -+---#+-.    -+#######################",
    "ENTER",
    "STRING ########################-   -+ .++##+.+#+.+##-     .  .##+.+#+.###++. +-   -########################",
    "ENTER",
    "STRING ########################-      +  -+#+  + ++#+.-. ..-.+#++.+  ++++  +      -########################",
    "ENTER",
    "STRING #############################-   .+ .-++#+####+-.--.-+###++#++-. +.   -#############################",
    "ENTER",
    "STRING #################################.  .+  +.+-###++--+####-+.+  +.  .##########+######################",
    "ENTER",
    "STRING #####################+##############+ -# +-+##+##++#++##+-+ #+ +####################################",
    "ENTER",
    "STRING ###########################++###############+-.+##+##.-+############################################",
    "ENTER",
    "STRING ############################################+#-+++#+++#--+##########################################",
    "ENTER",
    "STRING ######################################--####+#+++--+-++++##++ -#####++##############################",
    "ENTER",
    "STRING ####################################+.######+.+-#+.-+-..-+##+--##+-+##-#############################",
    "ENTER",
    "STRING ###################################-+ -#+####- ++.--.++  . +###+#++ ##+#############################",
    "ENTER",
    "STRING ################################+####-.      .#+ -- --#+- -.  +#+  ++###############################",
    "ENTER",
    "STRING ################################++###########.  -+ .+.#-#+ +##++++##################################",
    "ENTER",
    "STRING ################################-+######.   .-+#. .+.+#+++ +#####+##################################",
    "ENTER",
    "STRING #################################+++##+ -#####- .+#+#####--#########################################",
    "ENTER",
    "STRING #######################################-#####+-#########+###########################################",
    "ENTER",
    "STRING ########################################++##########################################################",
    "ENTER",
    "STRING ##################-     ##+  .###############+     ###   ###   ##      .###+     ###################",
    "ENTER",
    "STRING ################+    -   -+   ##############    -    #     #   #   +++   #    -   .#################",
    "ENTER",
    "STRING ################+   ++-   +   ######       #   +++   #         #   ###   #   -++   #################",
    "ENTER",
    "STRING ################+         +   #######++++++#         #   #-    #   #     #         #################",
    "ENTER",
    "STRING ################+   ##+   +        #########   ###   #   ###   #-       .#   ###   #####-  -########",
    "ENTER",
    "STRING ##################++####+###++++++###########+#####+###+#####+####++++   ##++####++######++#########",
    "ENTER",
    "STRING ####################################################################################################",
    "ENTER",
    "STRING ##########-. . - .  -.  +  .  -####  .  .-     -  . .    +####-.. .     .       - -   .#############",
    "ENTER",
    "STRING ##########-  . - .. -. . . - .- +## .-. . .-- .-. .+.-- .+-+##+..   +. #-+    +.. -   .# +##########",
    "ENTER",
    "STRING ####################################################################################################",
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

    // Static instruction block — drawn once, and re-drawn after an unlock (the lock
    // overlay wipes it, so on wake we must repaint the whole thing, not just the spinner).
    int wy = 0;
    auto drawStatic = [&]() {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(0x7BEF);     dm.printText("[");
        dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
        dm.setTextColor(0x7BEF);     dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText(clone ? "CLONE" : "EXEC");
        dm.setTextColor(0x7BEF);     dm.println("]");
        dm.printSeparator();
        if (clone) {
            bool macOk = bleKeyboard.badusbCloneMacOk();
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_RED);   dm.printText("CLONE ");
            dm.setTextColor(TFT_WHITE); dm.println(cloneMacStr);
            dm.setCursor(10, dm.getCursorY());
            if (macOk) {
                dm.setTextColor(TFT_GREEN); dm.println("MAC+name spoofed. Waiting for");
                dm.setCursor(10, dm.getCursorY());
                dm.setTextColor(0x7BEF);    dm.println("host reconnect (real dev off). q=x");
            } else {
                // ble_hs_id_set_rnd rejected the addr (RPA/public) — honest: name-only, no auto-reconnect
                dm.setTextColor(TFT_YELLOW); dm.println("MAC spoof FAILED (RPA/public");
                dm.setCursor(10, dm.getCursorY());
                dm.setTextColor(TFT_YELLOW); dm.println("addr) - NAME ONLY, connect manually. q=x");
            }
        } else {
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_WHITE); dm.printText("Advertising: ");
            dm.println((cloneName && *cloneName) ? cloneName : "AL-ANQA-KBD");
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(0x7BEF);    dm.println("Connect from target (no PIN,");
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(0x7BEF);    dm.println("no bond). q/hold=cancel.");
        }
        wy = dm.getCursorY();
    };
    drawStatic();

    uint32_t lastMs  = 0;
    uint8_t  spinIdx = 0;
    const char* spinChars = "|/-\\";
    bool     clickHeld   = false;
    uint32_t clickDownMs = 0;

    while (!bleKeyboard.badusbConnected()) {
        uint32_t now = millis();
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { bleKeyboard.badusbEnd(); return false; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { drawStatic(); lastMs = 0; }

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
    _bleLost          = false;
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
    dm.println(_bleLost ? "Host disconnected." : (_aborted ? "Aborted." : "Done."));
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
    // Separator sits at y+LINE_HEIGHT/2, so anchor it ABOVE the hint text (was y=210 →
    // line at 217, cutting through the hint at 214).
    dm.setCursor(0, layoutFooterY(38));   dm.printSeparator();
    dm.setCursor(6, layoutFooterY(26));   dm.setTextColor(0x7BEF); dm.printText(hint);
    dm.setTextColor(TFT_WHITE);
}

int BadUsb::blePickMode() {
    DisplayManager& dm = displayManager;
    const char* items[2] = { "Connect (fresh keyboard)", "Spoof (clone bonded dev)" };
    int sel = 0; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("BADBLE");
            dm.setCursor(10, outputY + 2 * LINE_HEIGHT);   // clear the header separator line
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

    // Show the SAME list as the last `sbl` scan (shared s_bleDevices cache) so devices
    // cross-reference by MAC/index. Only scan here if the cache is empty (never ran sbl),
    // or on demand with [u] — a fresh scan gives a different snapshot than sbl (BLE devices
    // come/go and RPA devices rotate their MAC), which is why the lists would otherwise
    // diverge. `scanBleIntoCache()` repopulates the cache; stopBleScan() idles the radio.
    bool needScan  = (s_bleCount == 0);   // auto-scan only when there's no prior scan data
    bool needBuild = true;                // (re)build the sorted display order + page counts
    static int order[64];
    int total = 0, totalPages = 1;
    const int PER = 9;
    int page = 0, sel = 0; bool redraw = true;

    while (true) {
        if (needScan) {
            needScan = false;
            if (bluetoothFunctions.scanBleIntoCache() < 0) return -1;   // [q] aborted scan
            bluetoothFunctions.stopBleScan();
            needBuild = true;
        }
        if (needBuild) {
            needBuild = false;
            total = s_bleCount; if (total > 64) total = 64;
            for (int i = 0; i < total; i++) order[i] = i;              // RSSI-sorted (display only)
            for (int i = 0; i < total - 1; i++)
                for (int j = i + 1; j < total; j++)
                    if (s_bleDevices[order[j]].rssi > s_bleDevices[order[i]].rssi) {
                        int t = order[i]; order[i] = order[j]; order[j] = t;
                    }
            totalPages = (total + PER - 1) / PER; if (totalPages < 1) totalPages = 1;
            page = 0; sel = 0; redraw = true;
        }

        int start = page * PER, pageCount = min(PER, total - start);
        if (pageCount < 0) pageCount = 0;
        if (sel >= pageCount) sel = pageCount - 1; if (sel < 0) sel = 0;
        if (redraw) {
            dm.clearScreen(); dm.setDefaultTextSize();
            dm.setCursor(2, outputY);                        // header (sbl-style)
            dm.setTextColor(0x7BEF);     dm.printText("[");
            dm.setTextColor(TFT_CYAN);   dm.printText("BLE");
            dm.setTextColor(0x7BEF);     dm.printText("::");
            dm.setTextColor(TFT_YELLOW); dm.printText("CLONE");
            dm.setTextColor(0x7BEF);     dm.printText("]  ");
            char hb[24]; snprintf(hb, sizeof(hb), "%d dev  %d/%d", total, page + 1, totalPages);
            dm.setTextColor(TFT_WHITE);  dm.printText(hb);
            dm.setTextColor(0x7BEF);                         // column headers
            dm.setCursor(2,   outputY + LINE_HEIGHT); dm.printText("#");
            dm.setCursor(24,  outputY + LINE_HEIGHT); dm.printText("NAME");
            dm.setCursor(122, outputY + LINE_HEIGHT); dm.printText("RSSI");
            dm.setCursor(150, outputY + LINE_HEIGHT); dm.printText("AT");
            dm.setCursor(176, outputY + LINE_HEIGHT); dm.printText("MAC");
            dm.setCursor(2,   outputY + 2 * LINE_HEIGHT); dm.printSeparator();
            if (total == 0) {
                dm.setCursor(24, outputY + 4 * LINE_HEIGHT);
                dm.setTextColor(TFT_YELLOW); dm.println("No BLE devices in range.");
                dm.setCursor(24, dm.getCursorY());
                dm.setTextColor(0x7BEF);     dm.println("[u] rescan   [q] back");
            }
            for (int si = start; si < start + pageCount; si++) {
                int oi = order[si]; BleEntry& d = s_bleDevices[oi];
                int ry = outputY + (3 + (si - start)) * LINE_HEIGHT;
                bool s = ((si - start) == sel);
                if (s) dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                unsigned msb = 0; sscanf(d.addr, "%2x", &msb);
                bool clonable = ((msb & 0xC0) == 0xC0) && (d.addrType != 0);  // static-random only
                // green name+MAC = clonable target; grey = name-only (can't spoof its MAC)
                uint16_t rowCol = clonable ? TFT_GREEN : 0x7BEF;
                dm.setCursor(2, ry);   dm.setTextColor(s ? TFT_YELLOW : TFT_CYAN);
                char ib[5]; snprintf(ib, sizeof(ib), "%2d", oi); dm.printText(ib);
                char nm[16]; snprintf(nm, sizeof(nm), "%-15.15s", d.name[0] ? d.name : "(unknown)");
                dm.setCursor(24, ry);  dm.setTextColor(s ? TFT_WHITE : rowCol); dm.printText(nm);
                char rb[6]; snprintf(rb, sizeof(rb), "%d", d.rssi);
                dm.setCursor(122, ry); dm.setTextColor(0x7BEF); dm.printText(rb);
                dm.setCursor(150, ry); dm.printText(d.addrType == 0 ? "pub" : "rnd");
                dm.setCursor(176, ry); dm.setTextColor(s ? TFT_YELLOW : rowCol);
                char mb[22]; snprintf(mb, sizeof(mb), "%s%s", d.addr, clonable ? " *" : "");
                dm.printText(mb);   // full MAC (matches sbl) so devices cross-reference
            }
            // footer: keys line + a legend that explains the green/`*` marker
            dm.setCursor(0, layoutFooterY(38)); dm.printSeparator();
            dm.setCursor(6, layoutFooterY(28)); dm.setTextColor(0x7BEF);
            dm.printText("trkbl=sel a/l=pg u=scan i=info ent=pick q=back");
            dm.setCursor(6, layoutFooterY(14)); dm.setTextColor(TFT_GREEN);
            dm.printText("green */rnd = clonable  ");
            dm.setTextColor(0x7BEF);
            dm.printText("pub/RPA = name-only");
            dm.setTextColor(TFT_WHITE);
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)    { if (sel > 0)              { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < pageCount - 1)  { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { if (total > 0) return order[start + sel]; continue; }
        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q')   return -1;
        if (k == 'u' || k == 'U')   { needScan = true; continue; }
        if ((k == 'i' || k == 'I') && total > 0) {
            // Inspect the selected device's GATT with bi (reveals its real name in
            // 0x2A00, services, security posture) — handy for choosing a spoof name.
            static char ibmac[18];
            strncpy(ibmac, s_bleDevices[order[start + sel]].addr, sizeof(ibmac) - 1);
            ibmac[sizeof(ibmac) - 1] = '\0';
            runBleInfo(ibmac);
            redraw = true; continue;   // bi took the screen — repaint the picker on return
        }
        if ((k == 'l' || k == 'L') && page < totalPages - 1) { page++; sel = 0; redraw = true; }
        if ((k == 'a' || k == 'A') && page > 0)              { page--; sel = 0; redraw = true; }
        if ((k == '\r' || k == '\n') && total > 0) return order[start + sel];
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
            dm.setCursor(10, outputY + 2 * LINE_HEIGHT);   // clear the header separator line
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

void BadUsb::blePromptName(char* buf, size_t n, const char* hint) {
    DisplayManager& dm = displayManager;
    int len = strlen(buf);
    bool full = true;   // full chrome redraw vs. just the name line
    int  ny  = 0;
    while (true) {
        if (full) {
            drawBleHeader("NAME");
            dm.setCursor(10, outputY + 2 * LINE_HEIGHT);   // clear the header separator line
            dm.setTextColor(0x7BEF); dm.println("Advertised name:");
            if (hint && *hint) {
                dm.setCursor(10, dm.getCursorY());
                dm.setTextColor(TFT_YELLOW); dm.println(hint);
            }
            ny = dm.getCursorY();   // input line sits just below the label (+ hint if any)
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

// Nameless spoof target → offer believable generic names (a HID keyboard should look
// like a keyboard) so we never expose the clone with the default "AL-ANQA-KBD".
// `found` (if non-empty) is the real name bi read from the device's 0x2A00 char — shown
// first, pre-selected, so an [i] inspect flows straight into the spoof name (no retyping).
// Returns true once a name is set in buf; false = [q] go back to the previous screen.
bool BadUsb::blePickGenericName(char* buf, size_t n, const char* found) {
    DisplayManager& dm = displayManager;
    static const char* BASE[] = {
        "Keyboard",
        "Bluetooth Keyboard",
        "Wireless Keyboard",
        "Magic Keyboard",
        "MX Keys",
        "Keychron K3",
    };
    const int BASE_COUNT = sizeof(BASE) / sizeof(BASE[0]);
    const char* items[10];
    int count = 0;
    bool hasFound = (found && *found);
    if (hasFound) items[count++] = found;                  // index 0 = discovered name
    for (int i = 0; i < BASE_COUNT; i++) items[count++] = BASE[i];
    const int CUSTOM = count; items[count++] = "Custom (type name)...";

    int sel = 0; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("NAME");
            dm.setCursor(10, outputY + 2 * LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Target has no name - pick one:");
            for (int i = 0; i < count; i++) {
                int y = outputY + (3 + i) * LINE_HEIGHT; bool s = (i == sel);
                if (s) dm.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(6, y);
                dm.setTextColor(s ? TFT_YELLOW : 0x7BEF); dm.printText(s ? ">" : " ");
                if (hasFound && i == 0) {                   // discovered name — highlight it
                    dm.setTextColor(TFT_GREEN); dm.printText(items[i]); dm.println("  (found)");
                } else {
                    dm.setTextColor(s ? TFT_WHITE : 0x7BEF); dm.println(items[i]);
                }
            }
            drawBleFooter("trkbl=sel  ent=pick  q=back");
            redraw = false;
        }
        TrackballEvent tb = inputHandler.getTrackballEvent();
        bool pick = (tb == TBALL_CLICK);
        if (tb == TBALL_UP)   { if (sel > 0)          { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN) { if (sel < count - 1)  { sel++; redraw = true; } continue; }
        char k = inputHandler.getKeyboardInput();
        if (!k && !pick) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q') return false;                // back to previous screen
        if (pick || k == '\r' || k == '\n') {
            if (sel == CUSTOM) blePromptName(buf, n, "Type a device name:");
            else { strncpy(buf, items[sel], n - 1); buf[n - 1] = '\0'; }
            return true;
        }
    }
}

// ── startInteractiveUsb() ─────────────────────────────────────────────────────
// USB transport already chosen — just pick a script and run it.
void BadUsb::startInteractiveUsb() {
    static char scriptPath[128];
    if (!blePickScript(scriptPath, sizeof(scriptPath))) {
        displayManager.printCommandScreen(); return;
    }
    start(scriptPath, false, nullptr, 1, nullptr);
}

// ── startInteractive() — unified: pick transport, then delegate ───────────────
// Replaces the old BLE-only version.  Bare `ux` lands here; bare `ux ble` skips
// the transport step and calls the BLE picker directly from handleUsbExecCmd.
void BadUsb::startInteractive() {
    DisplayManager& dm = displayManager;
    // Transport picker: USB vs BLE
    const char* items[2] = { "USB  (inject via cable)", "BLE  (inject over Bluetooth)" };
    int sel = 0; bool redraw = true;
    while (true) {
        if (redraw) {
            drawBleHeader("BADUSB");
            dm.setCursor(10, outputY + 2 * LINE_HEIGHT);
            dm.setTextColor(0x7BEF); dm.println("Select transport:");
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
        if (tb == TBALL_UP)    { if (sel > 0) { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < 1) { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { if (sel == 0) { startInteractiveUsb(); return; } break; }
        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q') { dm.printCommandScreen(); return; }
        if (k == '\r' || k == '\n') { if (sel == 0) { startInteractiveUsb(); return; } break; }
        if (k == '1') { startInteractiveUsb(); return; }
        if (k == '2') break;   // fall through to BLE flow
    }
    // BLE path: stop macwatch before bringing up BLE HID (mirrors the ux-ble-arg path)
    stopMacwatchBg();
    int mode = blePickMode();
    if (mode < 0) { dm.printCommandScreen(); return; }
    static char macBuf2[18]; static char nameBuf2[24];
    const char* cloneMac = nullptr; uint8_t cloneType = 1;
    nameBuf2[0] = '\0';
    if (mode == 1) {
        while (true) {
            int idx = blePickTarget();
            if (idx < 0) { dm.printCommandScreen(); return; }
            strncpy(macBuf2, s_bleDevices[idx].addr, sizeof(macBuf2) - 1); macBuf2[sizeof(macBuf2) - 1] = '\0';
            cloneMac  = macBuf2;
            cloneType = s_bleDevices[idx].addrType;
            strncpy(nameBuf2, s_bleDevices[idx].name, sizeof(nameBuf2) - 1); nameBuf2[sizeof(nameBuf2) - 1] = '\0';
            if (nameBuf2[0] != '\0') break;
            const char* found = (strcmp(bleInfoLastMac(), macBuf2) == 0) ? bleInfoLastName() : "";
            if (blePickGenericName(nameBuf2, sizeof(nameBuf2), found)) break;
        }
    } else {
        strncpy(nameBuf2, "AL-ANQA-KBD", sizeof(nameBuf2) - 1); nameBuf2[sizeof(nameBuf2) - 1] = '\0';
        blePromptName(nameBuf2, sizeof(nameBuf2));
    }
    const char* cloneName = nameBuf2[0] ? nameBuf2 : nullptr;
    static char scriptPath2[128];
    if (!blePickScript(scriptPath2, sizeof(scriptPath2))) { dm.printCommandScreen(); return; }
    start(scriptPath2, true, cloneMac, cloneType, cloneName);
}

// ── LED event handler (file-scope, registered once) ──────────────────────────
// We use a plain function (not a lambda) so registering it multiple times is
// idempotent — esp_event_handler_register_with deduplicates by (fn, arg) pair.
static volatile uint8_t s_osLedByte     = 0xFF;
static volatile bool    s_osLedReceived = false;

static void osLedCb(void*, esp_event_base_t, int32_t, void* data) {
    auto* ev = reinterpret_cast<arduino_usb_hid_keyboard_event_data_t*>(data);
    s_osLedByte     = ev->leds;
    s_osLedReceived = true;
}

// ── probeOs() — NumLock LED probe to fingerprint the host OS ──────────────────
// Presses NumLock, waits ≤500ms for a HID LED SET_REPORT from the host, then
// restores NumLock.  Windows always sends NumLock=1; macOS ignores NumLock keys
// (no LED response); Linux varies.
OsType BadUsb::probeOs() {
    s_osLedByte     = 0xFF;
    s_osLedReceived = false;

    // Register a plain-function handler — ESP-IDF deduplicates (fn, arg) pairs,
    // so repeated calls to probeOs() don't pile up handler instances.
    g_hid_keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, osLedCb);

    // Toggle NumLock: press + release
    _sink->press(KEY_USB_NUMLOCK);
    vTaskDelay(pdMS_TO_TICKS(50));
    _sink->releaseAll();

    uint32_t t0 = millis();
    while (!s_osLedReceived && millis() - t0 < 500) vTaskDelay(pdMS_TO_TICKS(10));

    OsType os = OS_UNKNOWN;
    if (s_osLedReceived) {
        // LED response received. The bit reflects NumLock state AFTER the toggle.
        // Windows default = NumLock ON  → press toggles to OFF  → LED=0 → detect Windows
        // Linux   default = NumLock OFF → press toggles to ON   → LED=1 → detect Linux
        // macOS ignores NumLock keys entirely → no LED event → OS_UNKNOWN (timeout)
        // This heuristic assumes the default NumLock state hasn't been manually changed.
        os = ((s_osLedByte & LED_NUMLOCK) == 0) ? OS_WINDOWS : OS_LINUX;
    }

    // Restore NumLock state (toggle again so the OS ends up where it started)
    _sink->press(KEY_USB_NUMLOCK);
    vTaskDelay(pdMS_TO_TICKS(50));
    _sink->releaseAll();
    vTaskDelay(pdMS_TO_TICKS(200));   // let the host settle

    _detectedOs = os;
    return os;
}

// ── startAuto() — probe OS then auto-select script dir ────────────────────────
void BadUsb::startAuto(const char* dir) {
    DisplayManager& dm = displayManager;

    if (!usbManager.isConnected()) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(TFT_RED); dm.println("[USB::AUTO] Not connected.");
        vTaskDelay(pdMS_TO_TICKS(2000)); dm.printCommandScreen(); return;
    }
    _ble  = false;
    _sink = g_usbHidSink;
    _sink->releaseAll();
    vTaskDelay(pdMS_TO_TICKS(500));

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("USB");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.println("AUTO]");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF); dm.println("Probing OS via NumLock LED...");

    OsType os = probeOs();
    const char* osName = (os == OS_WINDOWS) ? "WINDOWS" :
                         (os == OS_LINUX)   ? "Linux"   :
                         (os == OS_MACOS)   ? "macOS"   : "Unknown (no LED)";
    uint16_t osCol = (os == OS_WINDOWS) ? TFT_GREEN :
                     (os == OS_LINUX)   ? 0xFD20 :   // orange
                     (os == OS_MACOS)   ? TFT_CYAN  : TFT_YELLOW;

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF); dm.printText("OS: ");
    dm.setTextColor(osCol);  dm.println(osName);

    const char* osDir = (os == OS_WINDOWS) ? SD_DIR_BADUSB_OS_WIN :
                        (os == OS_LINUX)   ? SD_DIR_BADUSB_OS_LIN :
                        (os == OS_MACOS)   ? SD_DIR_BADUSB_OS_MAC : nullptr;

    // If caller passed an explicit dir, use that regardless
    static char resolvedDir[64];
    if (dir && *dir) {
        sdCardManager.resolvePath(dir, resolvedDir, sizeof(resolvedDir));
        osDir = resolvedDir;
    }

    // Try to pick a script from the OS dir; fall back to the main scripts dir
    static char scriptPath[128];
    const char* searchDir = (osDir && sdCardManager.isReady() && SD.exists(osDir)) ? osDir : SD_DIR_BADUSB_SCRIPTS;
    // Inline: reuse the BLE script picker adapted for an explicit directory
    static String files[24]; int fc = 0;
    files[fc++] = "demo";
    if (sdCardManager.isReady()) {
        File d = SD.open(searchDir);
        if (d) {
            File f2 = d.openNextFile();
            while (f2 && fc < 24) {
                if (!f2.isDirectory()) files[fc++] = String(f2.name());
                f2 = d.openNextFile();
            }
            d.close();
        }
    }

    if (fc <= 1) {
        // No scripts in the OS dir — fall back to full picker
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW); dm.println("No scripts in OS dir.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("Opening picker...");
        vTaskDelay(pdMS_TO_TICKS(1200));
        if (!blePickScript(scriptPath, sizeof(scriptPath))) { dm.printCommandScreen(); return; }
    } else if (fc == 2) {
        // Exactly one script — use it directly (skip picker)
        const char* nm = files[1].c_str();
        if (nm[0] == '/') strncpy(scriptPath, nm, sizeof(scriptPath) - 1);
        else snprintf(scriptPath, sizeof(scriptPath), "%s/%s", searchDir, nm);
        scriptPath[sizeof(scriptPath) - 1] = '\0';
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.printText("Script: ");
        const char* base = strrchr(scriptPath, '/'); dm.println(base ? base + 1 : scriptPath);
        vTaskDelay(pdMS_TO_TICKS(800));
    } else {
        // Multiple scripts in the OS dir — show an inline picker over files[]
        // (blePickScript() hardcodes SD_DIR_BADUSB_SCRIPTS; can't be used here)
        vTaskDelay(pdMS_TO_TICKS(400));
        int pick = 1; bool redraw2 = true;
        while (true) {
            if (redraw2) {
                drawBleHeader("AUTO — pick script");
                for (int j = 1; j < fc; j++) {  // files[0] == "demo", skip for display
                    int py = outputY + (1 + j) * LINE_HEIGHT; bool sel = (j == pick);
                    if (sel) dm.fillRect(0, py - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                    dm.setCursor(6, py);
                    dm.setTextColor(sel ? TFT_YELLOW : 0x7BEF); dm.printText(sel ? ">" : " ");
                    const char* nm2 = files[j].c_str();
                    const char* b2  = strrchr(nm2, '/');
                    dm.setTextColor(sel ? TFT_WHITE : 0x7BEF); dm.println(b2 ? b2 + 1 : nm2);
                }
                drawBleFooter("trkbl=sel  ent=pick  q=cancel");
                redraw2 = false;
            }
            TrackballEvent tb2 = inputHandler.getTrackballEvent();
            if (tb2 == TBALL_UP)   { if (pick > 1) { pick--; redraw2 = true; } continue; }
            if (tb2 == TBALL_DOWN) { if (pick < fc - 1) { pick++; redraw2 = true; } continue; }
            if (tb2 == TBALL_CLICK) break;
            char ck2 = inputHandler.getKeyboardInput();
            if (!ck2) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw2 = true; continue; }
            if (ck2 == 'q' || ck2 == 'Q') { dm.printCommandScreen(); return; }
            if (ck2 == '\r' || ck2 == '\n') break;
        }
        const char* nm = files[pick].c_str();
        if (nm[0] == '/') strncpy(scriptPath, nm, sizeof(scriptPath) - 1);
        else snprintf(scriptPath, sizeof(scriptPath), "%s/%s", searchDir, nm);
        scriptPath[sizeof(scriptPath) - 1] = '\0';
    }

    _aborted = false; _bleLost = false;
    _defaultCharDelay = 8; _nextCharDelay = -1;
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[USB::AUTO] ");
    dm.setTextColor(osCol);      dm.println(osName);
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE); dm.printText("Script: ");
    const char* base = strrchr(scriptPath, '/'); dm.println(base ? base + 1 : scriptPath);
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF); dm.println("q to abort.");
    vTaskDelay(pdMS_TO_TICKS(800));

    if (strcmp(scriptPath, "demo") == 0) runDemo(); else runFile(scriptPath);

    _sink->releaseAll();
    dm.clearScreen(); dm.setCursor(10, outputY);
    dm.setTextColor(_aborted ? TFT_YELLOW : TFT_GREEN);
    dm.println(_aborted ? "Aborted." : "Done.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── startRemote() — SoftAP web trigger ───────────────────────────────────────
// Starts a SoftAP + WebServer so a phone can browse the script list and fire
// scripts wirelessly while the T-Deck is plugged into the victim PC.
// SD reads (script file content) are fine during SoftAP (GDMA rule = writes only).
// Any session log is held in RAM and written to SD after WiFi teardown.
void BadUsb::startRemote(const char* ssid) {
    DisplayManager& dm = displayManager;

    if (!usbManager.isConnected()) {
        dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
        dm.setTextColor(TFT_RED); dm.println("[USB::REMOTE] Not connected to PC.");
        vTaskDelay(pdMS_TO_TICKS(2000)); dm.printCommandScreen(); return;
    }

    _ble  = false;
    _sink = g_usbHidSink;
    _sink->releaseAll();
    vTaskDelay(pdMS_TO_TICKS(300));
    _aborted = false; _bleLost = false;
    _defaultCharDelay = 8; _nextCharDelay = -1;

    // ── Read script list from SD before starting WiFi ─────────────────────────
    static String scripts[24]; int sc = 0;
    scripts[sc++] = "demo";
    if (sdCardManager.isReady()) {
        File d = SD.open(SD_DIR_BADUSB_SCRIPTS);
        if (d) {
            File f2 = d.openNextFile();
            while (f2 && sc < 24) {
                if (!f2.isDirectory()) scripts[sc++] = String(f2.name());
                f2 = d.openNextFile();
            }
            d.close();
        }
    }

    // ── Start SoftAP ─────────────────────────────────────────────────────────
    stopEspchatBg();   // must stop ESP-NOW before changing WiFi mode (mirrors all other WiFi cmds)
    const char* apSsid = (ssid && *ssid) ? ssid : "AL-ANQA-CMD";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, nullptr);   // open, no password
    vTaskDelay(pdMS_TO_TICKS(500));

    WebServer server(80);

    // Build the script list once as an HTML page served from RAM.
    // No SD reads during serving — page is pre-built into a static buffer.
    static char pageHtml[6144];
    {
        char* p = pageHtml; int rem = sizeof(pageHtml);
        int n = snprintf(p, rem,
            "<!DOCTYPE html><html><head>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>AL-ANQA Remote</title>"
            "<style>"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{background:#0a0a0a;color:#ddd;font-family:monospace;padding:12px;max-width:480px;margin:0 auto}"
            "h2{color:#0ff;font-size:1.3em;margin-bottom:4px}"
            ".info{color:#555;font-size:.8em;margin-bottom:14px}"
            ".status{background:#111;border:1px solid #333;border-radius:6px;padding:10px 14px;"
            "margin-bottom:14px;font-size:.95em;min-height:44px;display:flex;align-items:center}"
            ".status.idle{color:#888}.status.queued{color:#ff0}.status.running{color:#0f0}.status.done{color:#0cf}.status.err{color:#f44}"
            ".lbl{color:#555;font-size:.75em;text-transform:uppercase;letter-spacing:.08em;margin-bottom:6px;margin-top:10px}"
            "button{display:block;width:100%%;margin:6px 0;padding:14px 12px;border-radius:6px;"
            "background:#151515;color:#0f0;border:1px solid #1a3a1a;font-family:monospace;"
            "font-size:1em;text-align:left;cursor:pointer;transition:background .1s,border-color .1s}"
            "button:hover{background:#0f2a0f;border-color:#0f0}"
            "button:active{background:#0f0;color:#000}"
            "button.running{opacity:.4;pointer-events:none}"
            "button.demo{color:#0cf;border-color:#1a3040}"
            "button.demo:hover{background:#0a1f2a}"
            ".prog{width:100%%;height:3px;background:#111;border-radius:2px;overflow:hidden;margin-bottom:14px}"
            ".prog-bar{height:100%%;background:#0f0;width:0%%;transition:width .3s}"
            "</style></head>"
            "<body><h2>&#9762; AL-ANQA REMOTE</h2>"
            "<div class='info'>SSID: %s &nbsp;|&nbsp; 192.168.4.1</div>"
            "<div class='status idle' id='st'>&#x25CB; Ready — tap a script to fire</div>"
            "<div class='prog'><div class='prog-bar' id='pb'></div></div>",
            apSsid);
        p += n; rem -= n;

        // demo always first
        n = snprintf(p, rem,
            "<div class='lbl'>Built-in</div>"
            "<button class='demo' id='b_demo' onclick=\"fire('demo')\">&#9658; demo</button>"
            "<div class='lbl'>SD Scripts (%d)</div>", sc - 1);
        p += n; rem -= n;

        for (int i = 1; i < sc && rem > 128; i++) {
            const char* nm = scripts[i].c_str();
            const char* base = strrchr(nm, '/'); if (base) nm = base + 1;
            // strip .txt extension for display
            static char disp[48];
            strncpy(disp, nm, sizeof(disp) - 1); disp[sizeof(disp) - 1] = '\0';
            char* dot = strrchr(disp, '.'); if (dot) *dot = '\0';
            n = snprintf(p, rem,
                "<button id='b_%d' onclick=\"fire('%s')\">&#9658; %s</button>",
                i, nm, disp);
            p += n; rem -= n;
        }

        snprintf(p, rem,
            "<script>"
            "var busy=false,pollT=null;"
            "function setStatus(cls,txt){"
            "var el=document.getElementById('st');"
            "el.className='status '+cls;el.innerHTML=txt;}"
            "function setBusy(b){"
            "busy=b;"
            "document.querySelectorAll('button').forEach(function(btn){btn.classList.toggle('running',b);});"
            "}"
            "function poll(){"
            "fetch('/status').then(function(r){return r.json();}).then(function(d){"
            "if(d.running){"
            "var pct=d.total>0?Math.round(d.line*100/d.total):0;"
            "setStatus('running','&#9654; Running&hellip; '+d.line+'/'+d.total);"
            "document.getElementById('pb').style.width=pct+'%%';"
            "pollT=setTimeout(poll,400);"
            "}else if(busy){"
            "setBusy(false);"
            "setStatus('done','&#10003; Done');"
            "document.getElementById('pb').style.width='0%%';"
            "}"
            "}).catch(function(){pollT=setTimeout(poll,800);});}"
            "function fire(s){"
            "if(busy)return;"
            "setBusy(true);"
            "setStatus('queued','&#9200; Queued: '+s+' (3s delay&hellip;)');"
            "fetch('/run?script='+encodeURIComponent(s),{method:'POST'})"
            ".then(function(r){return r.json();})"
            ".then(function(d){"
            "if(d.ok){setTimeout(function(){pollT=setTimeout(poll,500);},3200);}"
            "else{setBusy(false);setStatus('err','&#10007; Error');}"
            "}).catch(function(){setBusy(false);setStatus('err','&#10007; Network error');});}"
            "</script></body></html>");
    }

    // Pending script request from phone (empty = none)
    // Explicit reset each call — static initializers run only once.
    static char pendingScript[128];
    static bool pendingReady = false;
    pendingScript[0] = '\0';
    pendingReady = false;

    server.on("/", HTTP_GET, [&]() {
        server.send(200, "text/html", pageHtml);
    });
    server.on("/run", HTTP_POST, [&]() {
        String s = server.arg("script");
        if (s.length() > 0 && s.length() < sizeof(pendingScript) - 1) {
            strncpy(pendingScript, s.c_str(), sizeof(pendingScript) - 1);
            pendingScript[sizeof(pendingScript) - 1] = '\0';
            pendingReady = true;
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"ok\":false}");
        }
    });
    server.on("/status", HTTP_GET, [&]() {
        char buf[64];
        snprintf(buf, sizeof(buf),
            "{\"running\":%s,\"line\":%d,\"total\":%d}",
            pendingReady ? "true" : "false", _scriptLine, _scriptTotal);
        server.send(200, "application/json", buf);
    });
    server.begin();

    // ── Draw UI ───────────────────────────────────────────────────────────────
    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("USB");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.println("REMOTE]");
    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);  dm.printText("SSID: "); dm.println(apSsid);
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);     dm.println("IP:   192.168.4.1");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);     dm.println("Open browser on phone.");
    dm.printSeparator();
    int statusY = dm.getCursorY();

    // Session log (RAM — flushed after WiFi teardown to respect GDMA rule)
    static char sessionLog[2048]; int logOff = 0; sessionLog[0] = '\0';

    while (true) {
        server.handleClient();

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
            dm.setTextColor(0x7BEF);     dm.printText("[");
            dm.setTextColor(TFT_CYAN);   dm.printText("USB");
            dm.setTextColor(0x7BEF);     dm.printText("::");
            dm.setTextColor(TFT_YELLOW); dm.println("REMOTE]");
            dm.printSeparator();
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_WHITE);  dm.printText("SSID: "); dm.println(apSsid);
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(0x7BEF);     dm.println("IP:   192.168.4.1");
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(0x7BEF);     dm.println("Open browser on phone.");
            dm.printSeparator();
            statusY = dm.getCursorY();
        }

        if (pendingReady) {
            // ── 3-second countdown + cancel ───────────────────────────────────
            bool cancelled = false;
            for (int cd = 3; cd > 0 && !cancelled; cd--) {
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(TFT_YELLOW); dm.printText("Firing in ");
                char cdbuf[8]; snprintf(cdbuf, sizeof(cdbuf), "%d...", cd);
                dm.println(cdbuf);
                dm.setCursor(10, statusY + LINE_HEIGHT);
                dm.setTextColor(0x7BEF); dm.println("q = cancel");
                for (int t = 0; t < 10 && !cancelled; t++) {   // 10 × 100ms = 1s
                    server.handleClient();
                    char ck = inputHandler.getKeyboardInput();
                    if (ck == 'q' || ck == 'Q') { cancelled = true; pendingReady = false; pendingScript[0] = '\0'; }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
            if (!cancelled) {
                // Resolve script path
                static char runPath[128];
                if (strcmp(pendingScript, "demo") == 0) {
                    strncpy(runPath, "demo", sizeof(runPath));
                } else {
                    snprintf(runPath, sizeof(runPath), "%s/%s", SD_DIR_BADUSB_SCRIPTS, pendingScript);
                }

                // Log to RAM
                if (logOff < (int)sizeof(sessionLog) - 64) {
                    int n = snprintf(sessionLog + logOff, sizeof(sessionLog) - logOff,
                                     "%lu %s\n", millis(), pendingScript);
                    logOff += n;
                }

                // Execute
                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 3, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(TFT_GREEN); dm.printText("Running: ");
                dm.println(pendingScript);

                _aborted = false; _bleLost = false;
                _defaultCharDelay = 8; _nextCharDelay = -1;
                if (strcmp(runPath, "demo") == 0) runDemo(); else runFile(runPath);
                _sink->releaseAll();

                pendingReady = false; pendingScript[0] = '\0';

                dm.fillRect(0, statusY, SCREEN_WIDTH, LINE_HEIGHT * 3, TFT_BLACK);
                dm.setCursor(10, statusY);
                dm.setTextColor(_aborted ? TFT_YELLOW : TFT_GREEN);
                dm.println(_aborted ? "Aborted." : "Done. Waiting...");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // ── Tear down WiFi ────────────────────────────────────────────────────────
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);   // idle, not OFF (GDMA rule)
    vTaskDelay(pdMS_TO_TICKS(400));

    // ── Flush session log to SD (WiFi is now idle — safe) ─────────────────────
    if (logOff > 0 && sdCardManager.isReady()) {
        char logPath[52];
        int i;
        for (i = 1; i <= 999; i++) {
            snprintf(logPath, sizeof(logPath), "%s/remote_%03d.txt", SD_DIR_BADUSB, i);
            if (!SD.exists(logPath)) break;
        }
        if (i <= 999) {
            File lf = SD.open(logPath, FILE_WRITE);
            if (lf) { lf.print(sessionLog); lf.close(); }
        }
    }

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(TFT_GREEN); dm.println("Remote session ended.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
}

// ── bleHostLost() ─────────────────────────────────────────────────────────────
// BLE run: if the host has dropped, latch the loss + abort so callers stop firing
// keystrokes into a dead link. No-op on USB (checked once up front in start()).
bool BadUsb::bleHostLost() {
    if (_ble && !bleKeyboard.badusbConnected()) { _bleLost = true; _aborted = true; return true; }
    return false;
}

// ── runDemo() ─────────────────────────────────────────────────────────────────
void BadUsb::runDemo() {
    DisplayManager& dm = displayManager;

    // Detect the host OS so we open the right editor before drawing the art.
    // The NumLock-LED probe only works over USB HID; BLE HID gets no LED event,
    // so BadBLE falls back to the Windows preamble.
    OsType os = OS_WINDOWS;
    if (!_ble) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("Detecting OS (NumLock LED)...");
        os = probeOs();
        if (os == OS_UNKNOWN) os = OS_MACOS;   // no LED response ≈ macOS
    }

    const char* osName = (os == OS_WINDOWS) ? "Windows" :
                         (os == OS_MACOS)   ? "macOS"   : "Linux";
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);   dm.printText("Target OS: ");
    dm.setTextColor(TFT_CYAN); dm.println(osName);

    const char* const* opener = (os == OS_LINUX) ? OPEN_LINUX :
                                (os == OS_MACOS) ? OPEN_MAC   : OPEN_WIN;
    runLines(opener, arrLen(opener));   // open the OS-appropriate target
    runLines(DEMO_LINES, DEMO_COUNT);   // then type the phoenix art
    if (os == OS_LINUX)                 // close the heredoc so `cat` echoes it
        runLines(CLOSE_LINUX, arrLen(CLOSE_LINUX));
}

// ── drawScriptStatus() — update the "Line N/T" row without clearing the screen ──
static void drawScriptStatus(int line, int total) {
    DisplayManager& dm = displayManager;
    // Status row is fixed at outputY + 4*LINE_HEIGHT (below the header/sep/script/q lines).
    int sy = outputY + 4 * LINE_HEIGHT;
    dm.fillRect(0, sy, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    dm.setCursor(10, sy);
    dm.setTextColor(0x7BEF); dm.printText("Line ");
    char buf[24];
    if (total > 0) snprintf(buf, sizeof(buf), "%d/%d", line, total);
    else           snprintf(buf, sizeof(buf), "%d", line);
    dm.setTextColor(TFT_WHITE); dm.println(buf);
}

// ── runLines() — shared executor for demo (array) ─────────────────────────────
void BadUsb::runLines(const char* const* lines, int count) {
    int      defaultDelay = 0;
    String   lastLine     = "";
    _scriptTotal = count;
    _scriptLine  = 0;

    for (int i = 0; i < count && !_aborted; i++) {
        if (bleHostLost()) break;
        const char* raw = lines[i];
        if (!raw || *raw == '\0') continue;
        _scriptLine = i + 1;
        drawScriptStatus(_scriptLine, _scriptTotal);

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

    // Count non-empty lines for the live counter (quick pass, rewind)
    _scriptTotal = 0; _scriptLine = 0;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() > 0) _scriptTotal++;
    }
    f.seek(0);

    int    defaultDelay = 0;
    String lastLine     = "";

    while (f.available() && !_aborted) {
        if (bleHostLost()) break;
        String line = f.readStringUntil('\n');
        // Strip CR (CRLF files from Windows)
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        line.trim();
        if (line.length() == 0) continue;
        _scriptLine++;
        drawScriptStatus(_scriptLine, _scriptTotal);

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

    // ── HOLD / RELEASE — press-and-hold keys until RELEASE (chords, games) ─────
    // HOLD <key...>  presses each modifier/key and does NOT release them.
    // RELEASE        releases everything currently held.
    // (Any held keys are also released at script end via start()'s releaseAll().)
    if (strncmp(rawLine, "RELEASE", 7) == 0 && (rawLine[7] == ' ' || rawLine[7] == '\0')) {
        _sink->releaseAll();
        return true;
    }
    if (strncmp(rawLine, "HOLD ", 5) == 0) {
        char hbuf[128];
        strncpy(hbuf, rawLine + 5, sizeof(hbuf) - 1); hbuf[sizeof(hbuf) - 1] = '\0';
        for (char* t = strtok(hbuf, " \t"); t; t = strtok(nullptr, " \t")) {
            if (isModifier(t)) { _sink->press(modifierCode(t)); continue; }
            uint8_t kc = resolveSpecialKey(t);   // named key, or single char (lower-cased)
            if (kc) _sink->press(kc);
        }
        return true;   // deliberately not released
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
        if (bleHostLost()) return false;                        // BLE host dropped mid-delay
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
