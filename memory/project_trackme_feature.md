---
name: trackme feature implementation
description: Anti-tracking scanner algorithm — gates, scoring, companion/whitelist, WiFi-only cap
type: project
---

**Radio time-slice:** 1s BLE scan → 0.5s WiFi probe sniff (subtype 4) → repeat

**Baseline (60s):** all devices seen in first 60s = `isCompanion=true`, never scored. Start before leaving home so own devices are baselined.

**SD whitelist:** `/logs/trackme_known.csv` (MAC,label per line). `w` key appends highest-threat non-companion.

**Tiers:** Tier1 (max 20, RSSI > −70): full gate analysis. Tier2 (max 100, −70 to −85 or Apple non-tracker): MAC+timestamp only. Below −85: dropped. Tier2→Tier1: RSSI improves or sightings≥3.

**Signatures:** Apple 0x004C requires `mfr[2]==0x12` (AirTag only — not iPhone/Mac/AirPods). Tile 0x00D7, Samsung 0x0075, Chipolo 0x00F0, Eufy 0x006B, Pebblebee 0x0157. SD `/signatures.csv` overrides builtins; Apple THREAT_NONE entries always appended regardless.

**Gap tracking:** absent one cycle → `gapActive=true`. Return → `distinctWindows++` ONLY if gap lasted ≥30s (prevents false gap from single missed BLE advert).

**Gate 2 scoring (unknown BLE only):**
- time: >30min+25, >15min+15, >5min+10
- sightings: ≥5+20, ≥3+15
- RSSI variance: <20+35, <50+20
- gapReturned+25, distinctWindows≥2+15

**Gate 3 (required for WARNING/ALERT — no beep without it):**
- GPS path: followDistM≥200m + distinctWindows≥1 → passes immediately
- Time path: seenMs≥5min + RSSI>−80 + (distinctWindows≥3 or sightings≥3) + no crowd guard
- Crowd guard: crowdAtArrival≥4 + distinctWindows==0 → blocks Gate 3

**WiFi-only (`isWiFi=true`, never BLE):** MAX=NOTICE. Needs `_gpsMoving` (≥50m) + followDistM≥100m + distinctWindows≥1. Stationary=NONE always (eliminates router false positives).

**Scoring flow:**
```
Apple || companion → NONE
WiFi-only → GPS check → NOTICE max
Known: Gate3 → ALERT; else 60s+2sightings+no crowd → NOTICE
Unknown: Gate2 → score; Gate3 → ALERT(≥80)/WARNING(60–79); else NOTICE(≥60)/NONE
```

**Key rule: WARNING/ALERT only after Gate 3. Score≥60 alone = NOTICE max.**

**GPS (Plus only):** `_totalDistM` accumulates ≥10m steps. `_gpsMoving`=true once ≥50m. `followDistM` per device adds GPS step when not gapping. Status: `GPS:none/srch/142m`.

**BLE scan crash fix:** always call `scan->setAdvertisedDeviceCallbacks(nullptr)` before each scan — prevents dangling-ptr crash from `scanblue` leaving a freed callback on the shared BLEScan singleton.
