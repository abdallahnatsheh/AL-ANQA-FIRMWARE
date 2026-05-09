---
name: GPS Manager module
description: GpsManager singleton — background FreeRTOS GPS task, module detection, status bar icon, trackme integration
type: project
---

## Files
- `t-deck-cli/gps_manager.h` + `gps_manager.cpp` — singleton class + runGpsOn/runGpsOff
- `t-deck-cli/test_gps.cpp` — reference implementation that GpsManager must mirror exactly

## Architecture

**Singleton** — `GpsManager::instance()` (static local, `#ifdef BOARD_TDECK_PLUS` guarded)

**FreeRTOS task** — `gpsTask()` pinned to core 0, 30ms poll loop; BLE/WiFi scan runs core 1 in parallel.

**Volatile primitives** — `_valid`, `_lat`, `_lon`, `_sats`, `_hour`, `_minute`, `_second`, `_chars` all `volatile`. ARM32 4-byte aligned reads are single-instruction — no mutex needed.

**Module string** — `_module[24]`, written once during init, read-only after.

## Module detection — CRITICAL init order

Must mirror `test_gps.cpp` exactly or M10Q will not be detected:

```
initModule():
  _serial->begin(9600, ...)          ← MUST be first, before any probe
  initL76K() — 3 attempts with delay(500) between each (total ~4.5s)
    attempt: PCAS03 stop → PCAS06 version → check $GPTXT,01,01,02
    if found: PCAS04 (GPS+GLONASS) + PCAS03 (enable NMEA) + PCAS11 (vehicle)
  if L76K failed:
    _serial->begin(38400, ...)        ← reinit before u-blox probe
    recoverUblox() → CFG1+CFG2+CFG3 (CFG-CFG reset) + RATE poll
    if still fails: updateBaudRate(9600) → recoverUblox()
```

**Why:** The u-blox M10Q takes ~4.5s to boot. The 3-attempt L76K loop (3 × ~1.5s) provides this naturally. A single L76K attempt (~1s) leaves the M10Q still booting when UBX commands arrive → ACK never comes → "Unknown (no ack)".

**Why begin() inside initModule():** In test_gps.cpp, `gpsSerial.begin(9600)` is called before `initL76K()`. GpsManager must do the same inside `initModule()`, not in `start()`. `setRxBufferSize(1024)` stays in `start()` (must be called before begin()).

## Status bar icon (`display_manager.cpp`)

`drawGPSIcon()` called in `updateStatusBar()`:
- Grey: GPS task not running (`!isRunning()`)
- Yellow: running but no fix (`isRunning() && !isValid()`)
- Green: fix locked (`isRunning() && isValid()`)

## gpson live display

Matches `test_gps.cpp` display exactly: Module name → Chars/Sats → FIX OK / Searching / No data → Lat/Lon/Maps/UTC. Press `q` to return; GPS task stays running.

## Trackme integration

`trackme.cpp` checks `GpsManager::instance().isRunning()` at start:
- If already running: skip own GPS init, skip 90s warm-up, use GpsManager directly
- If not running: own serial init + 90s warm-up loop, then manage own GPS
- `_ownGps` bool tracks ownership so cleanup doesn't double-close serial
