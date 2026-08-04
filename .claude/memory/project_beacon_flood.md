---
name: beacon-flood-implemented
description: BeaconFlood command — raw 802.11 beacon injection, 5 modes, interactive picker
type: project
---

## Command
`beaconflood` / `bf` — interactive mode picker on bare `bf`

## Modes
| Key | Mode | What |
|-----|------|------|
| 1 | list | 40 built-in funny SSIDs (PROGMEM) |
| 2 | rickroll | Never Gonna Give You Up lyrics |
| 3 | seq | base + counter (1–9999); Enter=confirm |
| 4 | file | /wordlist_beacons.txt from SD |
| 5 | clone | pick from last scanwifi result by index |

## Key technical facts
- Dynamic frame builder — no fixed template; IEs packed tight
- Random LA-MAC per beacon: `(mac[0] & 0xFE) | 0x02`
- `esp_wifi_80211_tx(WIFI_IF_AP, pkt, frameLen, false)`
- Channel hops every 20 beacons: 1→6→11→2→7→12→3→8→13→4→9→5→10
- Clone mode: locked to target channel; appends ZWS chars to SSID variants
- GDMA rule: SD file opened BEFORE `startInjection()`, closed AFTER `stopInjection()`
- WiFi: APSTA + softAP + promiscuous. Teardown: WIFI_STA
- Cannot run simultaneously with wguard (both need promiscuous)

## Files
- `t-deck-cli/beacon_flood.cpp` / `beacon_flood.h`
- `docs/beacon-flood.md`
