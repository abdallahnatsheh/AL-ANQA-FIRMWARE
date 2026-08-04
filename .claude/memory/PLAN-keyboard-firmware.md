# Plan — Custom AL-ANQA keyboard firmware (ESP32-C3) + host integration

> Status: approved, in progress. The paused Network MITM suite plan lives alongside
> at `docs/plans/network-mitm-suite.md`.

## Context
The T-Deck keyboard is a separate ESP32-C3 MCU that scans a 5×7 matrix and reports to
the main ESP32-S3 over I2C `0x55`. The stock firmware sends **one byte on press** and
nothing else — so AL-ANQA has **no key-up/down, no hold/long-press, and no real modifier
state** (it reverse-engineers Alt/Sym from single-byte codes, and fakes backspace-repeat
with host-side timing). Goal: fork a better C3 firmware, add a **long-press event exposed
generically to AL-ANQA apps**, plus real key-up/down + Alt/Ctrl/Shift/Sym reporting, and
adapt the host input path — without breaking the hundreds of existing `getKeyboardInput()`
call sites.

Decisions (confirmed): **full custom firmware**, **long-press = generic app event**,
**base = fork `hreikin/tdeck-keyboard` (MIT)**, **user can flash the C3**.

**Hard constraint (load-bearing):** the new firmware MUST reproduce the exact byte codes
AL-ANQA already depends on, or the whole UI breaks: Enter `0x0D`, Backspace `0x08`,
autocomplete `0x27` (Sym+K, `KEY_AUTOCOMPLETE` in `core/input/input_handling.h`), panic
`@`=`0x40` (Sym+P, `ucPanicKey()`), plain ASCII, Space `0x20`, and `q` for quit. Keymap
fidelity is the make-or-break item and must be verified on hardware.

## Why hreikin is the base
`hreikin/tdeck-keyboard` (MIT, Arduino/ESP32-C3) already has the hard parts:
- 4-state machine `NOT_PRESSED → PRESSED → HELD → RELEASED` (`keyStates[][]`,
  `keyPressed()/keyHeld()/keyReleased()`), debounce, and repeat timing (`keyRepeatStart`).
- Sends a **7-byte struct** `{key_value, alt, ctrl, shift, sym, mic, speaker}` — already
  carries modifiers; room to add an event field.
So long-press is nearly free (flag the existing `HELD` past a threshold), and modifiers
are already tracked. Cost: its keymap/protocol differ from stock → keymap re-alignment +
a host-side parser rewrite.

## Part A — C3 firmware (forked, in-repo)
New top-level dir **`keyboard/`** in the AL-ANQA repo holding the forked C3 firmware (its own
Arduino/ESP32-C3 build, NOT part of the S3 PlatformIO build). Retain hreikin's MIT LICENSE +
a MODIFICATIONS note; add a README with build + **flash procedure** (C3 USB/UART + boot pads)
and how to **reflash stock as recovery**.

Changes to the fork:
1. **Keymap fidelity:** tune `defaultKeymap`/`symbolKeymap1..3` (`keyboard.hpp/keys.hpp`) so
   emitted `key_value` bytes match AL-ANQA's expectations above (esp. Sym+K→`0x27`, Sym+P→`0x40`,
   Enter/Backspace/Space, a–z/0–9). Derive from the stock behaviour AL-ANQA consumes today; verify per-key on HW.
2. **Long-press:** add `LONGPRESS_DELAY` (~500 ms). When a key is `HELD` beyond it, emit a
   **one-shot** LONG event (once per hold). Existing REPEAT continues independently.
3. **Versioned, self-describing I2C packet** (replaces the 7-byte ad-hoc struct). Fixed
   **4 bytes**: `[event, key_value, mods, version]`
   - `event`: 0 NONE · 1 DOWN · 2 REPEAT · 3 LONG_PRESS · 4 UP
   - `key_value`: the AL-ANQA-compatible code (0x00 = none)
   - `mods`: bitmask (b0 alt, b1 ctrl, b2 shift, b3 sym, b4 caps, b5 altLock, b6 ctrlLock)
   - `version`: constant sentinel (e.g. `0x01`) so the host can detect new-vs-legacy protocol.
   Emit DOWN+REPEAT+LONG+UP as they occur (`onRequest()` returns the latest, `NONE` when idle).

## Part B — AL-ANQA host integration (`core/input/input_handling.{h,cpp}`)

