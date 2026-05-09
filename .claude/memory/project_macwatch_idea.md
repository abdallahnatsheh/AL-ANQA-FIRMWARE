---
name: macwatch feature idea
description: WiFi + BLE MAC watchlist command — scan, add known MACs, alert when they enter range
type: project
originSessionId: d9b00608-d6fa-4df3-9c52-9ac8aaf47cba
---
## Feature idea: `macwatch` / `mw`

Passive WiFi probe + BLE scanner that watches for specific MAC addresses and alerts when they come within range. Good for proximity awareness (knowing when a specific person/device is approaching).

---

## How it should work

**Two sub-modes:**

### 1. Add to watchlist (`mw add` or interactive)
- Scan WiFi probe requests (promiscuous mode) + BLE advertisements
- Show all unique MACs seen with RSSI and optional BLE device name
- User picks a MAC → prompted to give it a label (e.g. "Boss laptop", "Boss phone")
- Saved to `/watchlist.csv` on SD card: `MAC,label,addedTimestamp`

### 2. Watch mode (`mw` or `mw watch`)
- Load watchlist from SD
- Continuously scan BLE + WiFi probes in the background
- If a watched MAC is seen:
  - Display name + RSSI + "IN RANGE" on screen
  - Beep alert (I2S tone, same as trackme)
  - Log sighting to `/logs/macwatch.txt` with timestamp
- RSSI threshold configurable (e.g. > −80 dBm = close range alert)
- Distance indicator: very close / close / far based on RSSI bands

---

## Technical notes

**WiFi probe sniff:** same promiscuous approach as trackme — `esp_wifi_set_promiscuous()` + MGMT filter, probe request subtype 4, MAC at frame offset 10. Note: modern phones randomise probe request MACs (MAC randomisation), so this works best for laptops, IoT, older phones with randomisation off, or devices actively probing for saved SSIDs.

**BLE scan:** same `BLEScan` as trackme/scanblue. MAC address from `dev.getAddress().toString()`. BLE also has address randomisation — static random addresses are stable per device, so those are trackable. Public addresses are always stable.

**Watchlist storage (dual mode):**
- **SD present:** save to `/watchlist.csv`, unlimited entries. Format: `XX:XX:XX:XX:XX:XX,Label,WIFI|BT`
- **No SD:** RAM-only fallback, max 3 entries. Warn user on add when limit reached. Lost on reboot.

**Radio source tracking:** each watchlist entry records whether the MAC was first seen via `WIFI` (probe request) or `BT` (BLE advertisement). Shown in watch mode and scan list so user knows which radio to expect the alert from.

**Alert:** I2S beep (same pattern as trackme ALERT — 3 beeps). Show label + RSSI + radio type (WiFi/BT) on display. Continuous monitoring with 2s refresh.

**Keys while watching:**
- `[a]` add mode — scan and add new MAC to watchlist
- `[l]` list watchlist
- `[q]` quit

---

## Why this is useful
- Know when a specific device (laptop, phone) enters range before they arrive
- Home/office proximity awareness without needing any app on the target device
- Passive — target device has no idea it's being detected
