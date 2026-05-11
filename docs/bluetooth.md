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
