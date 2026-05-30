---
title: System
nav_order: 10
has_children: true
---

# System

| Guide | Commands |
|-------|---------|
| [Help & Manual](help-man) | `help` / `hlp` · `man` / `mn` · `show` / `sh` · `clear` · `MATRIX` |
| [Device Info](info) | `info` / `inf` |
| [Power Save](pwrsave) | `pwrsave` / `psv` |
| [Lock Screen](lock) | `lock` / `lk` |
| [Timezone](tz) | `tz` |
| [Audio & Notifications](audio) | `volume` / `vol` · `notif` / `nf` · `spktest` / `st` |
| [SD Commands](sd-commands) | `sdinfo` · `sdls` · `cd` · `cat` · `rm` · `sdformat` |
| [Diagnostics](diagnostics) | `gps on` · `gps off` · `gps test` · `spktest` · `loratest` |
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

Config is saved to `/pwrsave.conf` on the SD card and restored on boot.

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
CMD> spktest  # I2S speaker tone test + notif level test
CMD> loratest # LoRa SX1262 init, TX test, RX monitor
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
