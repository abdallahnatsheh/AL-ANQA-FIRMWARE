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

**📡 WiFi Attacks** — [full guide](docs/wifi-attacks.md)
- Scan, connect, monitor mode, deauth attack
- Evil Twin AP with adaptive deauth + captive portal
- Hidden SSID reveal, WPA2 handshake capture + on-device crack
- MAC spoofer, WPS flag detection
- **WiFi IDS** (`wguard`) — passive intrusion detection: deauth flood, evil twin, handshake harvest, PMKID grab, auth flood, probe storm, beacon flood, BSSID cloning, Karma attack; background mode with shield icon + popup alerts; session CSV logs with session-relative timestamps

**🔑 WiFi Credentials** — [full guide](docs/wifi-credentials.md)
- Saved credential manager (`wifipass`) — view all saved passwords
- Auto-save on connect, NVS + SD dual storage
- Linux `wpa_supplicant.conf` bidirectional sync — share networks between T-Deck and Linux
- Desktop Linux migration via `nmcli` + `wpa_passphrase`

**🌐 Network** — [full guide](docs/network.md)
- ARP host discovery, TCP port scan (4× parallel), top-31 ports
- ICMP ping, banner grabber (HTTP/TLS/MySQL/Redis), OS fingerprinting

**🔵 Bluetooth** — [full guide](docs/bluetooth.md)
- BLE device scanner (paginated, RSSI, name)
- **BLE GATT enumeration** (`bi`) — connect to any BLE device and read its full service/characteristic tree; 0x2901 user descriptions, 0x2904 type auto-decode (uint8/16/32, int8/16, UTF-8); interactive write (trackpad cursor, write-without-response fallback), fuzz (seq/rand/boundary), notify/indicate sniff with 30 s live stream + write-while-connected, pairing/bonding; `bi all` sweeps every scanned device; full UUID + full hex saved to SD — [full guide](docs/bleinfo.md)
  - **Auth leak detector** (`[b]` audit) — inline risk scoring flags AES-sized binary blobs, hex-encoded secrets, and PIN-shaped values; `[b]` key shows filtered triage view
  - **Write-cap** (`[r]wcap`) — replay any captured notification back to a writable char; sniff auto-saves `.ble` packet archives to SD; load captures from previous sessions
  - **Protocol reverse engineering** — sniff baseline → trigger action on device → identify new packet → replay or write back; works on proprietary protocols with no documentation
- Anti-tracking detector — BLE + WiFi probe surveillance with 3-gate confirmation and GPS movement awareness
- **Fast Pair attack** (`fp`) — scan for Fast Pair devices, flood Google FP advertisements with per-cycle MAC randomization, GATT probe (WhisperPair) to read anti-spoofing keys
- **BLE notification spam** (`bs`) — Apple Continuity (Proximity Pairing + Nearby Info popups), Google Fast Pair flood, Microsoft Swift Pair, Samsung Galaxy accessory popups

**🤖 Claude Desktop Buddy** — [full guide](docs/buddy.md)
- **BLE remote** (`bd`) — approve/deny Claude Desktop permission prompts from the T-Deck keyboard
- Full-screen terminal popup for long commands — full text wrapped across multiple lines, no truncation
- Live session stats: tokens, level, mood, energy — all persisted to NVS
- 19 ASCII pet species with 7-state animation at 5 fps

**💾 SD Card**
- Browse, read, delete files; all attack logs saved automatically

