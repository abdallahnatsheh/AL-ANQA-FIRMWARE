<p align="center">
  <img src="images/banner.png" width="420"/>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-LilyGo%20T--Deck-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/language-C%2FC%2B%2B-yellow?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-GPL--3.0-red?style=flat-square"/>
  <img src="https://img.shields.io/badge/status-active%20development-orange?style=flat-square"/>
  <img src="https://img.shields.io/badge/device-ESP32--S3-green?style=flat-square"/>
</p>

<p align="center">
  <b>Pentesting CLI firmware for the LilyGo T-Deck — a Linux shell in your pocket.</b><br/>
  <i>Scan. Attack. Repeat.</i>
</p>

---

## What is T-Rex?

T-Rex is an offensive security toolkit firmware for the **LilyGo T-Deck**, built around a raw terminal-first CLI that feels like a Linux shell. Type commands, run attacks, scan networks — all from a pocket-sized ESP32 device with a physical keyboard.

Inspired by [Bruce](https://github.com/BruceDevices/firmware) and [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder), but with its own identity: no menus, no GUI — just a blinking cursor and a command line.

---

## Features

### 📡 WiFi
| Feature | Status |
|---------|--------|
| Network scanner | ✅ |
| Connect to network | ✅ |
| Deauth attack | 🔨 WIP |
| Evil Portal / Captive Portal | 🔨 WIP |
| WPA Handshake capture | 🔨 WIP |
| PCAP logging to SD | 🔨 WIP |

### 🔵 Bluetooth
| Feature | Status |
|---------|--------|
| BT device scanner | ✅ |
| BLE scanner | 🔨 WIP |
| BLE spam | 🔨 WIP |

### 🌐 Network
| Feature | Status |
|---------|--------|
| LAN device discovery (ping scan) | ✅ |
| Port scanner | ⚗️ Experimental |
| ARP scan | 🔨 WIP |
| Banner grabber | 🔨 WIP |
| DNS enumeration | 🔨 WIP |

### 📻 LoRa
| Feature | Status |
|---------|--------|
| Frequency scanner | 🔨 WIP |
| Packet logger | 🔨 WIP |

### 🖥️ System
| Feature | Status |
|---------|--------|
| Linux-style shell interface | ✅ |
| Device info screen | ✅ |
| Matrix rain | ✅ |

---

## Hardware

| Component | Details |
|-----------|---------|
| Device | LilyGo T-Deck |
| MCU | ESP32-S3 |
| Display | 320×240 TFT |
| Input | Physical QWERTY keyboard |
| Radio | WiFi · Bluetooth · LoRa (SX1262) |
| Trackball | Navigation |

---

## Getting Started

### Requirements
- [VSCode](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/)
- LilyGo T-Deck hardware

### Build & Flash
```bash
git clone https://github.com/abdallahnatsheh/T-Rex
cd T-Rex
# Open in VSCode with PlatformIO
# Click the Upload (→) button
```

> **Can't upload?** Hold the trackball button, plug in USB, then try again — this forces the device into download mode.

---

## Commands

```
root@t-rex:~$ help
```

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `wifi scan` | Scan for nearby WiFi networks |
| `wifi connect <ssid>` | Connect to a network |
| `wifi deauth <bssid>` | Deauth attack *(WIP)* |
| `bt scan` | Scan for Bluetooth devices |
| `ble scan` | Scan for BLE devices *(WIP)* |
| `netscan` | Discover devices on the local network |
| `portscan <ip>` | Scan ports on a target *(experimental)* |
| `arpscan` | ARP-based LAN discovery *(WIP)* |
| `info` | Show device info |
| `matrix` | Matrix rain animation |
| `clear` | Clear the terminal |

---

## Roadmap

- [x] WiFi scanner & connect
- [x] Bluetooth scanner
- [x] LAN ping scan
- [x] Port scanner (experimental)
- [x] Device info screen
- [x] Matrix animation
- [ ] Deauth attack
- [ ] WPA handshake capture + PCAP export
- [ ] Evil Portal
- [ ] ARP scan
- [ ] BLE scanner & spam
- [ ] LoRa frequency scanner
- [ ] LoRa packet logger
- [ ] Banner grabber
- [ ] DNS enumeration
- [ ] Saved credentials manager

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
  <i>T-Rex — your network never saw it coming. 🦖</i>
</p>
