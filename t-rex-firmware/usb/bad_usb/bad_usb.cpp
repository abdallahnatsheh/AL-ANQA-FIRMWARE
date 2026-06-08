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

// ── start() ───────────────────────────────────────────────────────────────────
void BadUsb::start(char* args) {
    DisplayManager& dm = displayManager;

    dm.clearScreen(); dm.setCursor(10, outputY); dm.setDefaultTextSize();
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("USB");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("EXEC");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    if (!usbManager.isConnected()) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_RED);  dm.println("Not connected to PC.");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);   dm.println("Plug in USB cable first.");
        vTaskDelay(pdMS_TO_TICKS(2500));
        dm.printCommandScreen(); return;
    }

    while (args && (*args == ' ' || *args == '\t')) args++;

    if (!args || *args == '\0') {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE); dm.println("Usage:");
        dm.setCursor(10, dm.getCursorY()); dm.println("  ux demo");
        dm.setCursor(10, dm.getCursorY()); dm.println("  ux /badusb/payload.txt");
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(0x7BEF);    dm.println("Scripts in /badusb/ on SD.");
        dm.printCommandScreen(); return;
    }

    // Flush any stale HID state from a previous run before sending new keystrokes
    g_hid_keyboard.releaseAll();
    vTaskDelay(pdMS_TO_TICKS(500));

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

    g_hid_keyboard.releaseAll();

    dm.clearScreen(); dm.setCursor(10, outputY);
    dm.setTextColor(_aborted ? TFT_YELLOW : TFT_GREEN);
    dm.println(_aborted ? "Aborted." : "Done.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    dm.printCommandScreen();
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
        g_hid_keyboard.press(hc->k1);
        g_hid_keyboard.press(hc->k2);
        if (hc->k3) g_hid_keyboard.press(hc->k3);
        if (nTok > 1) {
            uint8_t argKey = resolveSpecialKey(tokens[1]);
            if (argKey) g_hid_keyboard.press(argKey);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        g_hid_keyboard.releaseAll();
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
            g_hid_keyboard.print((char)c);
        } else if (c == '\n') {
            g_hid_keyboard.press(KEY_RETURN);
            vTaskDelay(pdMS_TO_TICKS(20));
            g_hid_keyboard.releaseAll();
        }
        if (charDelay > 0) vTaskDelay(pdMS_TO_TICKS(charDelay));
    }
}

// ── pressSpecialKey() ─────────────────────────────────────────────────────────
void BadUsb::pressSpecialKey(uint8_t keyCode) {
    g_hid_keyboard.press(keyCode);
    vTaskDelay(pdMS_TO_TICKS(30));
    g_hid_keyboard.releaseAll();
}

// ── pressCombo() ──────────────────────────────────────────────────────────────
void BadUsb::pressCombo(uint8_t mods[], int nMods, uint8_t key) {
    for (int i = 0; i < nMods; i++) g_hid_keyboard.press(mods[i]);
    if (key) g_hid_keyboard.press(key);
    vTaskDelay(pdMS_TO_TICKS(50));
    g_hid_keyboard.releaseAll();
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
