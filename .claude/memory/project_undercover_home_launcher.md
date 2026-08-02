---
name: project_undercover_home_launcher
description: BlackBerry-style home-launcher undercover UI — BUILT 2026-07-07 as `home`/`hm` (pending HW test)
metadata:
  type: project
---

**BUILT 2026-07-07** (code-complete, NOT yet HW-tested — user compiles/flashes manually).
Command `home`/`hm` [EXP] in System. Files `core/system/undercover/home_ui.{cpp,h}`,
free fn `bool runHomeUi(bool standalone=true)`. This is now command **64/64 — the
array `Command commands[64]` in command_manager.h is FULL**; bump to [128] (~2KB RAM)
before adding any more commands.

## What was built (vs the design below)
- Dark-theme launcher: fake status bar (real HH:MM via `ClockManager` · "CRIMSON MOBILE"
  · signal+battery) + clock/weather hero (live time+date via ClockManager/strftime;
  static sun + "24°") + **4×2 app grid**: Phone/Messages(badge 3)/Email(badge 12)/
  Browser/Music/**Notes**/Calendar/Settings. App icons are **vector primitives**
  (fillRoundRect/fillCircle/fillTriangle/drawLine) — NO icon-font/unicode dependency
  (the baked Noto VLW set is text-only). Accent-colored rounded-square tiles, red
  unread badges, blue selection ring.
- **Only the Notes tile is real** → launches `runNotesUi(false)`. Every other tile is
  cosmetic → shows a "No service" toast, opens nothing.
- **Reuses ALL the Notes cover machinery** (mirrored, not shared — separate TU statics):
  PSRAM `LGFX_Sprite` compositing + `flushCanvas`, the 4 baked Noto VLW fonts
  (`notes_fonts.h`, load/unload), `displayManager.setBlocked(true)`, dim→wake full
  repaint, LockScreen stand-down (`isLocked()` → pump keys only), touch double-tap
  screen-off wake, and the **secret-passphrase rolling buffer** (`s_kbuf`/`ucCheckPhrase`)
  → typing the phrase from the home screen drops to the CLI. `q` = fallback exit only
  when no passphrase set.
- **Notes hand-off design (the load-bearing bit):** refactored `runNotesUi()` →
  `bool runNotesUi(bool standalone=true)` (backward-compat default; `notes`/`undercover`
  callers unchanged). `standalone=false` skips the CLI restore + leaves `setBlocked(true)`
  so Home retakes the screen with NO terminal flash; the bool return = "exited via secret
  passphrase". Home tears down its OWN sprite/fonts before calling Notes (avoids 2×150KB
  PSRAM peak), then on a normal Notes-back rebuilds + repaints home, but on a passphrase
  exit propagates it straight to the CLI. Same standalone/return contract added to
  `runHomeUi`.

## Home is now the undercover BASE (2026-07-07b — user decision)
- `undercover` (`runUndercover`) and boot-cover (`ucInit`) now call **`runHomeUi()`** (was `runNotesUi()`).
  Notes is reachable from the launcher's Notes tile (`runNotesUi(false)`, with the back-to-Home chevron).
- **The standalone `notes`/`nt` command was REMOVED** — Notes has no CLI command anymore, only the home tile.
  `runNotesUi()` stays (called by the tile); notes_ui.cpp/.h stay. `home`/`hm` stays as the standalone tuning
  command. Removing notes → 64/128 cmds. `man notes` still resolves (PAGES[] entry kept).
- Boot-cover now boots straight into the **home** launcher; `uc` status/boot messages + man/docs/README/CLAUDE
  all updated ("Notes disguise" → "home-screen disguise").
- **Charging indicator added** to the home status-bar battery (green body + dark lightning-bolt overlay when
  `isCharging()`); non-charging = proportional fill (red ≤15%). Notes battery still just green-fills when
  charging — mirror the bolt there if wanted.

## KNOWN quirks / TODO (HW test targets)
- Panic-key hook (`getKeyboardInput`) still fires inside **standalone** `home` (g_covert false) — harmless; a
  non-issue for the covert path (g_covert=true skips the hook).
- Icons are minimal primitives; can upgrade to nicer vector art or a baked icon font later.

---
## Original design (captured 2026-07-05 from a mockup the user liked)

## Idea
A fuller, modern **undercover disguise** that looks like a BlackBerry/pro-phone
**home launcher**, as an alternative (or addition) to the current Notes-only cover
(`core/system/undercover/notes_ui.cpp`). At a glance it reads as an ordinary phone.

## Layout that FITS the real 320×240 (an early 4×3-grid + keyboard mockup was too
big — user rejected it; this is the trimmed version):
- **Status bar** (~22px): time · fake carrier ("CRIMSON MOBILE") · bt/wifi/battery.
- **Clock + weather hero** (~70px): big time + date (left, real via `ClockManager`)
  and a temp + weather icon (right, static block).
- **4×2 app-tile grid** (~120px, 8 tiles): rounded-square flat-color icons + label,
  e.g. Phone / Messages(badge 3) / Email(badge 12) / Browser / Music / Notes /
  Calendar / Settings. Unread badges = a red rounded-rect + number.
- Total ≈ 212px of 240 — breathing room. A right-swipe/`→` could page to more apps
  later. NO on-screen keyboard in the mock (that's the physical T-Deck keyboard).

## Why it's cheap — reuses the Notes cover machinery (swap the LOOK, not the plumbing)
- One double-buffered PSRAM `LGFX_Sprite`; icons = `fillRoundRect` + a glyph; labels
  in the baked **Noto VLW smooth fonts** (`notes_fonts.h`). Flat RGB565 colors, no
  gradients. Dark screen `#0b0d11`, tiles one accent each, muted text `#7d828b`.
- Taps via `TouchManager` (icon hit-boxes) + trackball highlight — same pattern as
  `notes_ui`. Most tiles can be cosmetic; wire only 1–2 to tiny real screens (Notes,
  Weather, Clock).
- **Covert entry unchanged + inherited free** (disguise-agnostic): passphrase-exit
  rolling buffer, `displayManager.setBlocked(true)`, boot-cover, panic key all work
  as-is → typing the phrase from the home drops to the AL-ANQA CLI.

## Build shape (when/if picked up)
New `core/system/undercover/home_ui.cpp/h` next to `notes_ui.cpp`; `undercover` could
offer a cover-style choice (notes vs home). Start with a static home screen to flash +
eyeball on real glass, then add tap routing + the 1–2 real sub-screens.
