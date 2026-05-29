---
title: Bluetooth
nav_order: 4
---

# Bluetooth Tools

---

## `scanblue` / `sbl` — BLE Device Scanner

```
CMD> scanblue
CMD> sbl
```

Scans for nearby Bluetooth Low Energy (BLE) devices and displays a paginated table with device name, MAC address, and RSSI.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

The scan result is cached — use `show ble` to view it again without rescanning.

---

## `trackme` / `tm` — Anti-Tracking Detector

```
CMD> trackme
CMD> tm
CMD> tm silent    # no speaker alerts
```

Passive BLE + WiFi probe scanner that detects devices that may be physically following you. Uses a 60s baseline learning period, 3-gate confirmation pipeline, and Kalman-filtered RSSI to minimize false positives.

**T-Deck Plus tip:** Run `gpson` before `trackme` — GPS movement data significantly improves detection accuracy and eliminates most false positives.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `w` | Whitelist selected device (saves to SD) |
| `s` | Save session log to SD |
| `q` | Quit |

| Alert level | Meaning | Speaker |
|-------------|---------|---------|
| NOTICE | Suspicious device, Gate 3 not yet confirmed | Silent |
| WARNING | Gate 3 confirmed, score 60–79 | 1 beep / 30s |
| ALERT | Gate 3 confirmed, known tracker or score ≥ 80 | 3 beeps / 30s |

→ [Full guide: How trackme works](trackme.md)

---

## `fastpair` / `fp` — Google Fast Pair Attack Suite

```
CMD> fp             # interactive menu
CMD> fp scan        # BLE scan for nearby Fast Pair devices
CMD> fp spam        # flood Android pairing popups
CMD> fp h <index>   # GATT hijack a specific device
CMD> fp h all       # attempt GATT hijack on all scanned devices
```

Three-mode attack suite targeting Google Fast Pair.

### `fp scan`

Scans for BLE devices advertising Google Fast Pair service data (UUID `0xFE2C`) for 5 seconds. Shows a live found-count during the scan, then a paginated table with device index, model ID, name, MAC, and RSSI.

| Key | Action |
|-----|--------|
| `h <n>` | Attempt GATT hijack on device at index n |
| `s` | Switch to spam mode |
| `q` | Quit / abort scan early |

### `fp spam`

Floods the air with Fast Pair advertisement packets. Each cycle uses a freshly randomized MAC and advertises for 10 seconds — enough time for Android to display the pairing popup. Nearby Android devices show Google Fast Pair pairing popups.

Press `q` to stop.

### `fp h <index>` — GATT Hijack (WhisperPair)

Connects to the target device via GATT and reads its anti-spoofing public key. The key is cached in `/fastpair_keys.csv` and logged to `/logs/fastpair.csv`. Put the target in pairing mode for best results — devices in pairing mode expose the anti-spoofing key directly.

---

## `blespam` / `bs` — BLE Notification Spam

```
CMD> bs             # cycle all vendors
CMD> bs apple       # iOS Continuity popups (AirPods, etc.)
CMD> bs android     # Google Fast Pair (Android popup)
CMD> bs ms          # Windows Swift Pair popup
CMD> bs samsung     # Samsung Galaxy accessory data flood
CMD> bs all         # cycle through all four vendors
```

Floods the air with BLE advertisements that trigger pairing or notification popups on nearby devices. Each advertisement cycle randomizes the source MAC to bypass filters. The Android vendor (`bs android`) advertises for 10 seconds per cycle — Android needs several seconds to process and display Fast Pair popups.

| Vendor | What appears on target |
|--------|----------------------|
| `apple` | iOS Continuity — AirPods / Apple TV pairing popup |
| `android` | Google Fast Pair — "Headphones found nearby" popup |
| `ms` | Windows — Swift Pair notification toast |
| `samsung` | Samsung Galaxy accessory pairing popup |

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous vendor |
| `q` | Stop |
