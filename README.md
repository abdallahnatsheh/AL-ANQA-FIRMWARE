<p align="center">
  <img src="images/banner.png" width="420"/>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-LilyGo%20T--Deck%20%7C%20T--Deck%20Plus-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/language-C%2FC%2B%2B-yellow?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-AGPL--3.0-red?style=flat-square"/>
  <img src="https://img.shields.io/badge/status-active%20development-orange?style=flat-square"/>
  <img src="https://img.shields.io/badge/device-ESP32--S3-green?style=flat-square"/>
</p>

<p align="center">
  <a href="https://abdallahnatsheh.github.io/T-DECK-CLI"><b>📖 Documentation</b></a>
</p>

<p align="center">
  <b>Offensive security firmware for the LilyGo T-Deck — hacker CLI in your pocket.</b><br/>
  <i>DISCOVER. ENUMERATE. COMPROMISE.</i>
</p>

---

> **⚠️ Legal Disclaimer**
> For **authorized security testing, CTF competitions, and educational use only.**
> Using these tools against networks or devices without explicit permission is illegal.
> **Always get written permission before you test.**

---

## What is T-Rex?

T-Rex turns the LilyGo T-Deck into a pocket pentesting terminal. No menus, no GUI — just a blinking cursor, a physical keyboard, and a full suite of offensive security tools running on an ESP32-S3.

---

## Features

**📡 WiFi** — [full guide](docs/wifi-attacks.md)
- Scan, connect, monitor mode, deauth attack
- Evil Twin AP with adaptive deauth + captive portal
- Hidden SSID reveal, WPA2 handshake capture + on-device crack
- MAC spoofer, WPS flag detection

**🌐 Network** — [full guide](docs/network.md)
- ARP host discovery, TCP port scan (4× parallel), top-31 ports
- ICMP ping, banner grabber (HTTP/TLS/MySQL/Redis), OS fingerprinting

**🔵 Bluetooth** — [full guide](docs/bluetooth.md)
- BLE device scanner (paginated, RSSI, name)
- Anti-tracking detector — BLE + WiFi probe surveillance with 3-gate confirmation and GPS movement awareness

**💾 SD Card**
- Browse, read, delete files; all attack logs saved automatically

**🖥️ System** — [full guide](docs/system.md)
- Man pages on-device (`man <cmd>`), paginated help, power save, Matrix animation
- Trackpad cursor — move cursor mid-command, click to execute

---

## Hardware

| Component | Details |
|-----------|---------|
| Devices | LilyGo T-Deck · LilyGo T-Deck Plus |
| MCU | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 320×240 ST7789 TFT |
| Input | Physical QWERTY keyboard (PS/2 over I2C) + trackball |
| Radio | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |
| GPS | L76K / u-blox M10Q (T-Deck Plus only) |

---

## Getting Started

**Requirements:** [VSCode](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/) extension

```bash
git clone https://github.com/abdallahnatsheh/T-Rex
# Open in VSCode → select env:T-Deck or env:T-Deck-Plus → click Upload
```

> **Can't upload?** Hold the trackball button, plug in USB, then try again — this forces download mode.

---

## Commands

