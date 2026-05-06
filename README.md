<p align="center">
  <img src="images/banner.png" width="420"/>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-LilyGo%20T--Deck%20%7C%20T--Deck%20Plus-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/language-C%2FC%2B%2B-yellow?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-GPL--3.0-red?style=flat-square"/>
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

Inspired by [Bruce](https://github.com/pr3y/Bruce) and [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder), but with its own identity: no menus, no GUI — just a blinking cursor and a command line.

---

## Features

### 📡 WiFi
| Feature | Command | Status |
|---------|---------|--------|
| Network scanner — paginated, RSSI, open/WPA | `scanwifi` | ✅ |
| Connect to network (saved credentials via NVS) | `connectwifi` | ✅ |
| Clear saved credentials | `clearwifi` | ✅ |
| Monitor mode / packet capture | `wifimon` | ✅ |
| Deauth attack — raw 802.11, broadcast or targeted | `deauth` | ✅ |
| Evil Twin AP — clone + MAC spoof + adaptive deauth | `eviltwin` | ✅ |
| WPA handshake capture | — | 🔨 WIP |

### 🌐 Network Recon
| Feature | Command | Status |
|---------|---------|--------|
| ARP scan — LAN host discovery, paginated table | `netdiscover` | ✅ |
| Port scan — 4× parallel, 150 ms timeout | `portscan` | ✅ |
| Top-26 ports scan (nmap-style common ports) | `topscan` | ✅ |
| Ping — ICMP with RTT + packet loss | `ping` | ✅ |
| Banner grabber | — | 🔨 WIP |

### 🔵 Bluetooth / RF
| Feature | Command | Status |
|---------|---------|--------|
| BLE device scanner — paginated, RSSI, name | `scanblue` | ✅ |
| Anti-tracking scanner — BLE + WiFi probe sniff | `trackme` | ✅ |
| BLE GATT enumeration | — | 🔨 WIP |
| Bluetooth Classic scan | — | 🔨 WIP |

### 💾 SD Card
| Feature | Command | Status |
|---------|---------|--------|
| SD card info | `sdinfo` | ✅ |
| Directory listing | `sdls` | ✅ |
| File read | `sdread` | ✅ |
| File delete | `sdrm` | ✅ |

### 🖥️ System / Diagnostics
| Feature | Command | Status |
|---------|---------|--------|
| Linux-style shell interface | — | ✅ |
| Device info — IP, MAC, battery | `info` | ✅ |
| GPS coordinate test (T-Deck Plus) | `gpstest` | ✅ |
| Speaker / I2S audio test | `spktest` | ✅ |
| Matrix rain animation | `MATRIX` | ✅ |

---

## Hardware

| Component | Details |
|-----------|---------|
| Devices | LilyGo T-Deck · LilyGo T-Deck Plus |
| MCU | ESP32-S3 (16 MB flash, PSRAM) |
| Display | 320×240 ST7789 TFT |
| Input | Physical QWERTY keyboard (PS/2 over I2C) |
| Radio | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |

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

## Anti-Tracking Scanner — How It Works

```
CMD> trackme
```

Continuously scans BLE advertisements and WiFi probe requests. Tracks each device across multiple sightings using a 3-gate algorithm:

- **Gate 1** — signature match: known tracker IDs (AirTag, Tile, Samsung SmartTag, Chipolo, Google) with payload-byte disambiguation (prevents iPhones from triggering the Apple AirTag signature)
- **Gate 2** — behaviour scoring: time seen, sighting count, RSSI variance, gap-return events
- **Gate 3** — confirmation: 5+ min seen, 3+ distinct gap-return windows, strong RSSI, no traffic-jam crowd guard

Alert levels: **NOTICE** (yellow) → **WARNING** (orange, 1 beep) → **ALERT** (red, 3 beeps every 30s). Alerts logged to `/logs/trackme.txt`.

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
- [x] Anti-tracking scanner (BLE + WiFi, 3-gate algorithm)
- [x] SD card file manager
- [x] T-Deck Plus GPS + speaker diagnostics
- [ ] WPA handshake capture + PCAP export
- [ ] BLE GATT enumeration
- [ ] Banner grabber
- [ ] LoRa frequency scanner + packet logger
- [ ] BadUSB / HID keystroke injection (DuckyScript)

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
- [Bruce Firmware](https://github.com/pr3y/Bruce) — offensive ESP32 toolkit (Evil Twin HTML templates adapted with permission)
- [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) — WiFi/BT attack suite
- [LilyGo T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) — hardware reference

---

<p align="center">
  <i>T-Rex — your network never saw it coming.</i>
</p>
