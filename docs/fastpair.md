---
title: Fast Pair
parent: Bluetooth
nav_order: 4
---

# Fast Pair

## `fastpair` / `fp` — Google Fast Pair Attack Suite

```
CMD> fp              # interactive menu
CMD> fp scan         # BLE scan for Fast Pair devices
CMD> fp spam         # flood Android pairing popups
CMD> fp h <index>    # GATT hijack a specific device
CMD> fp h all        # hijack all scanned devices
```

### `fp scan`

Scans for BLE devices advertising the Fast Pair service UUID (`0xFE2C`) for 5 seconds. Shows index, model ID, name, MAC, and RSSI.

| Key | Action |
|-----|--------|
| `h <n>` | GATT hijack device at index n |
| `s` | Switch to spam mode |
| `q` | Quit / abort scan early |

### `fp spam`

Floods the air with Fast Pair advertisement packets. Each cycle uses a freshly randomized MAC and advertises for 10 seconds. Nearby Android devices show Google Fast Pair pairing popups.

Press `q` to stop.

### `fp h <index>` — WhisperPair

Connects to the target device via GATT and reads its anti-spoofing public key. The key is cached to `/fastpair_keys.csv` and logged to `/logs/fastpair.csv`.

> Put the target device in pairing mode for best results — devices in pairing mode expose the anti-spoofing key directly.
