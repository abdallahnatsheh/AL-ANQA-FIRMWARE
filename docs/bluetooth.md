---
title: Bluetooth
lang: en
nav_order: 8
has_children: true
---

# Bluetooth

BLE scanning, monitoring, tracker detection, and using the T-Deck as a Bluetooth keyboard. Each tool has its own page (left) with full detail.

> BLE and WiFi share one antenna — stop any active WiFi scan/attack before running a Bluetooth tool.

---

## Scan & monitor

| Tool | Command | What it does |
|------|---------|--------------|
| [Scan BLE](scanblue) | `sbl` | Active scan of nearby BLE devices |
| [BLE Advertisement Monitor](bmon) | `bmon` | **Passive** advertisement sniffer — decodes iBeacon / Eddystone / names, logs to SD |
| [BLE Info](bleinfo) | `bi` | Connect + enumerate GATT; read/write, fuzz, abuse read-hammer, security audit |

## Detection & watch

| Tool | Command | What it does |
|------|---------|--------------|
| [Tracking Detection](trackme) | `tm` | Detect AirTag/Tile-style trackers physically following you |
| [MAC Watch](macwatch) | `macwatch` | Watchlist of MACs (WiFi + BLE) with proximity alerts |

## Attacks & HID

| Tool | Command | What it does |
|------|---------|--------------|
| [Fast Pair](fastpair) | `fp` | Google Fast Pair attack suite |
| [BLE Spam](blespam) | `bs` | BLE advertisement spam (pairing pop-ups) |
| [Buddy](buddy) | `bd` | Desktop BLE remote / pet |
| [BT Keyboard](btkbd) | `bk` | Use the T-Deck as a BLE keyboard + mouse |

---

## Typical flow

```
CMD> sbl           # scan for devices, note an index
CMD> bi 3          # enumerate device #3's GATT services
CMD> tm            # or: watch for trackers following you
```
