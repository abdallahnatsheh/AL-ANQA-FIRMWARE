---
title: System
nav_order: 10
has_children: true
---

# System

Device settings, utilities, and the everyday tools. Each has its own page (left) with full detail.

---

## Commands

| Tool | Command | What it does |
|------|---------|--------------|
| [Help & Manual](help-man) | `help` · `man` · `show` | List commands, read man pages, re-show the last scan |
| [Device Info](info) | `info` | Chip / MACs / battery / SD — plus a **GitHub QR** on the ABOUT page |
| [Power Save](pwrsave) | `pwrsave` · `sleep` | Inactivity dimming, battery-aware dim, deep sleep |
| [Lock Screen](lock) | `lock` | PIN lock, lock-on-boot, idle auto-lock |
| [Undercover Mode](undercover) | `notes` · `undercover` | Notes-app disguise + silent mode, passphrase, panic key |
| [NES Emulator](game) | `game` | Play NES ROMs from the SD card |
| [Timezone](tz) | `tz` | Set the clock's timezone (survives reboot) |
| [Weather](weather) | `weather` | Local weather (GPS / WiFi-geolocation, keyless) |
| [Audio & Notifications](audio) | `volume` · `notif` | Master volume + per-level notification sounds |
| [SD Commands](sd-commands) | `sdinfo` · `sdls` · `cd` · `cat` · `edit` · `rm` · `sdformat` | Browse and edit the SD card |
| [SD Card Layout](sdcard) | — | Reference: where every tool stores its files |
| [Diagnostics](diagnostics) | `gps` · `test` · `i2cscan` · `csidetect` | GPS, speaker/mic/LoRa tests, I2C scan, CSI motion |
| [Home Launcher](home) | `home` | App-launcher home screen |
| [Custom Splash](splash) | — | Replace the boot image with your own PNG |

**Two tiny extras:** `clear` / `clr` wipes the output area and resets the prompt; `MATRIX` runs a Matrix digital-rain animation (`q` to exit).

---

## Trackball & keyboard

At the prompt the trackball moves the cursor and browses history; in apps it navigates and selects. Full reference: **[Keyboard](keyboard)**.

---

## Status bar icons

| Icon | Colours | Meaning |
|------|---------|---------|
| `[ANQA]` | Cyan | Firmware name (with `+` on T-Deck Plus) |
| Shield | Grey / Green ✓ / Yellow / Red | wguard: off / no threats / warnings / critical |
| Satellite | Grey / Yellow / Green | GPS: off / searching / fixed |
| ᛒ | Grey / Cyan | Bluetooth: off / active |
| Battery | Red / Yellow / Green | Charge level |

> **Hardware note:** `tone()` doesn't work on this board — all audio uses `i2s_driver_install()`.
