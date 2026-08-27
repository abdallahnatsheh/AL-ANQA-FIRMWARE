/**
 * @file   tpager_keyboard.cpp
 * @brief  TCA8418 keyboard engine for the T-Lora Pager. See tpager_keyboard.h.
 *
 * Faithful port of LilyGoLib LilyGoKeyboard::update()/getKeyChar() with HELD
 * modifiers (Sym/Caps/Alt active between press and release) instead of toggles.
 * Keymap + symbol layer + special-key raw values are taken verbatim from
 * LilyGoLib's keyboardConfig (see the T-Pager hardware memory).
 *
 * ⚠️ HW-VERIFY: the physical space key and the exact per-position map should be
 * confirmed on hardware with the `test keymap` diagnostic — LilyGo produce space
 * via a null-cell fall-through that is matrix-specific. Everything here is the
 * best faithful port; the diagnostic is how we finalize it.
 */
#include "tpager_keyboard.h"

#if defined(BOARD_TPAGER)

#include "pins.h"
#include <Wire.h>
#include <Adafruit_TCA8418.h>
#include "display_manager.h"
#include "input_handling.h"
#include "powersave_manager.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

static constexpr uint8_t KB_ROWS = 4;
static constexpr uint8_t KB_COLS = 10;

// Base + symbol layers (LilyGoLib keyboardConfig, verbatim).
static const char keymap[KB_ROWS][KB_COLS] = {
    {'q','w','e','r','t','y','u','i','o','p'},
    {'a','s','d','f','g','h','j','k','l','\n'},
    {'\0','z','x','c','v','b','n','m','\0','\0'},
    {' ','\0','\0','\0','\0','\0','\0','\0','\0','\0'},
};
static const char symbolmap[KB_ROWS][KB_COLS] = {
    {'1','2','3','4','5','6','7','8','9','0'},
    {'*','/','+','-','=',':','\'','"','@','\0'},
    {'\0','_','$',';','?','!',',','.','\0','\0'},
    {' ','\0','\0','\0','\0','\0','\0','\0','\0','\0'},
};

// Special-key raw values, 0-based (after the k-- below). CORRECTED from HW testing:
// LilyGoLib's config had these swapped for our unit — the bottom-left key (0x1E,
// LilyGo's "sym") is actually the SPACE bar, and the modifier under 'A' (0x14,
// LilyGo's "alt") is the real Sym/symbol key. Verify further with `test keymap`.
static constexpr uint8_t KV_SYM   = 0x14;   // modifier under 'A' → symbol layer + Sym+rotate
static constexpr uint8_t KV_CAPS  = 0x1C;
static constexpr uint8_t KV_BKSP  = 0x1D;
static constexpr uint8_t KV_SPACE = 0x1E;   // bottom-left key → space bar

static Adafruit_TCA8418 s_kb;
static bool s_ready   = false;
static bool s_sym     = false;   // held modifier states
static bool s_caps    = false;

// ── Hold-to-repeat (native, uses the TCA8418's real release events) ───────────
// Typing-comfortable defaults; gm overrides with a short delay/rate for smooth
// held movement (see tpagerKeyboardSetRepeat).
static constexpr uint16_t KB_REPEAT_DELAY_DEFAULT = 420;
static constexpr uint16_t KB_REPEAT_RATE_DEFAULT  = 55;
static uint16_t s_repeatDelay = KB_REPEAT_DELAY_DEFAULT;
static uint16_t s_repeatRate  = KB_REPEAT_RATE_DEFAULT;

// Small set of simultaneously-held keys so repeats interleave (diagonals in gm).
static constexpr uint8_t KB_MAX_HELD = 6;
struct HeldKey { uint8_t raw; char ch; uint32_t start; uint32_t last; bool active; };
static HeldKey s_held[KB_MAX_HELD] = {};

static void heldAdd(uint8_t raw, char ch, uint32_t now) {
    for (auto& h : s_held) if (h.active && h.raw == raw) { h.start = h.last = now; return; }
    for (auto& h : s_held) if (!h.active) { h = { raw, ch, now, now, true }; return; }
    // Table full (>6 keys down): drop the oldest so the newest still repeats.
    HeldKey* oldest = &s_held[0];
    for (auto& h : s_held) if (h.start < oldest->start) oldest = &h;
    *oldest = { raw, ch, now, now, true };
}
static void heldRemove(uint8_t raw) {
    for (auto& h : s_held) if (h.active && h.raw == raw) h.active = false;
}
static void heldClear() { for (auto& h : s_held) h.active = false; }

void tpagerKeyboardSetRepeat(uint16_t delayMs, uint16_t rateMs) {
    s_repeatDelay = delayMs;
    s_repeatRate  = rateMs;
    heldClear();   // fresh timing for the new context
}
void tpagerKeyboardResetRepeat() {
    tpagerKeyboardSetRepeat(KB_REPEAT_DELAY_DEFAULT, KB_REPEAT_RATE_DEFAULT);
}

void tpagerKeyboardBegin() {
    // Wire is already begun by boardPowerOn(); the keyboard power/reset rails were
    // released via the XL9555 in boardPowerOn() before this runs.
    if (!s_kb.begin(TPAGER_I2C_ADDR_TCA8418, &Wire)) { s_ready = false; return; }
    s_kb.matrix(KB_ROWS, KB_COLS);
    s_kb.flush();
    s_ready = true;
}