| Command | Short | Args | Description |
|---------|-------|------|-------------|
| `help` | `hlp` | `[cmd]` | List all commands or detail for one |
| `man` | `mn` | `<cmd>` | On-device manual page |
| `info` | `inf` | — | Device info (chip, MACs, battery, SD) |
| `show` | `sh` | `<wifi\|ble\|hosts>` | Re-display last scan without rescanning |
| `clear` | `clr` | — | Clear screen |
| `pwrsave` | `psv` | `[status\|on\|off\|set ...]` | Power save config |
| **WiFi** | | | |
| `scanwifi` | `sw` | — | Scan WiFi networks |
| `connectwifi` | `cw` | `<index>` | Connect to scanned network |
| `clearwifi` | `clrw` | — | Erase saved credentials |
| `wifimon` | `wm` | `[ch]` | Monitor mode (ch 1-13, 0=hop) |
| `deauth` | `da` | `<bssid\|#> [ch] [client]` | Deauth attack |
| `eviltwin` | `et` | — | Evil Twin AP + captive portal |
| `hiddenssid` | `hs` | `<idx\|bssid> [ch] [silent]` | Reveal hidden SSID |
| `macchanger` | `mc` | `on\|off\|random\|set <mac>` | Spoof STA MAC |
| `wpasniff` | `ws` | `<idx\|bssid> [ch]` | Capture + crack WPA2 handshake |
| **Network** | | | |
| `netdiscover` | `nd` | — | ARP scan local /24 |
| `portscan` | `ps` | `<ip\|#> <start> <end>` | TCP port scan |
| `topscan` | `ts` | `<ip\|#>` | Top 31 common ports |
| `ping` | `pg` | `<ip\|hostname>` | ICMP ping |
| **Bluetooth** | | | |
| `scanblue` | `sbl` | — | BLE device scan |
| `trackme` | `tm` | `[silent]` | Anti-tracking detector |
| **SD Card** | | | |
| `sdinfo` | `sdi` | — | SD card info |
| `sdls` | `ls` | `[path]` | List directory |
| `sdread` | `sdr` | `<path>` | Read file |
| `sdrm` | `srm` | `<path>` | Delete file |
| **Diagnostics** | | | |
| `gpson` | `gon` | — | Start GPS task (T-Deck Plus) |
| `gpsoff` | `gof` | — | Stop GPS task |
| `gpstest` | `gt` | — | GPS coordinate test |
| `spktest` | `st` | — | Speaker tone test |
| `loratest` | `lt` | — | LoRa SX1262 diagnostic |
| `MATRIX` | `matrix` | — | Matrix rain animation |

> **Tip:** Run `nd` first, then use the host index in `ps`/`ts` instead of typing the IP.

---

## Navigation

All scan tables share the same keys:

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-run scan |
| `q` | Quit |

Trackball: roll left/right to move the cursor mid-command, click to execute.

---

## SD Layout

```
/logs/          — eviltwin.csv, trackme.csv, hidden_ssids.csv, cracked.csv
/evilportal/    — custom HTML portal templates
/signatures.csv — custom BLE tracker signatures
/pwrsave.json   — power save config
```

---

## Roadmap

- [x] WiFi scan, connect, monitor, deauth, Evil Twin, hidden SSID, MAC spoof, WPA2 crack, WPS flag
- [x] ARP discovery, port scan, ping, banner grabber, OS fingerprinting
- [x] BLE scanner, anti-tracking detector
- [x] SD file manager, man pages, help, power save, trackpad cursor
- [x] LoRa diagnostic, GPS (T-Deck Plus)
- [ ] BLE GATT enumeration
- [ ] BadUSB / DuckyScript
- [ ] LoRa packet logger
- [ ] MAC proximity watchlist
- [ ] DNS enumeration

---

## Screenshots

| Main Screen | WiFi Scanner | Network Scan |
|-------------|-------------|--------------|
| ![main](images/1.jpg) | ![wifi](images/3.jpg) | ![net](images/4.jpg) |

---

## Dependencies

All install automatically via PlatformIO `lib_deps`:
LovyanGFX · ESP32Ping · ESP32 BLE · Digital Rain Anim · Pangodream 18650CL · RadioLib · ArduinoJson · AceButton · TouchLib · TinyGPS++

---

## Contributing

PRs and issues welcome. To add a new command or attack module:
1. Open an issue describing the feature
2. Fork, branch, implement
3. Submit a PR referencing the issue

---

## Credits

- [Bruce Firmware](https://github.com/pr3y/Bruce) — an awesome open source project, parts were adapted and built upon (AGPL-3.0)
- [LilyGo T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) — hardware reference
- [AirGuard](https://github.com/seemoo-lab/AirGuard) — anti-tracking research (TU Darmstadt)
- Built with [ChatGPT](https://chatgpt.com) · [GitHub Copilot](https://github.com/features/copilot) · [Claude Code](https://claude.ai/code)

---

<p align="center">
  <i>T-Rex — your network never saw it coming.</i>
</p>
