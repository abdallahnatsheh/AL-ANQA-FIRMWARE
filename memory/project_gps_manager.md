---
name: GPS Manager module
description: GpsManager singleton — critical init order, M10Q detection, trackme integration
type: project
---

Singleton: `GpsManager::instance()` (`#ifdef BOARD_TDECK_PLUS`). FreeRTOS task pinned core 0, 30ms poll.

**Init order (must mirror test_gps.cpp exactly):**
```
setRxBufferSize(1024)            ← before begin()
begin(9600)
initL76K() ×3 with delay(500)   ← 3 attempts = ~4.5s = M10Q boot window
  each: PCAS03 stop → PCAS06 version → check $GPTXT,01,01,02
  success: PCAS04 + PCAS03 + PCAS11
if L76K failed:
  begin(38400) → recoverUblox()
  if still fails: updateBaudRate(9600) → recoverUblox()
```

**Why 3 L76K attempts:** M10Q boots in ~4.5s. Single attempt (~1s) sends UBX commands before M10Q is ready → no ACK → wrongly reported "Unknown".

**Trackme integration:** checks `GpsManager::instance().isRunning()` at start — if already running, skips own GPS init and 90s warm-up. `_ownGps` bool prevents double-close on cleanup.
