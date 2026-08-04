---
title: Device Info
lang: en
parent: System
nav_order: 2
---

# Device Info

## `info` / `inf`

```
CMD> info
```

4-page view of device information.

| Page | Content |
|------|---------|
| 1 (SYS) | Chip model, cores, flash size, PSRAM, CPU frequency, heap, uptime |
| 2 (RADIO) | WiFi MAC, BT MAC, IP address, WiFi RSSI |
| 3 (HW) | Battery %, SD card status, LoRa pins, GPS status |
| 4 (ABOUT) | Project name + a scannable **QR code** linking to the [GitHub repo](https://github.com/abdallahnatsheh/AL-ANQA-FIRMWARE) |

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `s` | Replay the boot splash (phoenix) — press any key to return |
| `q` | Quit |
