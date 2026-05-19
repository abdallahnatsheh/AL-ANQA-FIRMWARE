---
name: macwatch feature idea
description: WiFi probe + BLE MAC watchlist — scan, label, alert when watched MAC enters range
type: project
---

`macwatch/mw` — two modes:
1. **Add** (`mw add`): scan WiFi probes + BLE, show MACs+RSSI+name, user picks + labels → `/watchlist.csv` (`MAC,label,WIFI|BT`)
2. **Watch** (`mw`): load watchlist, scan continuously, I2S 3-beep + display when watched MAC seen

**Storage:** SD → `/watchlist.csv` unlimited; no SD → RAM-only max 3, lost on reboot.

**Tech notes:**
- WiFi: promiscuous MGMT, probe request subtype 4, MAC at offset 10 (same as trackme). Modern phones randomize probe MACs — works best for laptops/IoT/older phones.
- BLE: static random addresses are stable per device; public addresses always stable.
- Keys: `[a]`=add `[l]`=list `[q]`=quit. Log sightings to `/logs/macwatch.csv`.
