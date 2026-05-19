---
name: buddy-module
description: Claude Desktop BLE remote — architecture, NimBLE init pattern, popup flow, protocol limits
metadata:
  type: project
---

## buddy / bd command (`t-deck-cli/buddy.cpp`)

Full port of claude-desktop-buddy for T-DECK. NimBLE NUS server + ASCII pet + NVS stats.

### NimBLE init pattern
Cold-boot safe: `init("") → deinit(true) → init(btName)`. Guard `deinit` with `if (NimBLEDevice::getInitialized())` — NOT `isInitialized()` (wrong API). Use `deinit(true)` (not false) to delete C++ objects; prevents duplicate-service crash on second `bd` run. After BLE deinit on quit: `SD.begin(39)` to restore SPI ([[feedback_esp32s3_wifi_sd_gdma]]).

### Display layout
- Left panel 0–161px: stats + HUD (drawn directly on `tft`)
- Right panel 162–319px: `LGFX_Sprite spr` (155×200) pushed at 5fps via `petTick()`
- `outputY = 38`, `SCREEN_HEIGHT = 240`

### Permission popup
When `promptId` arrives, `s_popupOpen = true` → `drawPopup()` overlays full screen (4px inset, cyan double border). Redraws every 1s for timer. Pet tick skipped while popup open. Space-to-cycle-species disabled while popup open. y/n closes popup, sets `lastPid[0] = '\1'` to force `drawStatus()` redraw. Auto-closes if desktop clears the prompt.

### Protocol (claude-desktop-buddy NUS JSON)
Heartbeat fields: `total`, `running`, `waiting`, `tokens_today`, `msg`, `entries[]`, `prompt{id,tool,hint}`.  
Permission response: `{"cmd":"permission","id":"...","decision":"once|deny"}\n`  
**`"always"` and `"comment"` are NOT supported** by the desktop handler.  
**`hint` is truncated on the desktop bridge side** to ~43 chars — the T-DECK buffer (`promptHint[256]`) is not the bottleneck. Cannot fix without patching the desktop bridge JS (DevTools → Sources → search `.slice(0,` near `hint`).

### Stats (NVS namespace "buddy")
Keys: `nap` (UInt), `appr`/`deny` (UShort), `tok` (UInt), `vidx`/`vcnt` (UChar), `vel` (bytes 16).  
Token sync: bridge sends cumulative since its own start; T-DECK latches on first packet, accumulates deltas.  
Level = `tokens / 50000`. Mood = median velocity over last 8 approvals, adjusted by denial ratio. Energy = 5/5 on session start, drains 1 tier per 2h.

### Species
19 species in `SPECIES_TABLE[]`: TREX, CAPYBARA, DUCK, GOOSE, BLOB, CAT, DRAGON, OCTOPUS, OWL, PENGUIN, TURTLE, SNAIL, GHOST, AXOLOTL, CACTUS, ROBOT, RABBIT, MUSHROOM, CHONK.  
7 states: B_SLEEP(0) B_IDLE(1) B_BUSY(2) B_ATTENTION(3) B_CELEBRATE(4) B_DIZZY(5) B_HEART(6).  
Species files: `t-deck-cli/buddies/*.cpp`, each includes `buddy_common.h` and `M5StickCPlus.h` (shim: `typedef LGFX_Sprite TFT_eSprite`).
