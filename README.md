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
| 60s baseline — auto-learns your watch, phone, car BT at session start | ✅ |
| Permanent device whitelist (`w` key → `/logs/trackme_known.csv` on SD) | ✅ |
| Movement-aware WiFi detection — no GPS movement = no WiFi alert (no router FP) | ✅ |
| GPS movement path for Gate 3 — 200m followed = confirmed, no 5-min wait | ✅ |
| Behaviour scoring — unknown device analysis | ✅ |
| 30s minimum gap — immune to single missed BLE advertisement | ✅ |
| WARNING only after Gate 3 — score alone can never trigger a beep | ✅ |
| Traffic jam / crowd false positive filtering | ✅ |
| Kalman-filtered RSSI distance estimation | ✅ |
| Custom tracker signatures from SD card | ✅ |
| GPS status display — `GPS:none / GPS:srch / GPS:142m` | ✅ |
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
| `trackme` | `tm` | `[silent]` | Anti-tracking scanner — BLE + WiFi probes ⚠️ experimental |
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

> **⚠️ Experimental:** `trackme` is a best-effort tool and may produce false positives. Radio signals alone cannot prove physical tracking — use results as a general indicator, not as conclusive evidence. GPS movement data (T-Deck Plus only) significantly improves accuracy.

```
CMD> trackme
```

`trackme` is a passive anti-tracking detector. It uses BLE and WiFi probe sniffing to detect devices that may be physically following you. The core problem it solves: your own phone, smartwatch, car Bluetooth, and family members' phones all look identical to a tracker in terms of radio signals — they all follow you perfectly because they are with you. The algorithm is specifically designed to handle this.

### Step 0 — Baseline (first 60 seconds)

When you start `trackme`, the first 60 seconds are a **learning period**. Every device detected during this window — your watch, your phone, car Bluetooth, passengers' phones — is marked as a **companion** and shown in dark grey in the table. Companions are never scored or alerted. Only devices that appear *after* the baseline ends are treated as potential trackers.

The display shows a yellow countdown: `BASELINE 47s — learning your devices...`

**Tip:** Start `trackme` *before* you leave home or get in your car so all your own devices are baselined first.

### Permanent whitelist

If a device slips through (e.g. your phone connected after baseline), press **`w`** to permanently whitelist it. The MAC is saved to `/logs/trackme_known.csv` on the SD card and recognized instantly on all future sessions.

### Detection pipeline

After baseline, every new device passes a 3-gate pipeline:

**Gate 1 — Signature check**
BLE manufacturer data is matched against known tracker signatures. AirTags, Tile, Samsung SmartTag, Chipolo, Eufy, and Pebblebee are identified by company ID, payload byte, and minimum manufacturer data length. Apple non-trackers (iPhones, Macs, AirPods) are classified separately and never scored — matching the company ID alone is not sufficient.

**Gate 2 — Behaviour scoring** *(unknown BLE devices only)*
Unknown devices are scored on: time seen, sighting count, RSSI consistency, and gap-and-return events. A gap only counts if the device was absent for **30+ seconds** — a single missed BLE advertisement does not trigger a false gap-return. Score below 40 → ignored. Score ≥ 40 → Gate 3.

**Gate 3 — Confirmation** *(required for WARNING or ALERT)*
No alert can sound before Gate 3 passes. Two paths:
- **GPS path** (T-Deck Plus): user has moved 200m+ and the device was present throughout with at least one real gap-and-return. Physical movement is direct proof — no time minimum needed.
- **Time path**: device seen for 5+ minutes, still nearby (RSSI > −80 dBm), 3+ gap-returns or sightings, not explained by a crowd scenario.

### WiFi probe detection and GPS movement

WiFi probe sniffing catches phones, laptops — and also **routers**, which actively scan channels and send probe requests. Without GPS, a WiFi-only device (never confirmed by BLE) is capped at NOTICE and cannot trigger a beep. With GPS (T-Deck Plus), a WiFi device can only reach NOTICE if **you have moved 100m+ this session** and the device followed with a real gap-and-return. A router you're sitting near stays at THREAT_NONE because you are not moving.

The status line shows your GPS state: `GPS:none` (no module), `GPS:srch` (searching, NMEA flowing), `GPS:142m` (fix acquired, 142m moved this session).

### Device tiers

| Tier | Max | Purpose |
|------|-----|---------|
| Tier 1 | 20 | Close devices (RSSI > −70) — full 3-gate analysis |
| Tier 2 | 100 | Distant or Apple non-tracker devices — lightweight tracking only |

Apple non-trackers are locked to Tier 2 and never scored.

### Alert levels

| Level | Trigger | Speaker | Display |
|-------|---------|---------|---------|
| NOTICE | Known tracker seen 60s + 2 sightings (not in crowd) | Silent | Yellow row |
| WARNING | BLE device, Gate 3 confirmed, score 60–79 | 1 short beep | Orange row |
| ALERT | Gate 3 confirmed, known tracker OR score ≥ 80 | 3 beeps every 30s | Red row |

**WARNING and ALERT can only fire after Gate 3.** Score alone — no matter how high — cannot trigger a beep before Gate 3 is confirmed.

### Keys

| Key | Action |
|-----|--------|
| `a` / `l` | Previous / next page |
| `w` | Whitelist highest-threat device (saves to SD permanently) |
| `s` | Save current log to SD |
| `q` | Quit |

### Custom tracker signatures

Drop `/signatures.csv` on the SD card to extend the database without reflashing:

```csv
BLE,0x004C,Apple AirTag,HIGH
BLE,0x00D7,Tile Tracker,HIGH
```

Apple non-tracker entries are always appended automatically regardless of SD content.

### Known limitations

- **MAC randomization**: iPhones and Android phones rotate BLE MACs every ~15 min. Commercial trackers (AirTag, Tile) use stable MACs and are detected reliably. Randomized phone MACs reset their history on rotation — this is expected behavior.
- **Single radio**: BLE and WiFi share one physical antenna and cannot scan simultaneously. The 1.5s scan cycle means a brief advertisement may occasionally be missed — the 30s gap minimum makes this harmless.
- **Cold GPS start**: The L76K GPS module needs ~4 minutes for a cold fix. The display shows `GPS:srch` with NMEA byte count so you can confirm the module is alive during the wait. Subsequent starts are faster (warm fix ~30s).

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
