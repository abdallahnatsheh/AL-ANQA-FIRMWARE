---
title: Hidden SSID
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 3
---

# Hidden SSID

## `hiddenssid` / `hs`

Reveals the SSID of a hidden network by forcing clients to re-associate.

```
CMD> hs <index|bssid> [channel] [silent]
CMD> hs 4
CMD> hs AA:BB:CC:DD:EE:FF 11
CMD> hs 4 11 silent
```

### How it works

1. Sends deauth bursts every 3 seconds to force clients off the AP
2. Sniffs for probe responses (subtype 5) and association requests (subtype 0) matching the target BSSID
3. Extracts the SSID from the captured frame

### On find

- Plays a two-tone beep (unless `silent`)
- Displays the SSID on screen
- Saves `BSSID,SSID,ch` to `/apps/hiddenssid/found.csv`
- The SSID appears as `~name` in cyan on the next `sw` scan

Press `q` to stop.
