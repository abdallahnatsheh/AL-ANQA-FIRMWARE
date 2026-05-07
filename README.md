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
  <b>Offensive security firmware for the LilyGo T-Deck — hacker CLI in your pocket.</b><br/>
  <i>DISCOVER. ENUMERATE. COMPROMISE.</i>
</p>

---

## What is T-Rex?

T-Rex is an offensive security toolkit firmware for the **LilyGo T-Deck** and **T-Deck Plus**, built around a raw terminal-first CLI that feels like a Linux shell. Type commands, run attacks, scan networks — all from a pocket-sized ESP32-S3 device with a physical keyboard.

Inspired by [Bruce](https://github.com/pr3y/Bruce), but with its own identity: no menus, no GUI — just a blinking cursor and a command line.

---

## Features

### 📡 WiFi
| Feature | Status |
|---------|--------|
| Network scanner (paginated, RSSI, open/WPA) | ✅ |
| Connect to network (saved credentials via NVS) | ✅ |
| Clear saved credentials | ✅ |
| Monitor mode / packet capture | ✅ |
| Deauth attack (raw 802.11) | ✅ |
| Evil Twin AP / Captive Portal | ✅ |
| WPA Handshake capture + PCAP export | 🔨 WIP |

### 🌐 Network Recon
| Feature | Status |
|---------|--------|
| ARP scan — LAN host discovery (paginated table) | ✅ |
| Port scan — 4× parallel, 150ms timeout | ✅ |
| Top-26 common ports scan | ✅ |
| Ping — ICMP with RTT + packet loss | ✅ |
| Banner grabber | 🔨 WIP |
| DNS enumeration | 🔨 WIP |

### 🔵 Bluetooth
| Feature | Status |
|---------|--------|
| BLE device scanner (paginated, RSSI, name) | ✅ |
| BLE GATT enumeration | 🔨 WIP |
| Bluetooth Classic scan | 🔨 WIP |

### 🛡️ Anti-Tracking
| Feature | Status |
|---------|--------|
| BLE tracker detection (AirTag, Tile, Samsung, Chipolo, Eufy, Pebblebee) | ✅ |
| WiFi probe request surveillance detection | ✅ |
| Behaviour scoring — unknown device analysis | ✅ |
| Traffic jam / crowd false positive filtering | ✅ |
| Kalman-filtered RSSI distance estimation | ✅ |
| Custom tracker signatures from SD card | ✅ |
| GPS displacement tracking (T-Deck Plus only) | ✅ |
| Speaker alert (WARNING beep / ALERT alarm) | ✅ |
| SD card alert logging | ✅ |

### 💾 SD Card
| Feature | Status |
|---------|--------|
| SD card info | ✅ |
| Directory listing | ✅ |
| File read | ✅ |
| File delete | ✅ |

### 🖥️ System
| Feature | Status |
|---------|--------|
| Terminal-style CLI interface | ✅ |
| Device info (IP, MAC, battery) | ✅ |
| GPS coordinate test (T-Deck Plus) | ✅ |
| Speaker / I2S audio test | ✅ |
| Matrix rain animation | ✅ |
| T-Deck Plus display support | ✅ |

---

## Hardware

| Component | Details |
|-----------|---------|
| Devices | LilyGo T-Deck · LilyGo T-Deck Plus |
| MCU | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 320×240 ST7789 TFT |
| Input | Physical QWERTY keyboard (PS/2 over I2C) |
| Radio | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |
| GPS | L76K / u-blox M10Q (T-Deck Plus only) |

---

## Dependencies

| Library | Purpose |
|---------|---------|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Display driver |
| [ESP32Ping](https://github.com/marian-craciunescu/ESP32Ping) | ICMP ping |
| [ESP32 BLE Arduino](https://github.com/espressif/arduino-esp32) | BLE scanning (bundled with core) |
| [Digital Rain Animation](https://github.com/0015/TP_Arduino_DigitalRain_Anim) | Matrix rain effect |
| [Pangodream 18650CL](https://github.com/pangodream/18650CL) | Battery level |
| [RadioLib](https://github.com/jgromes/RadioLib) | LoRa radio *(upcoming features)* |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | JSON config/data |
| [AceButton](https://github.com/bxparks/AceButton) | Physical button handling |
| [TouchLib](https://github.com/mmMicky/TouchLib) | Touchscreen input |
| [TinyGPS++](https://github.com/mikalhart/TinyGPSPlus) | GPS parsing (T-Deck Plus only) |

> All dependencies auto-install on first build via PlatformIO `lib_deps`. TinyGPS++ must be added to `lib_deps` for T-Deck Plus builds.

---

## Getting Started

### Requirements
- [VSCode](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/)
- LilyGo T-Deck or T-Deck Plus hardware

### Build & Flash
```bash
git clone https://github.com/abdallahnatsheh/T-Rex
cd T-Rex
# Open in VSCode with PlatformIO
# Select env:T-Deck or env:T-Deck-Plus in the PlatformIO toolbar
# Click the Upload (→) button
```

> **Can't upload?** Hold the trackball button, plug in USB, then try again — this forces the device into download mode.

---

## Commands

```
CMD> help
```

| Command | Short | Args | Description |
|---------|-------|------|-------------|
| `help` | `hlp` | `[command]` | Show all commands or detail for one |
| `info` | `inf` | — | Device info — IP, MAC, battery |
| `clear` | `clr` | — | Clear the screen |
| **WiFi** | | | |
| `scanwifi` | `sw` | — | Scan WiFi networks (paginated table) |
| `connectwifi` | `cw` | `<index>` | Connect to network from last scan |
| `clearwifi` | `clrw` | — | Erase all saved WiFi credentials |
| `wifimon` | `wm` | `[ch]` | Monitor mode — channel 1-13, 0=hop |
| `deauth` | `da` | `<bssid\|#> [ch] [client]` | Deauth attack via raw 802.11 |
| `eviltwin` | `et` | `[ssid]` | Evil Twin AP with captive portal |
| **Network** | | | |
| `netdiscover` | `nd` | — | ARP scan local /24 (paginated, u=rescan) |
| `portscan` | `ps` | `<ip\|#> <start> <end>` | TCP port scan, 4× parallel |
| `topscan` | `ts` | `<ip\|#>` | Scan top 26 common ports |
| `ping` | `pg` | `<ip\|hostname>` | ICMP ping with RTT + loss stats |
| **Bluetooth** | | | |
| `scanblue` | `sbl` | — | BLE device scan (paginated) |
| `trackme` | `tm` | — | Anti-tracking scanner — BLE + WiFi probes |
| **SD Card** | | | |
| `sdinfo` | `sdi` | — | SD card type and capacity |
| `sdls` | `ls` | `[path]` | List SD card directory |
| `sdread` | `sdr` | `<path>` | Read file from SD card |
| `sdrm` | `srm` | `<path>` | Delete file from SD card |
| **Diagnostics** | | | |
| `gpstest` | `gt` | — | GPS coordinate test (T-Deck Plus only) |
| `spktest` | `st` | — | Speaker / I2S tone test |
| `MATRIX` | `matrix` | — | Matrix rain animation (q to exit) |

> **ARP index shortcut:** run `nd` first, then use the host index (e.g. `ts 3`) instead of typing an IP address in `ps`/`ts`.

---

## Evil Twin AP — How It Works

```
CMD> eviltwin
```

T-Rex scans nearby networks and shows a picker. Pick a network and T-Rex automatically applies the right strategy:

| Target | MAC strategy | Deauth target | Result |
|--------|-------------|---------------|--------|
| Open network | Clone real AP's MAC + channel | Real AP BSSID | Devices reconnect to fake AP seamlessly — identical fingerprint |
| WPA/WPA2 network | Random locally-administered MAC | Real AP BSSID | Deauth only hits real AP; our clients are safe from our own frames |

**Adaptive deauth** — bursts every 8 seconds, but **automatically pauses** the moment a device connects to the portal so they can submit credentials uninterrupted. Resumes when they disconnect.

**Custom portal pages** — drop `.html` files into `/evilportal/` on the SD card. Press `[s]` while the portal is running to switch to an SD template live.

Credentials are saved to `/logs/eviltwin.csv` on the SD card.

---

## 🛡️ How `trackme` works

```
CMD> trackme
```

`trackme` is a passive anti-tracking detector that uses BLE and WiFi to detect devices that may be following you. Think of it like wardriving — but instead of mapping networks, you are watching whether any radio signal is consistently following you through the environment.

### Detection pipeline

The scanner alternates between BLE (1 second) and WiFi probe sniffing (0.5 seconds) in a continuous loop, time-slicing the shared ESP32-S3 radio. Every detected device passes through a 3-gate pipeline:

**Gate 1 — Signature check**
The device's BLE manufacturer data is matched against a table of known tracker signatures. AirTags, Tile, Samsung SmartTag, Chipolo, Eufy, and Pebblebee are identified by company ID, payload byte, and manufacturer data length. Apple non-trackers (iPhones, Macs, AirPods) are classified separately and never flagged as threats — matching the company ID alone is not enough.

**Gate 2 — Behaviour scoring** *(unknown devices only)*
Devices that don't match any known tracker signature are scored on behaviour: time seen, RSSI consistency (staying at the same distance from you), gap-and-return events (device disappeared then came back), and sighting count across multiple scan cycles. Score below 40 → ignored. Score 40 or above → proceeds to Gate 3.

**Gate 3 — Confirmation** *(all devices)*
Before any alert fires: seen for at least 5 minutes, still nearby (RSSI above -80 dBm), and not explained by a traffic jam or crowd scenario. Requires either 3+ distinct gap-and-return windows or 3+ total sightings. A device that arrived when 4 or more others were present and has never disappeared and returned is treated as a coincidence, not a tracker.

### RSSI and Kalman filtering

Every RSSI reading is smoothed with a 1-D Kalman filter before use. Devices below -85 dBm are dropped immediately — too far to be a credible threat. The filter also enables RSSI consistency scoring: a real tracker stays at roughly the same distance from you, giving a very low RSSI variance that is heavily weighted in Gate 2.

### Device tiers

| Tier | Max | Purpose |
|------|-----|---------|
| Tier 1 | 20 | Close devices — full 3-gate analysis |
| Tier 2 | 100 | Distant or Apple devices — lightweight tracking only |

Apple non-trackers are permanently locked to Tier 2 and never scored or alerted.

### Alert levels

| Level | Trigger | Speaker | Display |
|-------|---------|---------|---------|
| NOTICE | Known tracker seen 60 s, not yet Gate 3 confirmed | Silent | Yellow row |
| WARNING | Unknown device score ≥ 60, Gate 3 pending | 1 short beep | Orange row |
| ALERT | Gate 3 confirmed — device is following you | 3 beeps, repeat every 30 s | Red row |

### Custom tracker signatures

Drop a `signatures.csv` on the SD card root to extend the tracker database without reflashing:

```
# type,company_id,name,threat_level
BLE,0x004C,Apple AirTag,HIGH
BLE,0x00D7,Tile Tracker,HIGH
```

If no SD card is present, a hardcoded list covers all major commercial trackers. Apple non-tracker entries are always appended automatically regardless of SD card content.

### Known limitations

- **MAC randomization**: iPhones and Android phones rotate their BLE MAC every ~15 minutes. Unknown devices reset their history on each rotation. Commercial trackers (AirTag, Tile, etc.) use consistent MACs and are detected reliably.
- **Single radio**: BLE and WiFi share one physical radio and cannot scan simultaneously. The time-slice means a brief advertisement could theoretically be missed, but trackers broadcast repeatedly so this is not a practical problem.
- **GPS** (T-Deck Plus only): When GPS is available, physical displacement is used to strengthen Gate 3 confirmation — moving 100 m+ with a device still present increments its gap-and-return window count. On base T-Deck, time and RSSI consistency serve as proxies.

---

## Navigation

All scan results use a consistent table UI:

| Key | Action |
|-----|--------|
| `l` | Next page |
| `a` | Previous page |
| `u` | Re-run scan (WiFi, ARP) |
| `q` | Quit / back to prompt |

---

## Roadmap

- [x] WiFi scanner & connect with saved credentials
- [x] WiFi monitor mode
- [x] Deauth attack (raw 802.11, broadcast + targeted)
- [x] Evil Twin AP (clone MAC, adaptive deauth, captive portal, SD templates)
- [x] ARP LAN discovery (batch scan, paginated table)
- [x] Parallel TCP port scan (4× tasks, 150 ms timeout)
- [x] Top-26 ports scan
- [x] ICMP ping with RTT stats
- [x] BLE scanner
- [x] Anti-tracking detector (trackme)
- [x] SD card file manager
- [x] T-Deck Plus support (GPS + speaker diagnostics)
- [ ] WPA handshake capture + PCAP export to SD
- [ ] BadUSB / HID keystroke injection (DuckyScript)
- [ ] BLE GATT enumeration
- [ ] LoRa frequency scanner + packet logger
- [ ] Banner grabber
- [ ] DNS enumeration

---

## Screenshots

| Main Screen | WiFi Scanner | Network Scan |
|-------------|-------------|--------------|
| ![main](images/1.jpg) | ![wifi](images/3.jpg) | ![net](images/4.jpg) |

---

## ⚠️ Disclaimer

T-Rex is intended for **authorized security testing and educational purposes only**. Only use on networks and devices you own or have explicit written permission to test. The developers assume no liability for any misuse of this tool. Know your local laws.

---

## Contributing

PRs and issues are welcome! If you want to add a new attack module or command:

1. Open an issue describing the feature
2. Fork the repo and create a branch
3. Submit a PR referencing the issue

---

## Credits

Built on the shoulders of giants:
- [Bruce Firmware](https://github.com/pr3y/Bruce) — offensive ESP32 toolkit (Evil Twin HTML templates adapted under AGPL-3.0)
- [LilyGo T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) — hardware reference
- [AirGuard](https://github.com/seemoo-lab/AirGuard) — anti-tracking research and methodology by the Secure Mobile Networking Lab, TU Darmstadt. No code was used; credited for research inspiration only.
- Built with [ChatGPT](https://chatgpt.com) · [GitHub Copilot](https://github.com/features/copilot) · [Claude Code](https://claude.ai/code)

---

<p align="center">
  <i>T-Rex — your network never saw it coming.</i>
</p>
