---
name: wguard-implemented-reference
description: WGuard WiFi IDS — complete implementation reference for future changes
metadata:
  node_type: memory
  status: implemented
---

# wguard — WiFi Intrusion Detection System (IMPLEMENTED)

## Commands
`wg <index|bssid> [ch]` — interactive foreground session  
`wg <index|bssid> [ch] bg` — background mode (popup alerts while using other commands)  
`wg stop` — stop background session  
`wg view` — enter live view of running background session

## Detection Threats

| Subtype | Threat | Threshold | Level |
|---|---|---|---|
| 12 broadcast | BCAST DEAUTH | 5/5s per src MAC | WARNING |
| 12 targeted | DEAUTH storm | 15/5s per src MAC | CRITICAL |
| 8 | EVIL TWIN+DTH | foreign same-SSID AP + deauths, RSSI > -82 dBm | WARNING |
| 8 | FOREIGN AP | same-SSID AP, no deauths yet (pending upgrade) | INFO |
| 8 | CO-AP | same OUI as target (mesh/enterprise node) | INFO |
| 0xFF | HANDSHAKE harvest | deauth burst ≥3 + EAPOL M1/M2/M3/M4 | CRITICAL |
| 0xFD | BSSID CLONED | BSS timestamp backward jump (two radios, same MAC) | CRITICAL |
| 0xFE | BEACON FLOOD | 100+ unique BSSIDs / 30s window (ISR-level) | WARNING |
| 11 | AUTH flood | 32+ unique MACs / 10s | WARNING |
| 4 | PROBE storm | 50 probes/5s from one MAC | WARNING |
| 0 | RAPID ASSOC (PMKID?) | 5 rapid assoc requests/5s from one MAC — INFO/silent, DFIR hint only | INFO |
| 11 | AUTH FLOOD | counted by `_threatAuthFlood` — in THREATS tally | WARNING |
| 4 | PROBE STORM | counted by `_threatProbeStorm` — in THREATS tally | WARNING |
| 5 | KARMA | same BSSID probe-responds with 3+ different SSIDs / 60s | WARNING |

## Key Design Decisions

### Evil Twin Detection (two-tier)
- **Pending list** `_pendingForeign[4]`: AP seen with no/weak deauths → INFO event, wait for deauths
- **Warning dedup** `_evilTwinSeen[8]`: BSSIDs that already fired WARNING
- **Upgrade trigger** (case 12): when `_deauthBurstCount == 3`, promote pending APs that are:
  - Fresh (< 60s old) AND signal strong (> -82 dBm beacon RSSI)
  - RSSI filter prevents extender false positives (far-away co-SSIDs at -90+ dBm)
- **Expiry**: `_evilTwinSeenTs[8]` — if no beacon seen for 3s, entry expires → re-detection on restart ✓
- **Direct path** (case 8): AP appears while deauths already active + RSSI > -82 → immediate WARNING
- Evil twin SSID check is in ISR: only enqueues if SSID exactly matches monitored SSID

### Rate Limiting (prevents spam)
- BCAST DEAUTH: `lastFired` per src MAC in `_bcastCtr[]` — fires at most once per 30s
- Targeted DEAUTH storm: `lastFired` per src MAC in `_deauthCtr[]` — fires at most once per 30s
- HANDSHAKE harvest: `_harvestFiredTs` — fires at most once per 30s
- Separate `_bcastCtr[]` and `_deauthCtr[]` — bcast and targeted deauths don't pollute each other's counters

### Notification Throttle
- `notifyThrottled(level, now)` — prevents sound spam during active attacks
- WARNING: max 1 sound per 10s · ALERT: max 1 sound per 5s
- Background mode: `pollBackground()` coalesces via `_evHead != _lastBgHead` check

### Session Files
- Path: `/logs/wguard/NNN.csv` — scans SD for highest existing N, always creates new file (never overwrites)
- Session header: `# SESSION N  wguard "SSID"  bssid=XX:XX:XX:XX:XX:XX  chN  uptime=Ns`
- CSV columns: `time,severity,rssi_dbm,message,detail`
- **`detail` column is log-only DFIR data** — never shown on screen; blank for events that don't apply
  - Targeted deauth: `dst=AA:BB:CC:DD:EE:FF Dth=42 v=Alfa` (dst MAC + frame count + OUI vendor)
  - Broadcast deauth: `Dth=42 v=LA-MAC`
  - Evil twin: `Dth=5 v=RPi`
  - Handshake harvest: `Dth=20 EAP=3`
  - Probe storm / rapid assoc: `Prb=50 v=Espressif` / `dst=... v=LA-MAC`
  - Beacon flood: `Bcn=150`
  - Auth flood: `Ath=32`
  - BSSID clone / Karma / CO-AP / Foreign AP: `v=<vendor>`
