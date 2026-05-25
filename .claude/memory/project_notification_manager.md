---
name: NotificationManager
description: Future feature — standalone reusable notification module
---

NOT YET IMPLEMENTED. Files: `notification_manager.h/.cpp`. Commands: `vol/vol` + `notif/nf`.

**Design:** singleton, uses `extern LGFX tft` + `SD.h` directly (no displayManager/sdCardManager deps). Self-contained I2S tone (BCK=7, WS=5, DOUT=6). Config: `/notif.conf` (volume, alert/warning/success/info/ping on|off).

**Levels:** NOTIF_ALERT (3× 1kHz) · NOTIF_WARNING (2× 700Hz) · NOTIF_SUCCESS (500→800Hz) · NOTIF_INFO (1× 500Hz) · NOTIF_PING (1× 600Hz)

**Key API:** `begin()` · `notify(level, msg)` · `setWakeCallback(fn)` · `enable(level, bool)` · `setVolume(0-100)`

**Integration points (wire when building):**
- `buddy.cpp` → NOTIF_PING on permission popup
- `trackme.cpp` → replace current I2S beep with NOTIF_INFO/WARNING/ALERT per gate
- `hiddenssid.cpp` → replace I2S beep with NOTIF_SUCCESS
- `wpasniff` → NOTIF_SUCCESS on handshake/crack

**I2S note:** check driver installed before re-installing. Reuse pattern from `spktest`/`hiddenssid`.

**PowerSaveManager:** add `forceWake()` — clears `_manualOff`, calls `wakeUp()`. Wire in `setWakeCallback`.
