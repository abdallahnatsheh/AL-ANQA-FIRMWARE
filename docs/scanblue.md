---
title: Scan BLE
parent: Bluetooth
nav_order: 1
---

# Scan BLE

## `scanblue` / `sbl` — BLE Device Scanner

```
CMD> sbl
```

---

## How It Works

`sbl` performs a **passive BLE advertisement scan** — the radio listens for advertisement packets that BLE devices broadcast periodically. No pairing or connection is made. Every device found is added to a numbered list.

The scan runs for a fixed window (a few seconds), then displays the results. Devices that don't broadcast during the window won't appear — re-scan if you're looking for a specific device.

Each row in the result table shows:

| Column | Meaning |
|--------|---------|
| Index `#` | Device number — used by `bleinfo` and `trackme` |
| Name | Device name from the advertisement packet, or `[unknown]` if not advertised |
| MAC | 48-bit address. Random addresses (type 1) rotate periodically |
| RSSI | Signal strength in dBm — closer = higher (less negative) value |

---

## Keys

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

Results are cached — use `show ble` to view again without rescanning.

---

## Notes

- **`[unknown]` name** — the device didn't include its name in the advertisement payload. `bleinfo` can still connect and read its GATT tree.
- **Random MACs** — iPhones, Android phones, and modern laptops rotate their BLE MAC every ~15 minutes. The same physical device may appear with a different MAC on the next scan.
- **Index usage** — the index from `sbl` is used directly by `bi` (BLE Info) and `tm` (trackme): `bi 3` connects to device at index 3.
- **Antenna sharing** — BLE and WiFi share one antenna. Stop any active WiFi scan or attack before running `sbl`.
