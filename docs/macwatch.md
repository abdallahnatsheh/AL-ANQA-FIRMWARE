---
title: MAC Watch
parent: Bluetooth
nav_order: 9
---

# MAC Watch — `macwatch` / `mw`
{: .no_toc }

Keep a **watchlist of MAC addresses** and get alerted when any of them comes into range — across **both** WiFi (probe requests) and BLE advertisements. Useful for presence detection: know when a specific phone, tag, or laptop is nearby.

**Own devices / authorized monitoring only.**

1. TOC
{:toc}

---

## Watchlist

The watchlist lives at `/apps/macwatch/watchlist.csv`, one target per line:

```
mac_or_prefix,name,radio,nearRssi
```

| Field | Meaning |
|-------|---------|
| `mac_or_prefix` | Full MAC (`AA:BB:CC:DD:EE:FF`) or a prefix (`AA:BB:CC`) to match a vendor/device family |
| `name` | Friendly label shown in alerts |
| `radio` | `WIFI`, `BT`, or `BOTH` |
| `nearRssi` | Proximity threshold in dBm — alert only when signal is stronger than this (`0` = any distance) |

Example:
```
AA:BB:CC:DD:EE:FF,Alex phone,BOTH,-60
DC:A6:32,Raspberry Pi,WIFI,0
```

---

## Usage

```
CMD> mw              # foreground watch — live list of watched devices + last seen
CMD> mw add          # add a target interactively first, then watch
CMD> mw bg           # run in the background (adds an "MW" badge to the status bar)
CMD> mw stop         # stop the background watcher
```

In the foreground view you see each watchlist entry, whether it's currently **in range**, its RSSI, and which radio spotted it. Press `q` to quit (the background watcher keeps running if you started it with `mw bg`).

---

## Background mode

`mw bg` runs the watcher while you do other things — a green **`MW`** badge appears in the status bar. When a watched MAC appears (and passes its `nearRssi` threshold) you get a notification. It coexists with other background pollers (`wg bg`, `ec bg`, GPS).

Events are logged to `/apps/macwatch/events.csv` (`time, mac, name, radio, rssi`).

---

## Notes & limits

- WiFi matching relies on the target sending **probe requests** — a device that's already connected and idle may not probe often. BLE matching needs the device to be **advertising**.
- MAC randomization defeats matching a full MAC on modern phones. Use a **prefix** (vendor OUI) or pair it with [Tracking Detection](trackme) / [BLE Monitor](bmon) for randomized devices.
- Background HW test still pending.

---

## See also

- [BLE Advertisement Monitor](bmon) — see *all* advertising devices, not just a watchlist
- [WiFi Monitor](wifimon) — the probe-request source on the WiFi side
- [Tracking Detection](trackme) — behavioural tracker detection for randomized MACs
