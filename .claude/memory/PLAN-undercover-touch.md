# PLAN — Undercover Mode + Touchscreen Activation

> **STATUS 2026-07-01: Phase 0 ✅ + Phase 2 UI ✅ (committed+pushed b273edd) + Phase 1a built (uncommitted).**
> Phase 1a = `g_covert` flag (`covert.h`/`undercover.cpp`) + `undercover`/`uc` cmd (blocking silent cover) +
> the sound-leak audit: `NotificationManager::notify()` and `hidden_ssid` beep both no-op when covert (that
> ONE notify() gate silences wguard/macwatch/espchat too — all route through it). Visual tells already
> covered by the Notes UI's `setBlocked(true)`. **Phase 1 REMAINING:** panic-chord entry (needs the
> non-blocking `UndercoverManager` intercept model → refactor `runNotesUi` into stateful begin/handleEvent/
> end so a chord in `getKeyboardInput` can drop to cover mid-command); the deliberate `uc` blocking entry is
> a stopgap. Then rest of Phase 2 (SD notes + secret-passphrase exit, replacing the `q` tell) + Phase 3.
>
> ---
> **Phase 0 ✅ HW-VERIFIED & WORKING.** `test touch` on real hardware:
> GT911 detected at 0x5D, crosshair tracks the finger 1:1 into all four corners (orientation
> correct — the vendor `setSwapXY`/`setMirrorXY` config was right, no flip needed), tap/
> long-press/drag classify correctly, keyboard+trackball unaffected on the shared I2C bus.
> Abdallah: "all works amazing and fast and all good." **Phase 0 DONE.**
>
> **STATUS 2026-07-01: Phase 2 UI (NOTES COVER) — first pass built & compiles clean, awaiting
> HW feedback.** Per Abdallah's reorder ("build the ui as a test first, not full feature like
> duress password, just simple ui"), the Notes cover UI was built AHEAD of Phase 1, as a
> standalone TEST — no undercover machinery. Module `core/system/undercover/notes_ui.{h,cpp}`,
> command **`notes`/`nt`** ([EXP], System). Renders the mockup (`~/Downloads/trex-undercover-notes.html`,
> real px = mockup/2): fake status chrome (clock/signal/battery), Notes list (appbar + "A" avatar +
> search pill + Pinned/Recent section labels + tinted rounded cards) and note detail (back bar +
> title + paragraph/checklist body). Draws straight to global `tft` with anti-aliased
> **Noto Sans VLW smooth fonts** (Abdallah wanted "modern like an Android app" — done: 4 sizes baked
> by `convert_font.py`→`notes_fonts.h`, held as persistent `lgfx::VLWfont`; the plan's VLW smooth-font
> step is now COMPLETE, not deferred). `displayManager.setBlocked(true)` suppresses the real T-REX status bar (lock-screen
> pattern). Nav: touch tap card→detail / tap back→list / tap +FAB→new / drag-scroll; trackball
> up/down select + click open/back + scroll; `q` quits. 6 hardcoded sample notes. **Explicitly NOT
> included** (deferred to full Phase 2/3): SD `/notes/*.txt`, secret-passphrase exit, duress/decoy,
> `g_covert` wiring, boot-cover. Compiles clean both envs (Flash +~19KB → 39.2% Plus).
> **Next: flash `notes`, get Abdallah's visual/UX feedback, iterate the layout (font sizes, card
> geometry, colours), THEN do Phase 1 (g_covert leak audit) + wire the real exit.**
>
> ---
> (Original Phase-0 detail retained below.) `TouchManager`
> (`core/input/touch/`) + `test touch` diagnostic (`diagnostics/touch/`, folded into the
> existing `test`/`tst` dispatcher — no new `tt` command, keeps the 64-cmd cap headroom)
> are written and wired into `main.ino`. Deviations from the spec below:
> (1) diagnostic is `test touch`, not a standalone `tt` command;
> (2) **driver switched mid-session from `mmMicky/TouchLib` to `lewisxhe/SensorLib`'s
> `TouchDrvGT911`** after Abdallah linked LilyGo's official T-Deck example
> (`Xinyuan-LilyGO/T-Deck examples/Touchpad/Touchpad.ino`) — that example uses
> SensorLib, not TouchLib. Two concrete wins: `TouchLibGT911::init()` was found to
> unconditionally `return true` regardless of I2C ACK (checked the vendored source —
> would have broken the 0x5D->0x14 fallback and "no panel" detection); SensorLib's
> auto-probe verifies the real product-ID register (`==911`). And the coordinate
> mapping is no longer a guess — LilyGo's example calls `setMaxCoordinates(320,240)` +
> `setSwapXY(true)` + `setMirrorXY(false,true)` for this exact board, copied verbatim;
> `TouchManager::poll()` reads already-mapped coordinates straight from the driver.
> (3) **SensorLib is a registry `lib_deps` dep** (`lewisxhe/SensorLib @ ^0.4.1`), NOT
> vendored under `lib/` — Abdallah installed 0.4.1 via PlatformIO (`.pio/libdeps/`).
> Left as a registry pin like AceButton/NimBLE (large multi-driver lib, no point
> committing all the unused sensor code). `lib/TouchLib` (now unused) was removed.
> (4) **Version gotcha handled:** the LilyGo example ships SensorLib 0.2.x (header-only
> `.tpp`), but 0.4.1 was refactored (`.cpp/.hpp` split, new `getTouchPoints()` API).
> Verified the code against the *installed* 0.4.1 headers/example, not the 0.2.x one:
> `setPins`/`begin(Wire,addr,sda,scl)`/`setMaxCoordinates`/`setSwapXY`/`setMirrorXY`/
> `isPressed` are unchanged, but the old `getPoint(int16_t*,int16_t*,n)` is now
> `__attribute__((deprecated))` → migrated `poll()` to `getTouchPoints()` →
> `TouchPoints`/`TouchPoint{x,y}` (`hasPoints()`/`getPoint(0)`) to avoid warning spam.
> (5) **COMPILED CLEAN** (both `T-Deck` + `T-Deck-Plus` envs, on Abdallah's request) —
> zero warnings, zero errors. The warning he hit was `#pragma message: TouchDrvGT911.hpp
> is deprecated. Include TouchDrv.hpp instead` — SensorLib 0.4.x deprecated the
> per-driver top-level headers for the umbrella `TouchDrv.hpp`. Fixed: `touch_manager.cpp`
> now `#include "TouchDrv.hpp"` (still pulls in TouchDrvGT911 + TouchPoints). Firmware
> RAM 65.2% / Flash 38.9% on Plus.
> **Next action: flash + run `test touch`, confirm the dot tracks 1:1 into all four
> corner brackets. Should just work — mapping is the vendor's own config. If somehow
> off, the fix is `TouchManager::begin()`'s three `s_touch.set*` calls. Then Phase 1
> (`g_covert` flag + `UndercoverManager` glance cover).**

> Hand-off spec for Claude Code. Implements a modern phone-style **Notes cover UI**
> (undercover/opsec mode, à la `kali-undercover`) and **activates the GT911 touchscreen**,
> which the project has never used. Educational / private-lab firmware.
>
> Read `CLAUDE.md` first. Honor every existing rule: GDMA WiFi↔SD ordering, all output via
> `displayManager`, poll `q` in blocking loops, one-liner command registration, module = own
> `.cpp/.h` pair, `snprintf` not `sprintf`, no `Serial.println` of secrets.

---

## 0. Goal & shape

Two deliverables that depend on each other:

1. **Touchscreen** — a project-wide `TouchManager` that yields tap/drag/long-press events the
   same way `getTrackballEvent()` yields trackball events. Foundational; the cover UI consumes it.
2. **Undercover mode** — a `UndercoverManager` singleton that disguises the device as an
   ordinary Notes app. Built in three layered phases (glance → handling → duress) so each ships
   and is testable on its own.

Architecture reuse: **`UndercoverManager` mirrors `LockScreenManager` almost exactly** —
same input-intercept points, same `displayManager.setBlocked(true)` trick (buddy/wguard
direct-`tft` writes already honor `isBlocked()`), same `_justUnlocked` redraw plumbing on exit.
Disguise (undercover) and access-control (lockscreen) are different jobs and **stack**.

The spine of the whole feature is **one global flag, `g_covert`**. Wire it everywhere in Phase 1;
everything else hangs off it.

---

## PHASE 0 — Touchscreen activation  `core/input/touch/`

### Hardware (verified from `utilities.h` / `LGFX_T-Deck.h`)
- Controller: **GT911** capacitive, on the **shared** I2C bus `SDA=18 / SCL=8`.
- `BOARD_TOUCH_INT = 16`. No dedicated reset GPIO is broken out.
- I2C addr: **`0x5D` default**, `0x14` fallback. **Do not hard-code** — run the existing
  `i2cscan` (`i2c`) command on real hardware first and confirm which address answers.
- The keyboard (`0x55`) already owns `Wire` on the same pins via `input_handling`. **Touch MUST
  reuse the existing `Wire` instance** — never call `Wire.begin()` again with different pins,
  never use `Wire1`.

### Approach — primary: TouchLib (already a dep), shares the live Wire
`bxparks`… no — `mmMicky/TouchLib` is already in `platformio.ini` and is the known-good GT911
driver for this board. It accepts the existing `Wire`, so it sidesteps the bus-ownership fight.

```cpp
// touch_manager.h  (singleton, mirrors InputHandling's event style)
struct TouchEvent {
    enum Type { NONE, TAP, LONG_PRESS, DRAG_START, DRAG_MOVE, DRAG_END } type = NONE;
    int16_t x = 0, y = 0;        // already mapped to 320×240 landscape
    int16_t dx = 0, dy = 0;      // for DRAG_MOVE
};
class TouchManager {
public:
    static TouchManager& instance();
    void begin();                 // assumes Wire already begun by input_handling
    TouchEvent poll();            // call once per loop; NONE when idle
    bool isPresent() const;       // false if GT911 never probed → touch features no-op
private:
    // GT911 native coords are portrait; rotate/mirror to match displayManager rotation.
    // long-press = held > 600 ms without movement; tap = press+release < 250 ms, < 8 px travel.
};
```

- **Coordinate mapping is the fiddly part.** GT911 reports in its own orientation; the display
  runs landscape 320×240. Map in `poll()` to match `displayManager`'s rotation. Capacitive →
  **no point-calibration needed**, just axis swap/invert. Verify empirically (see test command);
  if taps land mirrored, flip via config, don't guess.
- Probe once in `begin()`. If GT911 doesn't ACK, set `isPresent()=false` and make every touch
  consumer degrade gracefully (keyboard/trackball still drive everything). Touch is **additive,
  never required**.

**Alternative (if you prefer tighter display integration):** add `lgfx::Touch_GT911` to the
`LGFX` class in `LGFX_T-Deck.h` and use `tft.getTouch(&x,&y)` — LovyanGFX then handles rotation
automatically. Tradeoff: it wants to manage its own I2C, so you must reconcile bus ownership with
the keyboard. Only take this path if the shared-`Wire` coordination in TouchLib proves messy.

### Wiring it in
- `main.ino setup()`: `TouchManager::instance().begin();` **after** `inputHandler.begin()` (Wire
  must exist first) and after `BOARD_POWERON` is HIGH.
- `main.ino loop()`: add `TouchEvent te = TouchManager::instance().poll();` then route it through
  the intercept chain **before** dispatch:
  ```cpp
  te = LockScreenManager::getInstance().interceptTouch(te);   // NEW — lock swallows touch
  te = UndercoverManager::instance().interceptTouch(te);      // NEW — cover consumes touch
  commandManager.processTouch(te);                            // NEW — apps that opt in
  ```
- **Critical:** `LockScreenManager` and `UndercoverManager` must each swallow touch while active,
  or a user taps straight through the lock/cover. Add `interceptTouch()` to both, same pattern as
  their existing `interceptTrackball()`.
- `PowerSaveManager`: a touch must count as activity (wake + reset dim timer), like a keypress.

### Diagnostic + man page
- Add `touchtest` / `tt` under **Diagnostics** (sibling to `spktest`, `gps test`): full-screen
  crosshair that follows the touch point, prints raw + mapped coords, shows tap/long-press/drag
  classification, `q` to quit. This is how rotation mapping gets verified on hardware.
- Add a `man` page entry for `tt`.

### Phase 0 acceptance
- [ ] `i2c` lists the GT911; address confirmed and noted in `CLAUDE.md`.
- [ ] `tt` shows the dot tracking the finger 1:1 across all four corners (no mirror/swap).
- [ ] Keyboard + trackball still work with touch active (shared bus not disturbed).
- [ ] Touch counts as power-save activity.
- [ ] GT911 absent → `isPresent()` false, firmware boots and runs normally.

---

## PHASE 1 — Glance cover  `core/system/undercover/`

The "across the table" tier. Full-screen benign UI + total silence + status bar replaced.

### `UndercoverManager` (singleton, models LockScreenManager)
- `enter()`:
  - raise **`g_covert = true`** (global, declared once, e.g. `core/system/covert.h`),
  - `displayManager.setBlocked(true)` (suppresses buddy/wguard/pet direct-`tft` writes),
  - freeze TX ops (Phase 3 fleshes this out; for now just stop foreground draws),
  - render the cover (Phase 2 fills the Notes UI; Phase 1 may stub a static screen).
- `interceptKey(k)` / `interceptTrackball(e)` / `interceptTouch(e)` — swallow all input except the
  secret-exit watcher, exactly like the lock screen swallows input.
- `exitReveal()`: `g_covert=false`, `setBlocked(false)`, set `_justUnlocked=true`, `clearScreen`,
  `printCommandScreen`. Reuse the lockscreen's redraw contract so interactive apps repaint.

### The `g_covert` flag — wire it EVERYWHERE (this is Phase 1's real work)
Every audible/visible leak must check it:
- `NotificationManager` — no sounds when covert.
- **Direct I2S tone calls bypass NotificationManager** — `hidden_ssid` two-tone beep, `wguard`
  alert tones. Gate each on `g_covert`. (Grep for `i2s` / `tone` / `ledcWriteTone`.)
- `wguard` background popup bar + status-bar **shield icon** — suppress draw when covert.
- Status-bar renderer — draw cover chrome (fake clock + battery) instead of the real bar.
- Any LED / vibration indicators.

### Entry triggers
- `uc` / `undercover` command (**System** category) — deliberate toggle.
- **Panic chord** caught inside `getKeyboardInput()` the same way `KEY_AUTOCOMPLETE` (Sym+K,
  `0x27`) already is — choose a second Sym+key so it fires mid-command, one-handed, hard to hit by
  accident. By the time you could *type* `undercover`, the prompt's already been seen — the chord
  is the real entry path.

### Phase 1 acceptance
- [ ] `uc` and the panic chord both drop instantly to the cover from any screen, even mid-scan.
- [ ] With `wguard bg` running, zero shield icon / popup / sound leaks through the cover.
- [ ] Trigger a `hiddenssid` find under cover → no beep.
- [ ] Real status bar (GPS dot, battery, shield) is fully replaced.

---

## PHASE 2 — Handling  (the Notes UI)  `core/system/undercover/notes_ui.*`

"Someone picks it up and pokes." The cover must be a *functional* notes app.

### Make it real, cheaply
- Notes are **real files** on SD: `/notes/*.txt` (title = first line, preview = next lines).
  Survives reboot, scrolls to mundane content when poked. Ship 4–6 believable decoy notes on
  first run (groceries, packing list, a recipe, "Home WiFi", movies to watch).
- **List view**: app bar "Notes" + search affordance + scrollable note cards (title, 2-line grey
  preview, date) + round amber FAB. Tap a card (touch) or trackball-click → detail.
- **Detail/editor**: **reuse the existing `edit`/`ed` editor**, restyled to the Notes look (off-
  white page, sans font, thin top bar with back chevron). Minimal new code.
- Touch interactions: tap card → open; tap FAB → new note; drag → scroll list; back chevron → list.
  Keyboard/trackball paths must also work (touch is additive).

### The hidden exit lives here
- Watch the editor buffer on each Enter/save for the **secret passphrase**. Match → `exitReveal()`.
- To anyone watching it's just someone typing a memo — that's the whole point. Store the phrase
  hashed: reuse the lockscreen's **SHA-256(salt+phrase)** helper, 8-byte `esp_random()` salt.

### Phase 2 acceptance
- [ ] Cover opens on a real note list from `/notes/`; scrolling/opening/creating works by touch
      AND by keyboard.
- [ ] Typing the passphrase in any note + Enter reveals T-REX; any other text just saves.
- [ ] Looks like the mockup: warm paper, single amber accent, rounded cards, no terminal cues.

---

## PHASE 3 — Duress  (coerced unlock)

"They make you turn it on / unlock it." Concealment over destruction.

- **Boot cover**: `boot_cover=1` → device powers on directly into the Notes app. A seized/borrowed
  device looks like a notepad from cold boot. Pairs with the lockscreen.
- **Dual passphrase** (VeraCrypt-style hidden volume):
  - *real* phrase → reveal T-REX.
  - *decoy* phrase → "unlocks" to a clean, working notepad with believable content and **no
    firmware visible**. Forced-unlock yields a boring memo app, not a refusal.
- **Running-ops policy on entry = "passive keeps running silent"** (your choice):
  - **Keep running, fully silent:** passive RX-only tools — `wguard`, `bmon`, `espsniff`,
    *passive* `csidetect`, `trackme` in passive mode. They emit nothing and keep logging to SD →
    covert monitoring is the payoff.
  - **Freeze on entry (always):** anything that transmits — `deauth`, `eviltwin`, `beaconflood`,
    `karma`, `pmkid` TX, `fastpair`/`blespam`, `badusb`, active `wardrive`. A "notepad" radiating
    deauth is an RF tell.
  - **Force passive variants:** `trackme` / `csidetect` active modes send scan-requests/probes —
    under cover, switch them to their passive paths or freeze.
- **Optional panic-wipe — OFF by default, opt-in only.** I'd steer you *away* from this: a wipe is
  irreversible, an empty device is itself suspicious, and destruction can be legally worse than
  concealment. If included, scope it to `/logs/` + creds only, behind an explicit config flag, and
  document the tradeoff in the man page. Concealment (decoy passphrase) is the better default.

### Phase 3 acceptance
- [ ] `boot_cover=1` → cold boot lands in Notes, no T-REX flash.
- [ ] Decoy phrase opens a clean notepad; real phrase reveals firmware; the two are indistinguishable
      to an observer.
- [ ] Under cover: `wguard bg` keeps logging silently; a queued `deauth` is frozen, not transmitting.

---

## Visual spec  (from the approved mockup; real-px = mockup ÷ 2)

**Palette**
```
paper/bg   #F7F5F0   (warm — NOT #FFFFFF, NOT #000)
card       #FFFFFF
ink        #23211C   (warm near-black — pure #000 is the "terminal" tell)
muted      #9A968C   (preview / meta text)
hairline   #ECE9E2
accent     #F4B740   (the ONLY saturated colour — FAB, pinned dot)
tints      #EAF1F7 blue · #EBF3EC green · #F8EEF1 pink  (Keep-style, sparing)
```

**Geometry (real px @ 320×240)**
```
status chrome   17 px tall (fake clock left · signal+battery right)
app-bar title   ~20 px bold "Notes"
card            radius 10, padding 10, gap 6–7, soft shadow + 1px hairline
card title      13–14 px semibold   preview 11–12 px muted   meta 10 px
FAB             44 px circle, amber, bottom-right inset 13
detail top bar  ~32 px, back chevron in accent
```

**Typography — the make-or-break detail.** The default GFX mono font reads retro no matter the
layout. **Bake an anti-aliased sans** (Inter or Roboto) as a LovyanGFX **VLW smooth font** at ~3
sizes (title 14, body 13, meta 10) and embed it. Add a `convert_font.py` build step alongside the
existing `convert_splash.py` so fresh clones build without manual font generation. Card tints +
`fillSmoothRoundRect` are nearly free; spend the effort on the font.

---

## Config — `/undercover.conf`  (key=value, like `/lockscreen.conf`)
```
real_hash    <sha256(salt+realphrase)>
real_salt    <hex>
decoy_hash   <sha256(salt+decoyphrase)>
decoy_salt   <hex>
boot_cover   0|1
persona      notes            # future: calc, clock, game
panic_key    <keycode>
ops_policy   passive_silent   # passive_silent | freeze_all | per_feature
wipe_on_duress 0              # opt-in, off by default
```
`saveConfig()` returns `bool`; on false print the yellow "No SD — active this session only"
warning, same as the lock screen.

---

## Build order & global checklist
1. **Phase 0** touch + `tt` diagnostic — verify on hardware before anything else.
2. **Phase 1** `g_covert` wired everywhere + triggers (the silent-leak audit is the real work).
3. **Phase 2** Notes UI on touch + hidden-exit passphrase.
4. **Phase 3** boot cover + dual passphrase + ops policy.

- [ ] `pio run -e T-Deck` **and** `-e T-Deck-Plus` both compile (CI gate).
- [ ] Command count fits the 64 cap (`uc`, `tt` added — confirm headroom; merge a command if tight).
- [ ] GDMA rule respected: cover never writes SD while promiscuous/APSTA without
      `ScopedPromiscPause`.
- [ ] Touch, keyboard, trackball all drive the cover; none is mandatory.
- [ ] No secret (passphrase, hash) ever hits `Serial`.
- [ ] Update `CLAUDE.md`: GT911 addr/INT, `TouchManager`, `UndercoverManager`, `g_covert`,
      `/undercover.conf`, new commands, and move "touchscreen" out of any pending list.

## Verify on real hardware (can't be unit-tested)
- GT911 address + rotation mapping.
- Shared-I2C stability: keyboard + touch polled together for a long session, no bus lockups.
- `sshcon`-style stress: long covert session with `wguard bg` logging — no GDMA corruption.
- Panic chord can't be triggered by accident during normal typing.
