---
name: project_undercover_home_launcher
description: LOW-PRIORITY idea — a BlackBerry-style home-launcher undercover UI (richer than the Notes-only cover)
metadata:
  type: project
---

**LOW PRIORITY** — a "someday" polish idea, NOT ahead of the isoscan smart/auto work
([[progress_log]] RESUME-HERE) or anything the user is actively testing. Captured
2026-07-05 from a mockup the user liked.

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
  as-is → typing the phrase from the home drops to the T-REX CLI.

## Build shape (when/if picked up)
New `core/system/undercover/home_ui.cpp/h` next to `notes_ui.cpp`; `undercover` could
offer a cover-style choice (notes vs home). Start with a static home screen to flash +
eyeball on real glass, then add tap routing + the 1–2 real sub-screens.
