---
name: powersave-manager-module
description: Display dim via inactivity + battery threshold — hooked into getKeyboardInput() globally
metadata: 
  node_type: memory
  type: project
  originSessionId: 375d410f-2458-450f-8ce0-724f0e36b6fe
---

Singleton hooked into `InputHandling::getKeyboardInput()` — `update()` every poll, `updateActivity()` on keypress. Works globally with no per-command changes.

**Non-obvious:** `init()` must call `tft.setBrightness(fullBrightness)` directly — `wakeUp()` guards on `isDimState=false` and would skip at startup.

**Defaults:** timeout=60s, dim=10, full=200, batteryDim=30, batteryThreshold=20%.

**SD config:** `/pwrsave.conf` (key=value format). Loaded on init, written by `save`, deleted by `reset`. Renamed from `/pwrsave.json` — was silently ignored because the parser uses `=` delimiter, not `:`.

**Subcommands:** `status` `on` `off` `set timeout <s>` `set dimto` `set fullto` `set batterymode on|off` `set batterythreshold <%>` `set batterydimto` `save` `reset`

**Rule:** Never call `tft.setBrightness()` directly from command handlers.