### Firmware identification (must-have)
The host must **detect which keyboard firmware is present** and only enable the extended
features on the AL-ANQA keyboard; on any other firmware it stays **byte-for-byte identical to
today's behavior**. Mechanism:
- The AL-ANQA C3 firmware's 4-byte packet **always carries the `version` sentinel byte, even
  when idle** (`[NONE,0,mods,VERSION]`) — so the version byte is a persistent fingerprint,
  not just present on a keypress. (Hardening option: also answer a dedicated I2C "identify"
  query with a fixed magic like `"TRX1"`; stock firmware won't.)
- `InputHandling::begin()` runs a **one-time detection probe**: read the packet; if it returns
  4 bytes with `version == KB_PROTO_VERSION` → set `_extendedKbd = true`, else `false`.
- Expose `bool hasExtendedKeyboard() const`. Every extended feature (`consumeLongPress()`,
  `getModifiers()`, key-up/down events) is **gated on `_extendedKbd`**; apps must check it first.
- If detection is momentarily idle/ambiguous at boot, re-latch `_extendedKbd=true` the first time
  a valid versioned packet is seen (never downgrade back to legacy mid-session once confirmed).

### Parsing
1. **`getKeyboardInput()`**: when `_extendedKbd`, `Wire.requestFrom(0x55, 4)` + parse the packet;
   otherwise use the **unchanged legacy single-byte path** (`requestFrom(0x55, 1)`, today's code
   verbatim, incl. the backspace-repeat hack). → AL-ANQA works on BOTH stock and AL-ANQA keyboards;
   flash order doesn't matter and the host change can land first.
2. **Preserve the `char getKeyboardInput()` contract:** on DOWN (and REPEAT) return the primary
   char exactly as today → every existing call site is unchanged. UP/LONG return `0` from this
   method (they don't inject phantom chars). Panic-key + autocomplete + `LockScreenManager::intercept`
   logic stays as-is (still operates on the returned char).
3. **New opt-in event API** (additive, mirrors the trackball/touch polling pattern —
   `getTrackballEvent()` / `TouchManager::poll()`): expose
   `struct KeyEvent { char key; uint8_t event; uint8_t mods; }` + `KeyEvent getLastKeyEvent()`
   and a convenience `bool consumeLongPress(char& key)` + `uint8_t getModifiers()`. Apps that
   want long-press/modifiers poll these; nothing else changes.
4. **Retire the backspace-repeat hack** (`_repeatKey`/`_repeatStart`/`kRepeatDelayMs`) in
   new-protocol mode — firmware now sends real REPEAT events; keep the hack only on the legacy path.

## Cross-cutting
- **NOTICES:** add `hreikin/tdeck-keyboard` (MIT) as the keyboard-firmware base + credit.
- **Docs:** `docs/keyboard.md` (new protocol, long-press event, modifier reporting, flash guide);
  CLAUDE.md (I2C protocol section + `keyboard/` dir + input_handling changes).
- **Memory:** progress_log entry (built / flashed / HW-status).
- **No S3 build impact** unless input_handling changes; CI still builds both S3 envs on push.

## Verification (hardware — requires flashing the C3)
1. Flash the forked C3 firmware (keep a stock backup to revert).
2. **Keymap fidelity (critical):** type the full layout — confirm a–z/0–9/Space/Enter/Backspace,
   Sym+K→autocomplete, Sym+P→panic `@`, and `q`-quit all still behave. Any mismatch = keymap fix.
3. **Legacy fallback:** temporarily reflash stock → confirm AL-ANQA still works via the 1-byte path.
4. **Long-press:** with a temporary debug consumer (or one demo hook), confirm `consumeLongPress()`
   fires ~500 ms into a hold, exactly once, and resets on UP.
5. **Modifiers:** confirm Alt/Ctrl/Shift/Sym report correctly via `getModifiers()`.
6. **Repeat:** confirm firmware-driven key repeat (e.g. backspace) still works.
7. Build gate: `pio run -e T-Deck` for the host change; CI on push.

## Scope / non-goals
- v1 exposes the long-press **event + modifier API**; wiring specific apps to use long-press is a
  follow-on (the generic event is the deliverable).
- Backlight/mic/speaker side keys: carry through as before (optional in `mods`/side-channel), not a v1 focus.
- Keymap is derived to match today's AL-ANQA codes; a redesigned layout is out of scope.
