---
title: Wardrive
lang: en
parent: WiFi
nav_order: 6
---

# Wardrive

## `wardrive` / `wd` — WiFi + GPS Mapping

```
CMD> wd
```

**T-Deck Plus only** (needs GPS). On the base T-Deck it prints a notice and exits.

Press `q` to stop. The GPS task keeps running after you quit (same as the `gps` command).

---

## What It Does

`wardrive` continuously scans for WiFi access points and tags each one with your current GPS coordinates, writing a **WiGLE WiFi-1.4 CSV** you can upload directly to [wigle.net](https://wigle.net) or open in any mapping tool.

- **Waits for the first GPS fix before scanning** (radio stays idle until then), then scans continuously
- Synchronous scan (the proven Bruce/Marauder method) — the screen briefly shows `Scanning...` and freezes for the ~3-4s of each sweep, then updates; `[q]` is read between sweeps
- Each BSSID is logged **once per session** (deduplicated in RAM)
- Rows are written **only while a GPS fix is valid** — an AP with no coordinates is useless to WiGLE
- Hidden APs are logged with an empty SSID

---

## Do I Need to Wait for a GPS Fix?

**No — not to launch.** The app starts immediately, starts the GPS task if it isn't already running, and shows live status. It scans the whole time but **holds logging until there's a fix**:

```
GPS searching  4 sat
not logging until fix (go outside)
```

The moment a fix lands it flips to:

```
FIX  8 sat  LOGGING
+40.123456, -74.123456
```

and rows start landing. A **cold** fix takes ~4 minutes outdoors; a **warm** start (recent fix cached in NVS) is much faster. Indoors you may never get a fix — go outside.

---

## Output File

```
/apps/wardrive/NNN.csv
```

Sequentially numbered (`001.csv`, `002.csv` …) and **never overwritten** — each run is a new file. The file is **created lazily, on the first AP actually logged** — a session that never gets a fix (or you quit early) leaves **no file**, so you won't accumulate empty CSVs. Format is WiGLE WiFi-1.4:

```
WigleWifi-1.4,appRelease=AL-ANQA,model=T-Deck-Plus,release=2026,device=ESP32-S3,display=ST7789,board=LilyGo,brand=LilyGo
MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
1a:2b:3c:4d:5e:6f,MyWiFi,[WPA2-PSK-CCMP][ESS],2026-06-20 14:30:00,6,-55,40.123456,-74.123456,52.0,5.0,WIFI
```

| Column | Notes |
|--------|-------|
| MAC | BSSID, lowercase |
| SSID | empty for hidden APs; commas stripped to keep rows well-formed |
| AuthMode | WiGLE bracket capabilities, e.g. `[WPA2-PSK-CCMP][ESS]`, `[WEP][ESS]`, `[ESS]` (open) |
| FirstSeen | `YYYY-MM-DD hh:mm:ss`, **UTC** from GPS |
| Channel / RSSI | integers |
| Lat / Lon | decimal degrees (6 dp — the real precision of the 32-bit GPS fix) |
| AltitudeMeters | real altitude (metres MSL) from the GPS fix; `0` until the module reports altitude |
| AccuracyMeters | from GPS **HDOP** (same convention as Bruce); falls back to a satellite-count estimate (8 m if ≥6 sats, else 20 m) when no HDOP is available yet |
| Type | always `WIFI` |

Upload at [wigle.net/uploads](https://wigle.net/uploads) to add your captures to the global map.

---

## On-Screen Display

```
[WAR::DRIVE]
File /apps/wardrive/001.csv
FIX  8 sat  LOGGING
+40.123456, -74.123456
Logged 42 APs
Scans 17  (last saw 9)
sweep n=9 fix=1 new=3 wr=3
[q] stop
```

- **Logged** — unique APs written this session
- **Scans** — completed scan cycles; **last saw** = APs in the most recent scan
- **sweep** — per-sweep gate trace: `n` APs returned · `fix` GPS valid (1/0) · `new` rows staged · `wr` rows written to SD. If `n` is high but `wr` stays 0, this shows whether the fix dropped (`fix=0`) or everything was a duplicate (`new=0`).

---

## Notes

- WiFi runs in **STA mode**; SD writes happen between scans (radio idle) — GDMA-safe.
- The GPS task is shared with the `gps` command; if you already ran `gps on`, the fix may already be warm.
- No SD card → it scans and shows status but can't log (shown in red).