bool tpagerSymHeld() { return s_sym; }

static char resolveChar(uint8_t k) {
    uint8_t row = k / 10, col = k % 10;
    if (row >= KB_ROWS || col >= KB_COLS) return '\0';
    char v = s_sym ? symbolmap[row][col] : keymap[row][col];
    if (!s_sym && s_caps && v != '\0') v = (char)toupper(v);
    return v;
}

char tpagerKeyboardRead() {
    if (!s_ready) return 0;
    uint32_t now = millis();

    // 1) Drain one physical event (press/release), if any.
    if (s_kb.available() > 0) {
        int ev = s_kb.getEvent();
        if (ev != 0) {
            bool    pressed = (ev & 0x80) != 0;   // MSB = pressed
            uint8_t k       = (uint8_t)(ev & 0x7F);
            if (k != 0) {
                k--;                               // raw is 1-based → 0-based (LilyGo)

                // Held modifiers — track both edges, never emit / never repeat.
                if (k == KV_SYM)  { s_sym  = pressed; return 0; }
                if (k == KV_CAPS) { s_caps = pressed; return 0; }

                // Release: stop repeating that key.
                if (!pressed) { heldRemove(k); return 0; }

                // Press: resolve the char, arm hold-repeat, emit once now.
                char v = (k == KV_SPACE) ? ' '
                       : (k == KV_BKSP)  ? '\b'
                       :                   resolveChar(k);
                if (v) { heldAdd(k, v, now); return v; }
                return 0;                          // unmapped cell
            }
        }
    }

    // 2) No physical event: emit the first held key that is due to repeat. Scanning
    //    in order interleaves multiple held keys across successive polls, so a
    //    diagonal (e.g. W+D) keeps both refreshed.
    for (auto& h : s_held) {
        if (!h.active) continue;
        if (now - h.start >= s_repeatDelay && now - h.last >= s_repeatRate) {
            h.last = now;
            return h.ch;
        }
    }
    return 0;
}

// ── `test keymap` diagnostic ────────────────────────────────────────────────
static const char* specialName(uint8_t k) {
    switch (k) {
        case KV_SYM:   return "SYM";
        case KV_CAPS:  return "CAPS";
        case KV_SPACE: return "SPACE";
        case KV_BKSP:  return "BKSP";
        default:       return "";
    }
}

void tpagerKeymapTest() {
    if (!s_ready) tpagerKeyboardBegin();

    displayManager.clearScreen();
    displayManager.printText("[KEYMAP TEST]  encoder-click = exit", 4, outputY, TFT_CYAN);
    displayManager.printText("Press each key; read me the codes.", 4, outputY + LINE_HEIGHT, 0x7BEF);
    displayManager.printText("Check SPACE + BACKSPACE especially.", 4, outputY + LINE_HEIGHT * 2, 0x7BEF);

    const int yv = outputY + LINE_HEIGHT * 4;   // value area
    char line[96];

    for (;;) {
        // Keep the power-save/screen-off timers advancing and the backlight awake
        // while the user maps keys — this loop reads the TCA8418 directly and never
        // calls getKeyboardInput() (which would steal the raw events), so pump the
        // power manager here instead.
        PowerSaveManager::getInstance().update();
        PowerSaveManager::getInstance().updateActivity();

        if (inputHandler.getTrackballEvent() == TBALL_CLICK) break;

        if (s_kb.available() > 0) {
            int ev = s_kb.getEvent();
            if (ev != 0) {
                bool    pressed = (ev & 0x80) != 0;
                uint8_t raw     = (uint8_t)(ev & 0x7F);
                int     adj     = (int)raw - 1;                 // value used for lookup
                int     row     = (adj >= 0) ? adj / 10 : -1;
                int     col     = (adj >= 0) ? adj % 10 : -1;
                char    base    = (row >= 0 && row < KB_ROWS && col < KB_COLS) ? keymap[row][col]    : '?';
                char    sym     = (row >= 0 && row < KB_ROWS && col < KB_COLS) ? symbolmap[row][col] : '?';

                displayManager.fillRect(0, yv, SCREEN_WIDTH, LINE_HEIGHT * 4, TFT_BLACK);
                snprintf(line, sizeof(line), "raw:0x%02X  adj:%d  %s  %s",
                         raw, adj, pressed ? "PRESS" : "rel", specialName((uint8_t)adj));
                displayManager.printText(line, 4, yv, pressed ? TFT_GREEN : 0x7BEF);

                snprintf(line, sizeof(line), "row:%d col:%d", row, col);
                displayManager.printText(line, 4, yv + LINE_HEIGHT, TFT_YELLOW);

                snprintf(line, sizeof(line), "base:'%c'(0x%02X)  sym:'%c'(0x%02X)",
                         base ? base : ' ', (uint8_t)base, sym ? sym : ' ', (uint8_t)sym);
                displayManager.printText(line, 4, yv + LINE_HEIGHT * 2, TFT_WHITE);
            }
        }
        delay(5);
    }

    displayManager.clearScreen();
    displayManager.printCommandScreen();
}

#endif // BOARD_TPAGER
