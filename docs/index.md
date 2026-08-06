---
title: Home
nav_order: 1
description: Al-Anqa offensive security firmware for LilyGo T-Deck
permalink: /
lang: en
---

<p align="center">
  <img src="assets/images/banner.png" width="480"/>
</p>

# Al-Anqa

**Offensive security firmware for the LilyGo T-Deck — hacker CLI in your pocket.**

Al-Anqa turns the LilyGo T-Deck into a pocket pentesting terminal. No menus, no GUI — just a blinking cursor, a physical keyboard, and a full suite of offensive security tools running on an ESP32-S3.

---

> ⚠️ **Legal Disclaimer** — For authorized security testing, CTF competitions, and educational use only. Always get written permission before testing.

---

## Documentation

### 🚀 Start Here

- [**Getting Started**](getting-started) — Flash, first boot, SD setup, first commands
- [**Keyboard Reference**](keyboard) — Sym key, autocomplete, history, cursor editing, trackball
- [**Workflows**](workflows) — End-to-end examples: WPA2 capture, network recon, IDS, tracker detection
- [**T-Deck vs T-Deck Plus**](hardware) — The only hardware difference is GPS
- [**Troubleshooting**](troubleshooting) — Upload failures, SD issues, WiFi, BLE, GPS, lock screen

---

### 📡 WiFi

- [Scan & Connect](wifi-scan) — `scanwifi` · `connectwifi`
- [WiFi Monitor](wifimon) — `wifimon`
- [Wardrive](wardrive) — `wardrive` — WiFi + GPS → WiGLE CSV (Plus only)
- [WiFi Credentials](wifi-credentials) — `wifipass` · `wp export` · `wp clear`
- [MAC Changer](macchanger) — `macchanger`
- [WiFi Attacks](wifi-attacks)
  - [Deauth](deauth) — `deauth`
  - [Evil Twin](eviltwin) — `eviltwin`
  - [Hidden SSID](hiddenssid) — `hiddenssid`
  - [WPA Sniff](wpasniff) — `wpasniff`
  - [PMKID Attack](pmkid) — `pmkid`
  - [WGuard IDS](wguard) — `wguard`
  - [Beacon Flood](beacon-flood) — `beaconflood`
  - [WPS](wps) — `wps` — recon + PIN gen + handshake capture
  - [WPA3 Downgrade](wpa3down) — `wpa3down` — **[EXP]** transition-mode downgrade to WPA2

---

### 🌐 Network

- [Net Discover](netdiscover) — `netdiscover`
- [Net Spy](netspy) — `netspy` / `ns` — **[EXP]** passive client-isolation device recon (AirSnitch)
- [Iso Scan](isoscan) — `isoscan` / `is` — **[EXP]** active isolation audit: GTK inject + capture (AirSnitch)
- [Port Scan](portscan) — `portscan` · `ps top` · banner grabber · OS fingerprint
- [Ping](ping) — `ping`
- [SSH Client](ssh) — `ssh` — interactive colour terminal + scrollback
- [ARP Spoof](arpspoof) — `arpspoof` — L2 ARP cache poisoning + redirected-traffic log
- [Responder](responder) — `responder` — **[EXP]** LLMNR/NBT-NS/mDNS poisoner + NetNTLM capture
- [Default-Password Check](dpwo) — `dpwo` / `dw` — **[EXP]** default creds on FTP/SSH/Telnet/HTTP/RTSP/Redis/MQTT/SNMP (custom ports too)

---

### 🔵 Bluetooth

- [Scan BLE](scanblue) — `scanblue`
- [BLE Info](bleinfo) — `bleinfo`
- [Tracking Detection](trackme) — `trackme`
- [Fast Pair](fastpair) — `fastpair`
- [BLE Spam](blespam) — `blespam`
- [Buddy](buddy) — `buddy`
- [BT Keyboard](btkbd) — `btkbd`
- [BLE Monitor](bmon) — `bmon` — passive advertisement sniffer (iBeacon/Eddystone)
- [MAC Watch](macwatch) — `macwatch` — MAC watchlist + proximity alert

---

### 🔌 USB

- [USB Mass Storage](usbmsc) — `usbmsc`
- [USB Keyboard](usbkbd) — `usbkbd`
- [BadUSB](usbexec) — `usbexec`
- [Mouse Jiggler](jiggle) — `jiggle`

---

### ⚙️ System

- [Help & Manual](help-man) — `help` · `man` · `show` · `clear` · `MATRIX`
- [Device Info](info) — `info`
- [Power Save](pwrsave) — `pwrsave` · `sleep`
- [Lock Screen](lock) — `lock`
- [Undercover Mode](undercover) — `notes` · `undercover`
- [NES Emulator](game) — `game` — play NES ROMs from the SD card
- [Timezone](tz) — `tz`
- [Audio & Notifications](audio) — `volume` · `notif` · `test spk`
- [SD Commands](sd-commands) — `sdinfo` · `sdls` · `cd` · `cat` · `edit` · `rm` · `sdformat`
- [Diagnostics](diagnostics) — `gps on/off/test` · `test spk/mic/lora` · `i2cscan` · `csidetect`
- [SD Card Layout](sdcard) — file layout reference
- [Custom Splash Screen](splash) — replace the boot image with your own PNG

---

## Quick Start

**Requirements:** [VSCode](https://code.visualstudio.com) + [PlatformIO](https://platformio.org) extension

```bash
git clone https://github.com/abdallahnatsheh/AL-ANQA-FIRMWARE
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
