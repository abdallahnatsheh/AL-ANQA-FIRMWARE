---
title: Home Launcher
parent: System
nav_order: 11
---

# Home Launcher

## `home` / `hm` — Phone-style home-screen disguise  `[EXP]`

A modern phone **home launcher** used as an undercover disguise — an alternative
to the Notes-only cover. At a glance it reads as an ordinary smartphone home
screen; only the **Notes** tile opens anything real.

```
CMD> home        # or: hm
```

---

## Layout

- **Status bar** — real clock (`HH:MM`), a fake carrier ("CRIMSON MOBILE"), signal
  bars, and the **real battery** level (green when charging, red at ≤15%).
- **Clock / weather hero** — large live time + date (via `ClockManager`) and the
  current **weather** (via [`weather` / `wx`](weather) — sun/cloud/rain/snow/thunder
  icon + temperature + condition). Shows a muted `--` until weather is configured.
- **4×2 app grid** — Phone · Messages · Email · Browser · Music · **Notes** ·
  Calendar · Settings, with unread badges on a couple of tiles.

---

## Using it

| Input | Action |
|-------|--------|
| Tap a tile | **Notes** opens the real notes app (see [Notes](../system#undercover-mode)); every other tile shows a "No service" toast and opens nothing |
| Trackball U/D/L/R | Move the selection ring between tiles |
| Trackball click | Launch the selected tile |
| Back chevron **‹** in the notes list | Tap it (top-left of the notes appbar) → returns **here**, to Home. Only shown when Notes was opened from the launcher. |
| `q` **inside the notes list** | Same as the chevron — returns to Home (keyboard shortcut). Works even with a passphrase set. In a note, tap the detail back chevron to reach the list first. |
| Type the passphrase | Drops silently to the CLI (when an undercover passphrase is set) |
| `q` **at the home screen** | Exits to the CLI — **only** when no passphrase is configured |

The launcher inherits **all** the undercover plumbing: `displayManager.setBlocked`
status-bar suppression, dim→wake repaint, lock-screen stand-down, touch double-tap
wake, and the secret-passphrase rolling-buffer exit.

---

## Live data

- **Clock** and **battery** are always real (no configuration needed).
- **Weather** is fetched **on entry** if WiFi is connected and the cached reading is
  stale. Configure it once with [`weather` / `wx`](weather) (or by editing
  `/config/weather.conf`). Weather cannot be known offline — it is live server data.

---

## Notes

- This is the standalone launcher for tuning the look. It is **not yet** offered as
  an `undercover` cover-style choice (Notes vs Home) — that is planned.
- App icons are drawn as vector primitives (no icon font), so they render on both
  board variants without extra assets.

See also: [Notes / Undercover](system#undercover-mode), [Weather](weather).
