---
title: BLE Advertisement Monitor
parent: Bluetooth
nav_order: 8
---

# BLE Advertisement Monitor — `bmon` / `bm`
{: .no_toc }

A passive BLE advertisement sniffer. It listens to the beacons every nearby Bluetooth Low Energy device broadcasts and decodes the common formats — without connecting to anything.

1. TOC
{:toc}

---

## What it decodes

| Format | Shown |
|--------|-------|
| **iBeacon** (Apple, MFR `0x004C`) | UUID · Major · Minor · TxPower |
| **Eddystone-UID / URL / TLM** (service `0xFEAA`) | Namespace/instance · URL · adv-count + uptime |
| **Device name** | Cleartext advertised name |
| **Unknown manufacturer** | Company ID + first bytes of the payload |

Each device also shows its **address type** — `pub` (public MAC) or `rnd` (random/rotating MAC) — and a live **RSSI** and **sighting count**.

---

## Usage

```
CMD> bmon           # start sniffing — devices appear newest-first
```

| Key / control | Action |
|---------------|--------|
| Trackball **up / down** | Select a row — the detail pane shows its full decoded data |
| `a` / `l` | Previous / next page (7 rows per page) |
| `s` | Toggle SD logging on/off |
| `q` | Quit |

The bottom pane shows the **full** decoded payload for the selected device (full iBeacon UUID, full Eddystone namespace, TLM uptime, raw manufacturer hex) — more than fits in the table row.

---

## Logging

Press `s` to log to `/apps/bmon/NNN.csv` (sequential, never overwritten). Entries are de-duplicated per MAC every 60 seconds.

```
timestamp,first_seen,mac,addr_type,type,rssi,sightings,info,extended
```

- `info` — the short string shown on screen
- `extended` — the full decoded data (full UUIDs, TLM counters, complete MFR hex)

Timestamps come from `ClockManager` (GPS/NTP); if the clock isn't set yet they fall back to `@<ms since boot>`.

---

## Examples

**Find an iBeacon's UUID/Major/Minor** — start `bmon`, select the row tagged `iBeacon`, read the detail pane.

**Audit what's leaking a static MAC** — rows marked `pub` broadcast a fixed address (trackable); `rnd` rotate theirs. Log with `s` and open the CSV on a PC.

---

## See also

- [Scan BLE](scanblue) — active scan (connects/queries), vs `bmon`'s passive listen
- [Tracking Detection](trackme) — flags trackers (AirTag/Tile) hiding in these advertisements
- [BLE Info](bleinfo) — connect to a device and enumerate its GATT services
