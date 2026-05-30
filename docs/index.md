---
title: Home
nav_order: 1
description: T-Rex offensive security firmware for LilyGo T-Deck
permalink: /
---

<p align="center">
  <img src="assets/images/banner.png" width="480"/>
</p>

# T-Rex

**Offensive security firmware for the LilyGo T-Deck — hacker CLI in your pocket.**

T-Rex turns the LilyGo T-Deck into a pocket pentesting terminal. No menus, no GUI — just a blinking cursor, a physical keyboard, and a full suite of offensive security tools running on an ESP32-S3.

---

> ⚠️ **Legal Disclaimer** — For authorized security testing, CTF competitions, and educational use only. Always get written permission before testing.

---

## Documentation

### Start Here
| Guide | What's inside |
|-------|--------------|
| [Getting Started](getting-started) | Flash, first boot, SD setup, first commands — start here |
| [Keyboard Reference](keyboard) | Sym key, autocomplete, history, cursor editing, trackball |
| [Workflows](workflows) | End-to-end examples: WPA2 capture, network recon, IDS, tracker detection |
| [T-Deck vs T-Deck Plus](hardware) | The only difference is GPS — what it changes |
| [Troubleshooting](troubleshooting) | Upload failures, SD issues, WiFi, BLE, GPS, lock screen |

### WiFi
| Guide | Commands |
|-------|---------|
| [Scan & Connect](wifi-scan) | `scanwifi` `connectwifi` `clearwifi` |
| [WiFi Monitor](wifimon) | `wifimon` |
| [WiFi Credentials](wifi-credentials) | `wifipass` `wifiexport` |
| [MAC Changer](macchanger) | `macchanger` |
| [WiFi Attacks](wifi-attacks) | *(see below)* |
| &nbsp;&nbsp;↳ [Deauth](deauth) | `deauth` |
| &nbsp;&nbsp;↳ [Evil Twin](eviltwin) | `eviltwin` |
| &nbsp;&nbsp;↳ [Hidden SSID](hiddenssid) | `hiddenssid` |
| &nbsp;&nbsp;↳ [WPA Sniff](wpasniff) | `wpasniff` |
| &nbsp;&nbsp;↳ [WGuard IDS](wguard) | `wguard` |
| &nbsp;&nbsp;↳ [Beacon Flood](beacon-flood) | `beaconflood` |

### Network
| Guide | Commands |
|-------|---------|
| [Net Discover](netdiscover) | `netdiscover` |
| [Port Scan](portscan) | `portscan` `topscan` + banner grabber + OS fingerprint |
| [Ping](ping) | `ping` |

### Bluetooth
| Guide | Commands |
|-------|---------|
| [Scan BLE](scanblue) | `scanblue` |
| [BLE Info](bleinfo) | `bleinfo` |
| [Tracking Detection](trackme) | `trackme` |
| [Fast Pair](fastpair) | `fastpair` |
| [BLE Spam](blespam) | `blespam` |
| [Buddy](buddy) | `buddy` |
| [BT Keyboard](btkbd) | `btkbd` |

### USB
| Guide | Commands |
|-------|---------|
| [USB Mass Storage](usbmsc) | `usbmsc` |
| [USB Keyboard](usbkbd) | `usbkbd` |
| [BadUSB](usbexec) | `usbexec` |
| [Mouse Jiggler](jiggle) | `jiggle` |

### System
| Guide | Commands |
|-------|---------|
| [Help & Manual](help-man) | `help` `man` `show` `clear` `MATRIX` |
| [Device Info](info) | `info` |
| [Power Save](pwrsave) | `pwrsave` |
| [Lock Screen](lock) | `lock` |
| [Timezone](tz) | `tz` |
| [Audio & Notifications](audio) | `volume` `notif` `spktest` |
| [SD Commands](sd-commands) | `sdinfo` `sdls` `cd` `cat` `rm` `sdformat` |
| [Diagnostics](diagnostics) | `gpson` `gpsoff` `gpstest` `spktest` `loratest` |
| [SD Card Layout](sdcard) | File layout reference |

---

## Quick Start

**Requirements:** [VSCode](https://code.visualstudio.com) + [PlatformIO](https://platformio.org) extension

```bash
git clone https://github.com/abdallahnatsheh/T-DECK-CLI
# Open in VSCode → select env:T-Deck or env:T-Deck-Plus → click Upload
```

> **Can't upload?** Hold the trackball button, plug in USB, then try again — this forces download mode.

---

## Hardware

| Component | Details |
|-----------|---------|
| Devices | LilyGo T-Deck · LilyGo T-Deck Plus |
| MCU | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 320×240 ST7789 TFT |
| Input | Physical QWERTY keyboard + trackball |
| Radio | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |
| GPS | L76K / u-blox M10Q (T-Deck Plus only) |
