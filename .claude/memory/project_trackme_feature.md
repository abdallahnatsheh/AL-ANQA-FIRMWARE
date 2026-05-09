---
name: trackme feature implementation
description: Anti-tracking scanner — full algorithm design, false positive fixes, GPS integration, companion/whitelist system
type: project
---

## Files
- `t-deck-cli/trackme.h` + `trackme.cpp` — full implementation
- `t-deck-cli/test_gps.cpp` — GPS diagnostic command (`gpstest` / `gt`)
- `t-deck-cli/test_speaker.cpp` — speaker tone test (`spktest` / `st`)

## Algorithm overview

**Radio time-slice:** 1s BLE scan → 0.5s WiFi probe sniff (promiscuous MGMT, subtype 4) → repeat

**Session start — Baseline (60s)**
All devices seen during the first 60 seconds are marked `isCompanion = true` (own phone, watch, car BT, passengers). Companions are never scored. Rationale: own devices look identical to trackers. Only devices appearing *after* baseline are candidates.

**SD whitelist** — `/logs/trackme_known.csv` (MAC,label per line). Loaded at start. `w` key appends the highest-threat non-companion device. Companion flag also set immediately on match.

**Device tiers:**
- Tier 1 (max 20): RSSI > −70 dBm, full gate analysis
- Tier 2 (max 100): RSSI −70 to −85 dBm or Apple non-tracker, MAC+timestamp only
- Tier 2 → Tier 1 promotion: RSSI improves OR sightings ≥ 3
- Below −85 dBm: dropped immediately

**Signature matching:**
- Apple 0x004C requires `mfr[2] == 0x12` (Find My) to distinguish AirTag from iPhone/Mac/AirPods
- Tile 0x00D7, Samsung 0x0075, Chipolo 0x00F0, Google 0x00E0, Eufy 0x006B, Pebblebee 0x0157 match on company ID
- SD `/signatures.csv` overrides builtins; Apple THREAT_NONE entries always appended
- BLE sighting of an existing WiFi-detected MAC clears `isWiFi` flag

**Gap tracking:**
- `markGaps()`: device absent for one cycle → `gapActive = true`
- On return: `distinctWindows++` ONLY if gap lasted ≥ 30 seconds (prevents fake gap from single missed BLE advert)

**Gate 2 (unknown BLE devices):**
- time score: >30min +25, >15min +15, >5min +10
- sightings: ≥5 +20, ≥3 +15
- RSSI variance <20 +35, <50 +20
- gapReturned +25, distinctWindows≥2 +15

**Gate 3 (required for WARNING or ALERT — no beep without it):**
- GPS path: `followDistM >= 200m && distinctWindows >= 1` → passes immediately (physical proof)
- Time path: seenMs ≥ 5min, RSSI > −80, (distinctWindows≥3 OR sightings≥3), crowd guard
- Crowd guard: `crowdAtArrival >= 4 && distinctWindows == 0` → blocks Gate 3

**WiFi-only devices (`isWiFi = true`, never seen via BLE):**
- MAX threat: NOTICE
- Requires: `_gpsMoving` (≥50m total) AND `followDistM >= 100m` AND `distinctWindows >= 1`
- Stationary user → NONE always (eliminates router false positives completely)

**Scoring flow:**
```
isAppleDevice || isCompanion → NONE, skip
isWiFi → GPS movement check → NOTICE max, skip
isKnown:
  Gate3 → ALERT
  else: 60s + 2 sightings + no crowd-jam → NOTICE
unknown:
  Gate2 → score
  Gate3 → ALERT (≥80) or WARNING (60-79)
  else: NOTICE (≥60) or NONE
```

**Key rule: WARNING/ALERT only after Gate 3. Score ≥ 60 alone = NOTICE max.**

**GPS tracking (`#ifdef BOARD_TDECK_PLUS`):**
- `_totalDistM`: accumulates distance in 10m+ steps
- `_gpsMoving`: true once _totalDistM ≥ 50m
- `followDistM` per device: adds GPS step when device not gapping
- Status line: `GPS:none` / `GPS:srch` / `GPS:142m`
- Gate 3 GPS path: followDistM≥200m + distinctWindows≥1

## Hardware discoveries

### Speaker
- `tone()` / `ledcWriteTone()` does NOT work — T-Deck has I2S amplifier
- Must use `i2s_driver_install()` + `i2s_write()` with PCM sine wave samples
- Port: `I2S_NUM_0`, pins: BCK=7, WS=5, DOUT=6, sample rate 22050 Hz
- 8 DMA bufs × 128 samples, 16-bit stereo
- Init at command start, deinit on exit; shared — never run two I2S commands simultaneously

### GPS (T-Deck Plus)
- Module: Quectel L76K GNSS (or u-blox M10Q on some units)
- Pins: RX=44, TX=43, baud=9600
- Cold start TTFF: ~4 minutes — `test_gps.cpp` shows `Chars:N Sats:N` during wait
- **Critical init sequence for L76K:**
  1. `$PCAS03,0,...` — stop NMEA
  2. `$PCAS06,0*1B` — version request (response: `$GPTXT,01,01,02`)
  3. `$PCAS04,5*1C` — GPS+GLONASS
  4. `$PCAS03,1,1,...` — re-enable NMEA
  5. `$PCAS11,3*1E` — vehicle mode
- Without init sequence: module stays silent (looks like wiring fault, is software)
- Fallback: u-blox M10Q@38400 → u-blox@9600 with UBX CFG-RESET

## Display
- `drawScreen()` layout: line0=status (BLE+WiFi t:MM:SS T1:N Cpn:N [page] GPS:Xm), line1=baseline countdown OR table header, lines 2-7=device rows
- Companion rows: dark grey (0x2104), labeled with BLE name or "Companion"
- Controls line: a=prev l=next w=trust s=save q=quit
- Alert bar at bottom: color-coded by highest threat level