**🔌 USB Gadget** — [full guide](docs/usb.md)
- **Mass Storage** (`um`) — expose SD card as a USB drive; read and write files from any PC with no drivers
- **USB Keyboard + Mouse** (`uk`) — T-Deck becomes a full USB input device; physical keyboard types into host, trackball moves the mouse cursor with hardware acceleration; tap = left click, hold = right click, hold 1.5s = exit
- **BadUSB / DuckyScript** (`ux`) — execute keystroke injection payloads; Flipper Zero DuckyScript v1 compatible; built-in T-Rex demo; scripts in `/badusb/` on SD

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
| `connectwifi` | `cw` | `<index\|ssid>` | Connect by scan index or SSID name |
| `wifipass` | `wp` | — | View all saved WiFi passwords (SD + NVS merged) |
| `wifiexport` | `wex` | — | Export NVS credentials → wpa_supplicant.conf |
| `clearwifi` | `clrw` | — | Erase saved credentials |
| `wifimon` | `wm` | `[ch]` | Monitor mode (ch 1-13, 0=hop) |
| `deauth` | `da` | `<bssid\|#> [ch] [client]` | Deauth attack |
| `eviltwin` | `et` | — | Evil Twin AP + captive portal |
| `hiddenssid` | `hs` | `<idx\|bssid> [ch] [silent]` | Reveal hidden SSID |
| `macchanger` | `mc` | `on\|off\|random\|set <mac>` | Spoof STA MAC |
| `wpasniff` | `ws` | `<idx\|bssid> [ch]` | Capture + crack WPA2 handshake |
| `wguard` | `wg` | `<idx\|bssid> [ch] [bg]` | WiFi IDS — passive intrusion detection; `wg stop` / `wg view` |
| **Network** | | | |
| `netdiscover` | `nd` | — | ARP scan local /24 |
| `portscan` | `ps` | `<ip\|#> <start> <end>` | TCP port scan |
| `topscan` | `ts` | `<ip\|#>` | Top 31 common ports |
| `ping` | `pg` | `<ip\|hostname>` | ICMP ping |
| **Bluetooth** | | | |
| `scanblue` | `sbl` | — | BLE device scan |
| `bleinfo` | `bi` | `<index\|mac\|all>` | GATT enum: services, chars, values, write, fuzz, sniff, pair |
| `trackme` | `tm` | `[silent]` | Anti-tracking detector |
| `fastpair` | `fp` | `[scan\|spam\|h <idx>\|h all]` | Fast Pair: scan devices / flood ads / GATT hijack |
| `blespam` | `bs` | `[apple\|android\|ms\|samsung\|all]` | BLE notification spam (popups on nearby devices) |
| `buddy` | `bd` | `[name]` | Claude Desktop remote — approve prompts, ASCII pet, NVS stats |
| **SD Card** | | | |
| `sdinfo` | `sdi` | — | SD card info |
| `sdls` | `ls` | `[path]` | List directory (CWD if no path, paginated, dirs in cyan) |
| `cd` | `cd` | `<dir\|..>` | Change working directory — `cd badusb`, `cd ..`, `cd /` |
| `sdread` | `sdr` | `<path>` | Read file (relative to CWD) |
| `sdrm` | `srm` | `<path>` | Delete file (relative to CWD) |
| **USB** | | | |
| `usbmsc` | `um` | — | Expose SD card as USB Mass Storage drive |
| `usbkbd` | `uk` | — | T-Deck as USB keyboard + mouse (trackball = cursor, tap = left click, hold = right click) |
| `usbexec` | `ux` | `demo\|<path>` | BadUSB — execute DuckyScript payload (Flipper Zero compatible) |
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

**Trackball (command line):**

| Direction | Action |
|-----------|--------|
| Left / Right | Move cursor within the current command |
| Up / Down | Scroll through command history (16 entries) |
| Click | Execute command |

**Autocomplete:** Press `'` (Sym+K) at any point in a command.
- At the start → completes command names (`scan` + `'` shows `scanwifi`, `scanblue`)
- After a command name → completes file/dir paths from the current working directory
- Smart filtering: `cd` suggests dirs only, `sdr`/`srm`/`ux` suggest files only, `ls` suggests both
- Fills common prefix automatically (Linux-style); single match adds a trailing space

---

## SD Layout

