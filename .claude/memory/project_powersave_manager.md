---
name: PowerSave Manager module
description: Display power-save via inactivity dimming + battery-aware dim — singleton, FreeRTOS-safe, SD config persistence
type: project
---

Singleton: `PowerSaveManager::getInstance()` — hooked into `InputHandling::getKeyboardInput()` so it works globally across every blocking loop without any per-command changes.

**Why:** `getKeyboardInput()` is the one function every blocking loop already calls for `q`. Putting `update()` and `updateActivity()` there makes power-save truly global.

**Files:** `powersave_manager.h` / `powersave_manager.cpp` (new), modified `input_handling.cpp`, `main.ino`

**Key behaviour:**
- `update()` called every `getKeyboardInput()` poll — rate-limited: battery check max 1 Hz (getPct() reads ADC 20×), inactivity check every call
- `updateActivity()` called on any non-zero keypress — resets timer and wakes display if dimmed
- Battery mode: if battery < threshold → force dim regardless of inactivity state
- `init()` must call `tft.setBrightness(fullBrightness)` directly — `wakeUp()` is guarded by `isDimState=false` so it would skip at startup

**Defaults:** timeout=60s, dim=10, full=200, batteryDim=30, batteryThreshold=20%, enabled=true, batteryMode=true

**SD config:** `/pwrsave.json` (actually key=value format, not JSON despite name). `save` command writes it; loaded on `init()`. `reset` deletes the file.

**Command:** `pwrsave`/`psv` — subcommands: `status`, `on`, `off`, `set timeout <s>`, `set dimto <0-255>`, `set fullto <0-255>`, `set batterymode on|off`, `set batterythreshold <%>`, `set batterydimu <0-255>`, `save`, `reset`

**How to apply:** Always hook display/power interactions through PowerSaveManager. Never call `tft.setBrightness()` directly from command handlers.
