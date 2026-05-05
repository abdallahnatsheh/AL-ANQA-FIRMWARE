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

Inspired by [Bruce](https://github.com/BruceDevices/firmware) and [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder), but with its own identity: no menus, no GUI — just a blinking cursor and a command line.

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
| Evil Portal / Captive Portal | 🔨 WIP |
| WPA Handshake capture | 🔨 WIP |

### 🌐 Network Recon
| Feature | Status |
|---------|--------|
| ARP scan — LAN host discovery (paginated table) | ✅ |
| Port scan — 4× parallel, 150 ms timeout (table UI) | ✅ |
| Top-26 ports scan (nmap-style common ports) | ✅ |
| Ping — ICMP with RTT + packet loss summary | ✅ |
| Banner grabber | 🔨 WIP |
| DNS enumeration | 🔨 WIP |

### 🔵 Bluetooth
| Feature | Status |
|---------|--------|
| BLE device scanner (paginated, RSSI, name) | ✅ |
| BLE GATT enumeration | 🔨 WIP |
| Bluetooth Classic scan | 🔨 WIP |

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
| Linux-style shell interface | ✅ |
| Device info (IP, MAC, battery) | ✅ |
| Matrix rain animation | ✅ |
| T-Deck Plus display support | ✅ |

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
| `scanwifi` | `sw` | — | Scan WiFi networks (paginated table) |
| `connectwifi` | `cw` | `<index>` | Connect to network from last scan |
| `clearwifi` | `clrw` | — | Erase all saved WiFi credentials |
| `wifimon` | `wm` | `[ch]` | Monitor mode — channel 1-13, 0=hop |
| `deauth` | `da` | `<bssid> [ch] [client]` | Deauth attack via raw 802.11 |
| `netdiscover` | `nd` | — | ARP scan local /24 (paginated, u=rescan) |
| `portscan` | `ps` | `<ip\|#> <start> <end>` | TCP port scan, 4× parallel |
| `topscan` | `ts` | `<ip\|#>` | Scan top 26 common ports |
| `ping` | `pg` | `<ip\|hostname>` | ICMP ping with RTT + loss stats |
| `scanblue` | `sbl` | — | BLE device scan (paginated) |
| `sdinfo` | `sdi` | — | SD card type and capacity |
| `sdls` | `ls` | `[path]` | List SD card directory |
| `sdread` | `sdr` | `<path>` | Read file from SD card |
| `sdrm` | `srm` | `<path>` | Delete file from SD card |
| `MATRIX` | `matrix` | — | Matrix rain animation (q to exit) |

> **ARP index shortcut:** run `nd` first, then use the host index (e.g. `ts 3`) instead of typing an IP address in `ps`/`ts`.

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
- [x] Deauth attack
- [x] ARP LAN discovery (batch scan, paginated table)
- [x] Parallel TCP port scan (4× tasks, 150 ms timeout)
- [x] Top-26 ports scan
- [x] ICMP ping with RTT stats
- [x] BLE scanner
- [x] SD card file manager
- [x] T-Deck Plus support
- [ ] WPA handshake capture + PCAP export
- [ ] Evil Portal / captive portal
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
- [Bruce Firmware](https://github.com/BruceDevices/firmware) — offensive ESP32 toolkit
- [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) — WiFi/BT attack suite
- [LilyGo T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) — hardware reference

---

<p align="center">
  <i>T-Rex — your network never saw it coming.</i>
</p>