```
/wpa_supplicant.conf  — saved WiFi credentials (Linux-compatible format)
/wpa_supplicant.bak   — auto-backup of original file before T-Rex modifies it
/wordlist.txt         — custom WPA crack wordlist (one password per line, ≥8 chars)
/pwrsave.conf         — power save config (key=value)
/macchanger.conf      — MAC changer config (key=value)
/signatures.csv       — custom BLE tracker signatures
/logs/                — eviltwin.csv, trackme.csv, hidden_ssids.csv, cracked.csv, fastpair.csv
/logs/wguard/         — wguard session files (001.csv, 002.csv … — never overwritten)
/logs/hs/             — WPA handshake captures (<BSSID>.cap, libpcap format)
/logs/bleinfo/        — GATT dumps (<mac>.txt), sniff logs (<mac>_sniff.txt), write-cap archives (<mac>_replay.ble)
/fastpair_keys.csv    — cached Fast Pair anti-spoofing keys (modelId,name,key64hex)
/fastpair_paired.csv  — log of devices successfully paired via GATT
/evilportal/          — custom HTML portal templates
/badusb/              — DuckyScript payload files (auto-created on boot)
/captures/            — misc capture output (auto-created on boot)
```

> See the [SD Card guide](docs/sdcard.md) for the full file reference and quick-start checklist.

---

## Roadmap

- [x] WiFi scan, connect, monitor, deauth, Evil Twin, hidden SSID, MAC spoof, WPA2 crack, WPS flag, saved credential manager, Linux wpa_supplicant.conf sync
- [x] ARP discovery, port scan, ping, banner grabber, OS fingerprinting
- [x] BLE scanner, anti-tracking detector
- [x] BLE GATT enumeration (`bi`) — full service/char tree, 0x2904 auto-decode, write (trackpad cursor, write-without-response fallback), fuzz, notify/indicate sniff with write-while-connected, pairing/bonding, `bi all` sweep, full UUID + full hex saved to SD
- [x] BLE auth leak detector (`[b]` audit view) — inline risk scoring for AES blobs / hex secrets / PINs
- [x] BLE write-cap (`[r]wcap`) — replay captured notification values back to writable chars; `.ble` SD archive; protocol reverse engineering workflow
- [x] Fast Pair attack — advertisement flood + GATT probe (WhisperPair)
- [x] BLE notification spam — Apple / Android / Microsoft / Samsung
- [x] SD file manager — `ls` (paginated, dirs in cyan), `cd` CWD navigation (relative paths for all SD commands), man pages, help, power save, trackpad cursor
- [x] Command history (16-entry ring buffer, trackpad UP/DOWN), tab autocomplete with smart per-command filtering (Sym+K)
- [x] LoRa diagnostic, GPS (T-Deck Plus)
- [x] USB Mass Storage — expose SD card as USB drive (read + write, 2MB file tested)
- [x] USB Keyboard + Mouse — T-Deck physical keyboard + trackball as full USB HID input device
- [x] Claude Desktop Buddy — BLE remote, permission prompts, ASCII pet, NVS stats
- [x] BadUSB / DuckyScript — Flipper Zero DuckyScript v1 compatible, hyphenated combos, REPEAT, built-in demo
- [x] `wguard` WiFi IDS — deauth flood, evil twin (two-tier RSSI-filtered detection), handshake harvest, PMKID grab, auth flood, probe storm, beacon flood, BSSID cloning, Karma attack; background mode with shield icon + popup bars; session CSV logs (session-relative timestamps, no duplicate events across save blocks)
- [x] Notification manager — I2S WAV playback from SD, per-level volume, screen wake callback; wired into Buddy, TrackMe, wguard
- [ ] LoRa packet logger
- [ ] MAC proximity watchlist
- [ ] DNS enumeration
- [ ] `bmon` — passive BLE advertisement sniffer: cleartext detector, iBeacon/Eddystone parser, PCAP export to SD (Wireshark compatible)

---

## Screenshots

| Main Screen | WiFi Scanner | Network Scan |
|-------------|-------------|--------------|
| ![main](images/1.jpg) | ![wifi](images/3.jpg) | ![net](images/4.jpg) |

---

## Dependencies

All install automatically via PlatformIO `lib_deps`:
LovyanGFX · ESP32Ping · NimBLE-Arduino (h2zero) · Digital Rain Anim · Pangodream 18650CL · RadioLib · ArduinoJson · AceButton · TouchLib · TinyGPS++

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
