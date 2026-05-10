---
name: macwatch feature spec
description: WiFi probe + BLE MAC watchlist with proximity alert
type: project
---

Command: `macwatch/mw` — watch for specific MACs, alert when in range.

**Sub-modes:**
- `mw add` — scan WiFi probes + BLE, pick MAC, assign label → saved to `/watchlist.csv`
- `mw` / `mw watch` — continuous scan, beep + display when watched MAC seen

**Watchlist:** `/watchlist.csv` format: `MAC,label,WIFI|BT`. RAM fallback (3 entries) if no SD.

**WiFi:** promiscuous probe-request sniff (subtype 4), src MAC at frame offset 10.
**BLE:** `BLEScan` same as trackme/scanblue. Static random addresses are stable.
**Alert:** I2S 3-beep (same as trackme ALERT), show label + RSSI + radio type.
**Keys:** `[a]` add mode, `[l]` list watchlist, `[q]` quit.

Note: modern phones randomize probe MACs — works best on laptops, IoT, older phones.
