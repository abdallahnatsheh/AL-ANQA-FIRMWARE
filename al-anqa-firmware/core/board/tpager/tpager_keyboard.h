/**
 * @file   tpager_keyboard.h
 * @brief  T-Lora Pager keyboard (TCA8418 matrix scanner) → ASCII char contract.
 *
 * Reproduces the single-byte-per-press contract the T-Deck's I2C keyboard
 * coprocessor provided, so InputHandling::getKeyboardInput() consumes it the same
 * way (backspace hold-repeat, panic key, autocomplete, lockscreen intercept all
 * live unchanged in input_handling.cpp).
 *
 * Scan layer = Adafruit_TCA8418. The keymap engine (base/symbol layers, modifier
 * handling) is a faithful port of LilyGoLib's LilyGoKeyboard::update() with ONE
 * deliberate change: Sym/Caps/Alt are HELD modifiers (active between press and
 * release) rather than toggles — matching the T-Deck's momentary Sym-as-shift and
 * enabling "hold Sym + rotate encoder = LEFT/RIGHT" (the locked nav decision).
 *
 * Autocomplete: apostrophe (0x27, KEY_AUTOCOMPLETE) is Sym+J on the symbol layer,
 * so no remap is needed.
 *
 * Only compiled/#included on the T-Pager (guarded), so Adafruit_TCA8418 is not
 * pulled into the T-Deck builds.
 */
#pragma once

#include <Arduino.h>

// Bring up the TCA8418 (I2C 0x34, 4x10 matrix). Safe to call once from begin().
void tpagerKeyboardBegin();

// Poll one key event. Returns the resolved ASCII char on a key PRESS, or 0 for
// no event / a release / a modifier key. Modifier state is updated internally.
char tpagerKeyboardRead();

// True while the Sym (symbol/Fn) key is physically held — used by the encoder
// adapter to map rotation to LEFT/RIGHT instead of UP/DOWN.
bool tpagerSymHeld();

// Hold-to-repeat: after `delayMs` of holding a key, tpagerKeyboardRead() re-emits
// its char every `rateMs` until it is physically released. Multiple keys can be
// held at once (repeats interleave — needed for diagonals in `gm`). Backspace and
// every printable key repeat; modifiers never do. Tune per context: the typing
// default is comfortable; `gm` sets a short delay/rate for smooth held movement.
void tpagerKeyboardSetRepeat(uint16_t delayMs, uint16_t rateMs);

// Restore the typing-comfortable default repeat (call on exit from a game etc.).
void tpagerKeyboardResetRepeat();

// `test keymap` diagnostic: full-screen echo of raw TCA8418 events (raw code,
// press/release, adjusted code, row/col, base+symbol chars) so the physical
// keymap — especially the space and backspace keys — can be verified/finalized on
// hardware. Exits on encoder click.
void tpagerKeymapTest();