- **Timestamps are session-relative** — `(e.ts - _sessionStartMs) / 1000`. Display clock also session-relative.
- Each save block writes only **new events since last save** — tracked by `_savedEvCount` (uint8_t). No duplicate events across blocks.
- Per-save block header: stats snapshot `Bcn= Prb= Ath= Dth= EAP=`
- Session footer: `# SESSION END  total=N events  maxSev=X  chN  duration=Xm Xs  Bcn= Prb= Ath= Dth= EAP=`
- Threat tally: `# THREATS  evil_twin=N  deauth_storm=N  karma=N  clone=N  beacon_flood=N`

### Save Types (four paths, all append, GDMA-safe)
| Type | Trigger | Ring cleared? | `_savedEvCount` |
|---|---|---|---|
| `# AUTO-SAVE N` | Ring hits 128 events | YES | Reset to 0 with ring |
| `# CHECKPOINT N` | Every 2 minutes — skipped if nothing new | NO | Set to `_evCount` |
| `# MANUAL SAVE N` | `[s]` key — skipped if nothing new; footer shows `Saved N events` or `Nothing new to save` | NO | Set to `_evCount` |
| `# FINAL SAVE N` | Session end (quit/stop) — omitted if nothing new | NO | Set to `_evCount` |

All SD writes pause promiscuous (`s_active=false`), write, then resume — GDMA rule compliance.

### Save Feedback (interactive mode)
- `[s]` key sets `saveNote` + `saveNoteUntil` local vars in `runUI()`
- Footer row redrawn every 500ms refresh: shows note in green for 2.5s, then reverts to key hint
- No `addEvent()` calls for save events — ring stays clean for threat events only

### Background Mode
- `beginBackground()` / `stopBackground()` / `pollBackground()` — called from main loop
- `pollBackground()` drains ring every 200ms, runs same `processFrame()` as interactive
- Auto-save and checkpoint both wired in `pollBackground()`
- Popup bar at y=222 for sev>0 events; shield icon in status bar reflects `_maxSev`
- `enterView()` temporarily sets `_bgMode=false` so notifications fire normally during live view

### Cloned BSSID Detection
- ISR tracks BSS timestamp (8-byte field at offset 24 in beacon frame) from target AP
- If timestamp goes backward → two radios transmitting on same BSSID → enqueue 0xFD
- `s_lastTargetTs` / `s_targetTsSeen` are ISR-level volatile statics
- `_cloneFired` bool (first-ever detection fires instantly) + `_cloneFiredTs` uint32_t (60s cooldown after first). Stopped-then-relaunched clone re-triggers after 60s silence.

### Association Request Tracking (DFIR hint)
- Dedicated `_assocCtr[WG_CTR_MAX]` — isolated from `_deauthCtr` to prevent victim reconnections inflating deauth storm counter
- Fires INFO (silent, no alert) at 5 assoc requests/5s from same MAC
- Message: `RAPID ASSOC (PMKID?)` — written to CSV for DFIR correlation, never alerts the user
- `WG_BSSID_MAX = 128` — beacon flood ISR array sized to reach the 100-AP trigger threshold

## WgFrame subtypes used internally
- `0xFF` = EAPOL data frame
- `0xFE` = beacon flood trigger (from ISR)
- `0xFD` = cloned BSSID / BSS timestamp backward jump
- `8` = evil twin beacon (same SSID, foreign BSSID)
- `5` = probe response (Karma detection)

### OUI Vendor Lookup (`lookupOui()`)
- Static function, called from `processFrame()` only (not ISR-safe, not needed there)
- LA-MAC check first: `if (mac[0] & 0x02) return "LA-MAC"` — always spoofed
- Table covers: Alfa, Hak5, Realtek, RPi (6 OUI blocks), Espressif (22 OUI blocks including 7C:87:CE for ESP32-S3)
- Returns `nullptr` for unknown OUIs — caller checks and omits the `v=` field from detail string
- No lag — 34 entries, linear scan, runs once per event (not per frame)

## Ring / Event structures
```cpp
struct WgEvent { uint32_t ts; uint8_t sev; int8_t rssi; char msg[44]; char detail[48]; };
WG_EVENT_MAX = 128   // ring size
WG_CTR_MAX   = 16   // per-MAC counter table size
WG_BSSID_MAX = 128  // ISR beacon flood BSSID tracker (needs 128 to reach 100-AP trigger)
```

## WgCounter (per-MAC rolling window)
```cpp
struct WgCounter { uint8_t mac[6]; uint32_t count; uint32_t winStart; uint32_t lastFired; };
```
`lastFired` = cooldown anchor — both bcast and targeted deauth counters use it to rate-limit events.
