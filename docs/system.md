---
title: System
nav_order: 6
---

# System Commands

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

Config is saved to `/pwrsave.json` on the SD card and restored on boot.

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
CMD> sdls [path]             # list directory (default: /)
CMD> sdread <path>           # print file contents
CMD> sdrm <path>             # delete a file
CMD> sdformat [init]         # format SD card to FAT32 (WARNING: destroys all data)
```

`sdformat` prompts for confirmation before formatting. Use `sdf init` to format and re-initialise the directory structure in one step.

### SD Layout

```
/logs/                  — attack output logs
  eviltwin.csv          — captured portal credentials
  trackme.csv           — anti-tracking session log
  trackme_known.csv     — permanent device whitelist
  hidden_ssids.csv      — revealed hidden SSIDs
  cracked.csv           — cracked WPA2 passwords
/evilportal/            — custom captive portal HTML templates
/wordlist.txt           — WPA2 crack wordlist (one password per line)
/signatures.csv         — custom BLE tracker signatures
/macchanger.cfg         — MAC spoofer persistent config
/pwrsave.json           — power save config
```

---

## Diagnostics

```
CMD> gpson    # start GPS background task with live status (T-Deck Plus)
CMD> gpsoff   # stop GPS task
CMD> gpstest  # one-shot GPS coordinate read (T-Deck Plus)
CMD> spktest  # I2S speaker tone test
CMD> loratest # LoRa SX1262 init, TX test, RX monitor
```

GPS status is shown in the status bar:
- Grey satellite icon — GPS off
- Yellow — searching for fix (~4 min cold start)
- Green — fix acquired

> **Note:** `tone()` does not work on this hardware. All audio uses `i2s_driver_install()`.
