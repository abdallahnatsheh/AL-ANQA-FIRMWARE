---
title: System
nav_order: 10
has_children: true
---

# System

| Guide | Commands |
|-------|---------|
| [Help & Manual](help-man) | `help` / `hlp` · `man` / `mn` · `show` / `sh` · `clear` · `MATRIX` |
| [NES Emulator](system#game--gm--nes-emulator) | `game` / `gm` |
| [Device Info](info) | `info` / `inf` |
| [Power Save](pwrsave) | `pwrsave` / `psv` · `sleep` / `slp` |
| [Lock Screen](lock) | `lock` / `lk` |
| Undercover Mode | `undercover` / `uc` (home-screen disguise + silent mode) |
| [Home Launcher](home) | `home` / `hm` — the disguise `undercover` uses |
| [Timezone](tz) | `tz` |
| [Weather](weather) | `weather` / `wx` |
| [Audio & Notifications](audio) | `volume` / `vol` · `notif` / `nf` · `test spk` |
| [SD Commands](sd-commands) | `sdinfo` · `sdls` · `cd` · `cat` · `edit` · `rm` · `sdformat` |
| [Diagnostics](diagnostics) | `gps on` · `gps off` · `gps test` · `test spk` · `test mic` · `test lora` |
| [SD Card Layout](sdcard) | File layout reference |
| [Custom Splash Screen](splash) | Replace the boot image with your own PNG |

---

## Trackpad (Trackball)

The physical trackball works at the command prompt at all times — no command needed.

| Action | Result |
|--------|--------|
| Roll left | Move cursor left within the typed command |
| Roll right | Move cursor right within the typed command |
| Click | Execute command (same as Enter) |

You can roll the cursor to any position mid-command and type to insert characters there. Backspace deletes the character before the cursor, just like a normal terminal.

**Download mode** — hold the trackball button while plugging in USB to force the ESP32 into download mode for flashing.

---

## `help` / `hlp` — Command List

```
CMD> help           # browse all commands by category
CMD> help deauth    # detail for a specific command
CMD> hlp da
```

Commands are grouped by category (System, WiFi, Network, Bluetooth, SD Card, Diagnostics). Each category is paginated at 5 commands per sub-page.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

---

## `man` / `mn` — Manual Pages

```
CMD> man deauth
CMD> mn ws
```

Full on-device manual for any command. Covers syntax, steps, keys, options, files, and warnings. Short names work too (`mn da` = `mn deauth`).

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

---

## `show` / `sh` — Re-display Last Scan

```
CMD> show wifi      # last scanwifi result
CMD> show ble       # last scanblue result
CMD> show hosts     # last netdiscover result
CMD> sh wifi
```

Re-renders the last cached scan table without running a new scan. Shows `No scan data` if that scan has not been run yet in this session.

---

## `info` / `inf` — Device Info

```
CMD> info
```

3-page view of device information:

| Page | Content |
|------|---------|
| 1 | Chip model, cores, flash size, PSRAM, CPU frequency |
| 2 | WiFi MAC, BT MAC, IP address, WiFi RSSI |
| 3 | Battery %, SD card status, LoRa pins, GPS status |

Use `l` / `a` to navigate pages, `q` to quit.

---

## `pwrsave` / `psv` — Power Save

```
CMD> psv status                    # show current settings
CMD> psv on                        # enable power save
CMD> psv off                       # disable power save
CMD> psv set dim <seconds>         # inactivity dim timeout (default: 120s)
CMD> psv set screenoff <seconds>   # screen-off timeout (default: 300s)
CMD> psv set screenoffmode on|off  # enable/disable screen-off tier
```

Two-tier inactivity system:

| Tier | Default | Behaviour |
|------|---------|-----------|
| Dim | 2 min | Reduces brightness to save power |
| Screen off | 5 min | Sets brightness to 0 (any keypress restores) |

**Battery-aware dim** — automatically dims when battery drops below threshold, regardless of inactivity timer.

Config is saved to `/config/pwrsave.conf` on the SD card and restored on boot.

---

## `sleep` / `slp` — Deep Sleep

```
CMD> sleep
CMD> slp
```

Puts the ESP32-S3 into **deep sleep** (~240 µA) — a far deeper power state than `pwrsave`'s screen-off, which only blanks the backlight while the CPU keeps running. On `sleep` the device fades the backlight, puts the display panel to sleep, brings WiFi to idle, releases the SPI/I2C buses, and powers the peripherals down.

**Wake:** click the trackball (the center button). Waking is a **full reboot** — the device comes back to a fresh prompt, so any RAM-only state (unsaved scan tables, captured creds not yet flushed, the lock-screen session) is lost.

| | |
|---|---|
| Wake source | Trackball click only (GPIO0) |
| Keyboard wake | **No** — the keyboard INT line (GPIO46) is not an RTC pin, so it cannot wake the chip |
| Trigger | **Manual only** — never fires on a timeout (that's `pwrsave`'s job) |
| Current draw | ~240 µA |

> Before sleeping, exit any running attack/capture tool and let it flush to SD — deep sleep tears down the buses and reboots, so in-flight work is not saved.

---

## Audio (`vol` / `notif`)

→ See [Audio & Notifications](audio.md) for the `vol` and `notif` commands.

---

## `clear` / `clr` — Clear Screen

```
CMD> clear
```

Clears the output area and resets the prompt.

---

## `MATRIX` / `matrix` — Matrix Animation

```
CMD> MATRIX
```

Launches the Matrix digital rain animation. Press `q` to exit.

---

## `game` / `gm` — NES Emulator

```
CMD> gm            # open the ROM library
CMD> gm nova.nes   # boot a ROM directly from /nes/roms/
```

A NES emulator built on the vendored **[Anemoia-ESP32](https://github.com/Shim06/Anemoia-ESP32)**
core (GPL-3.0, © Shim06; see `NOTICES` #20). Supports **mappers 0–4 + 069** — roughly 90% of
the commercial library.

- **ROMs** live at `/nes/roms/<name>.nes` on the SD card. Bare `gm` opens a retro **library
  picker** (trackball / `W`·`S` to scroll, a scrollbar shows your position, `Enter` to load).
- **Controls in-game:** `WASD` + trackball = D-pad · `k` = B · `l` = A · `Space` = Select ·
  `Enter` / trackball-click = Start · `e` = save state · `r` = load state.
- **`q` returns to the ROM library** (not the CLI) so you can pick another game; press `q` again
  in the library to exit to the terminal.
- **Save states:** one slot per ROM at `/nes/states/<CRC32>.state` (`e` save / `r` load).
- **Audio** uses the I2S speaker; `vol` adjusts it live. **Auto-lock is suppressed** for the whole
  session (explicit `lock` / panic key still fire).

> Use only ROMs you are legally entitled to run. Open-source/homebrew titles such as
> [Nova the Squirrel](https://github.com/NovaSquirrel/NovaTheSquirrel) (GPL-3.0 code, MMC1) are a
> good, fully-legal way to test the emulator.

---

## `notes` / `nt` and `undercover` / `uc` — Undercover Mode

```
CMD> notes            # standalone Notes cover UI (no silent mode)
CMD> undercover        # silent Notes disguise
CMD> uc set             # set a secret exit passphrase
CMD> uc clear           # remove the exit passphrase
CMD> uc status          # show passphrase + boot-cover state
CMD> uc boot on         # boot directly into Notes (no splash)
CMD> uc boot off        # restore normal boot
CMD> uc panic set       # arm an instant-hide key (press the key to bind it)
CMD> uc panic off       # disable the instant-hide key
```

A real, SD-backed notes app (`/notes/NNN.txt`, at SD root — not `/apps/` — so it looks like an ordinary folder to anyone browsing the card on a PC) that doubles as a disguise. `notes` just runs the cover UI. `undercover` runs the same UI but also raises the covert flag: notification sounds and the hidden-SSID beep go silent, and the real T-Rex status bar stays hidden — passive tools (wguard, macwatch, espchat, etc.) keep running and logging to SD underneath, only the audible/visible tells go quiet.

| Action | Result |
|--------|--------|
| Tap a card / trackball UP-DOWN + click | Open a note |
| Tap a line inside a note | Move the cursor there |
| Type / Backspace / Enter | Edit the note in place |
| Trackball UP / DOWN (in a note) | Move cursor a line at a time |
| Trackball LEFT / RIGHT (in a note) | Move cursor a character at a time (wraps across lines) |
| Tap the save icon | Save to SD (toast confirms "Saved" or "No SD") |
| Tap the + FAB | New note |
| Tap the back chevron | Return to the list |
| `[q]` | Exit — fallback only, disabled once a passphrase is set |
| Type the secret passphrase (uc only) | Exit silently to the CLI, without saving the open note |
| Press the panic key (default `@`) | From anywhere — even mid-command — instantly drops into the cover |

No SD card → notes still work for the session (typing, editing, navigating) but nothing persists across a reboot — there is no crash or degraded mode, just no save. The passphrase is stored as `SHA-256(salt + phrase)` in `/config/undercover.conf`, never in plaintext; on a passphrase-match exit the currently-open note is deliberately **not** saved, so the phrase's own keystrokes (typed live into the note before the match is detected) never reach the SD card.

Touch wake — one tap wakes a half-dimmed screen, a double-tap (within 500 ms) wakes a fully screen-off device. This only works while undercover (`uc`) is active; the plain terminal and standalone `notes` never wake on touch.

**Boot-cover** (`uc boot on`) — the device boots directly into the Notes disguise with no T-REX splash screen or CLI flash. SD is initialised first so the `boot_cover` flag is read before anything is drawn; the screen goes black then Notes takes over immediately. If a lock PIN is also configured (`lock new` + `lock boot on`), the lock screen fires *after* the passphrase exits Notes — not before — so an observer only ever sees Notes from cold boot through unlock.

**Panic button** (`uc panic set`) — arms a single keyboard key as an instant-hide trigger. Pressing it **anywhere, even in the middle of another command** (a scan, a monitor, an attack), drops straight into the Notes cover; type the passphrase to return. Run `uc panic set` and press the key you want — the default is `@` (Sym+P). It only fires once an exit passphrase is set (so there is always a way back), and reserved keys are rejected (`'` autocomplete, `q` cover-exit, space, Enter, Backspace). `uc panic off` disables it. Note: while armed, that key can no longer be typed in commands (it hides instead) — the default `@` is chosen to avoid clashing with index targeting like `ps #2`; inside the cover the key types normally again.

**Still TODO:** duress/decoy passphrase (a second phrase that opens a clean notepad under coercion) — not currently planned.

---

## SD Card Commands

```
CMD> sdinfo                  # SD card type and capacity
CMD> sdls [path]             # list directory (default: CWD)
CMD> cd <path>               # change working directory
CMD> cat <path>              # read file — scrollable viewer, tpad UP/DN, q to quit
CMD> rm <path>               # delete a file
CMD> sdformat [init]         # format SD card to FAT32 (WARNING: destroys all data)
```

`cat` loads up to 400 lines, strips Windows `\r`, and shows a scrollable viewer with a cyan scrollbar. Tab-complete navigates into directories.

`sdformat` prompts for confirmation before formatting. Use `sdf init` to format and re-initialise the directory structure in one step.

→ See [SD Card](sdcard.md) for the complete file layout and optional files.

---

## Diagnostics

```
CMD> gps on    # start GPS background task with live status (T-Deck Plus)
CMD> gps off   # stop GPS task
CMD> gps test  # one-shot GPS coordinate read (T-Deck Plus)
CMD> test spk   # I2S speaker tone test + notif level test
CMD> test mic   # ES7210 mic test (level/VAD/record+play)
CMD> test lora  # LoRa SX1262 init, TX test, RX monitor
```

GPS status is shown in the status bar:
- Grey satellite icon — GPS off
- Yellow — searching for fix (~4 min cold start)
- Green — fix acquired

### Status bar icons

| Icon | Colours | Meaning |
|------|---------|---------|
| Shield | Grey | wguard not running |
| Shield | Green ✓ | wguard bg — no threats |
| Shield | Yellow | wguard bg — warnings |
| Shield | Red | wguard bg — critical alert |
| Satellite | Grey / Yellow / Green | GPS off / searching / fixed |
| ᛒ | Grey / Cyan | Bluetooth off / active |
| Battery | Red / Yellow / Green | Charge level |

> **Note:** `tone()` does not work on this hardware. All audio uses `i2s_driver_install()`.
