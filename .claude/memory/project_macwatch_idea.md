---
name: macwatch feature spec
description: WiFi probe + BLE MAC watchlist with proximity alert (trackme-lite)
type: project
---

Command: `macwatch/mw` — watch specific WiFi/BLE MACs (or vendor OUI prefixes), alert when one
comes into radio range. Use case: presence detection / counter-surveillance.
**STATUS: ✅ BUILT + HW-VERIFIED 2026-06-28 (built 2026-06-25 commits 9cec839 + a853f62; field-tested end-to-end — add/presence/proximity/bg all work, idempotent `mw bg/stop` committed aad4dad).**
This file is the design+reuse reference; the implementation diverged slightly (add flow uses a
STABLE list with `[u]` rescan + `[h]` silent hunt meter; BLE device ID split into `ble_ident.h`).

## Architecture — "trackme-lite" (reuse trackme's dual-radio loop)
trackme already runs BOTH radios in one command — macwatch is the same loop with a watchlist
matcher instead of scoring/signatures:
- **BLE**: continuous NimBLE passive scan, callback → lock-free volatile ring, main loop drains
  it. Pattern: `trackme.cpp:28-79` (callback), `:619-627` (drain), `:1086-1092` (scan setup);
  also `bmon.cpp:595-601`. NOTE: phones rotate their BLE identity too (iOS/Android RPA ~every
  15min unless bonded) — reliable targets are dedicated tags / wearables / many IoT, NOT phones.
- **WiFi**: time-gated (~500ms/cycle) promiscuous probe-request sniff; subtype 4, src MAC at
  payload offset 10; own ring. Pattern: `trackme.cpp:603-616` (`wifiCb`), `:639-655` (window).
  **Channel-hop the sniff window (1/6/11…)** — probes are sprayed across channels, single-channel
  misses devices. (Verify whether trackme's window hops; if not, hop here.)
- **GDMA-safe gap (key)**: trackme calls `esp_wifi_set_promiscuous(false)` at the end of each
  sniff window → between windows promiscuous is OFF → that gap is where SD writes are allowed.
  Do all log writes there (no ScopedPromiscPause needed; BLE+SD is fine — bmon logs during
  continuous BLE scan).

## New module
`bluetooth/tools/macwatch/macwatch.cpp/.h` (own pair). Class style like `TrackMeScanner` or free
`runMacwatch(char*)`.

### Data model
```
struct WatchEntry {            // max ~24
  uint8_t mac[6]; uint8_t prefixLen;   // 6=full MAC, 3=OUI-prefix match
  char label[24]; uint8_t radio;       // label = user-given person/device NAME; bitmask 1=WIFI 2=BT 3=both
  int8_t  nearRssi;                    // proximity gate: 0=any range; else alert only when rssiSmoothed>=this
  bool present; uint32_t lastSeenMs;   // presence state machine
  uint32_t lastAlertMs; float rssiSmoothed; int8_t lastRssi;
};
```
Match: compare first `prefixLen` bytes; honor `radio` mask vs sighting source. OUI-prefix
(`prefixLen=3`) is **WiFi-oriented** — randomized BLE addresses have random top bits, no real
vendor OUI, so prefix match is meaningless for BLE (use full-MAC for BLE tags).

### Presence state machine (UX improvement — no beep spam)
ABSENT→PRESENT on first sighting **that passes the proximity gate** → fire arrive-alert ONCE.
Stays PRESENT while seen within PRESENCE_TIMEOUT (~3min, default); expiry → ABSENT + "LEFT: <name>".
Return after ABSENT re-fires arrive alert. (3-min timeout already prevents flap spam; the per-entry
≥30s rate-limit is a redundant safety net.)

**Proximity gate (serves the "getting NEAR me" intent):** if `nearRssi != 0`, a sighting only
counts as PRESENT when `rssiSmoothed >= nearRssi`. Calibration ref: ~-70dBm ≈ 1-2m, ~-85dBm ≈
5-7m. Default 0 = alert on any detectable range. RSSI is Kalman-smoothed (reuse trackme's
smoothing) so a single noisy reading near the threshold doesn't trigger.

### Arrive-alert UX (attention-grabbing)
On ARRIVAL fire three things together:
1. **Beep** — `NOTIF_ALERT` (I2S 3-beep), ALWAYS fires regardless of screen state.
2. **Wake screen** — if dimmed by `PowerSaveManager`, force brightness back up (use its
   brightness/activity API — VERIFY exposed method before coding, rule 6).
3. **Center popup** — full-width centered box (not a thin banner): line 1 = device **NAME**
   (large), line 2 = `-NN dBm  WIFI|BT`. Auto-dismiss after ~4s or any key. Draw via
   `displayManager` like buddy/eviltwin popups.
- **Locked-device rule (deliberate)**: if `displayManager.isBlocked()` (lock screen up), the
  beep still fires but the popup/wake is SUPPRESSED — never draw a watched name over the lock
  screen (security; matches the codebase-wide `isBlocked()` guard). Latch the pending alert and
  render it on `consumeJustUnlocked()`.

## Reuse map (do NOT reimplement)
- Alert 3-beep: `NotificationManager::getInstance().notify(NOTIF_ALERT)` — `notification_manager.cpp:182-221`
- Vendor/type label (add-mode list): `ouiLookup(mac)`→`{vendor,type}` — `oui_lookup.h:816`
- SD CSV: `sd.appendLine()`/`sd.ensureDir()` (`sdcard_manager.cpp:209-215,:40-50`); read via
  `readStringUntil('\n')`+`sscanf("%hhx:..")` like `trackme.cpp:188-209` (loadWhitelist)
- Lock-aware: `consumeJustUnlocked()`→needDraw; `displayManager.isBlocked()` guard every draw
  (`trackme.cpp:784,:1364`)
- Add-mode row-select table: bmon/trackme trackpad selection pattern

## SD layout (add to `hardware/sdcard/sdcard_manager.h` + `ensureTreeStructure()`)
```
#define SD_DIR_MACWATCH      "/apps/macwatch"
#define SD_LOG_MACWATCH_LIST "/apps/macwatch/watchlist.csv"
#define SD_LOG_MACWATCH_EVT  "/apps/macwatch/events.csv"      // optional [s] transition log
```
- `watchlist.csv`: `MAC,label,radio` — MAC full (`AA:BB:..:FF`) or 3-byte prefix (`AA:BB:CC`);
  radio ∈ `WIFI|BT|BOTH`; `#` comments skipped. Loaded once at start (WiFi idle → GDMA-safe).
  RAM fallback: 3 entries if no SD.
- `events.csv` (optional `[s]` toggle like bmon): logs presence **transitions**, not raw
  sightings — `time,event(ARRIVE|LEAVE),name,mac,radio,rssi`. Far fewer SD writes + answers "who
  came by / when". Written only in the promiscuous-OFF gap.

## Command + keys
One-liner in `setupCommands()` (`command_manager.cpp` ~:678; cap 58/64, 6 free), category
`Bluetooth`, `COMP_NONE`:
```cpp
registerCommand("macwatch","mw",[](char* a){ stopEspchatBg(); runMacwatch(a); },
                "Watch MACs: mw [add|bg|stop]", true, "Bluetooth");
// runMacwatch() dispatches: "bg"→startMacwatchBg, "stop"→stopMacwatchBg, "add"→add mode, else interactive.
```
- `mw`/`mw watch` — load list, scan both radios, alert on presence changes.
- `mw add` (or `[a]`) — scan both radios ~10s, candidate table (MAC, vendor via ouiLookup, type,
  RSSI, radio), trackpad-select a device, then a **name prompt** (text entry, ≤23 chars, e.g.
  "John's phone", "Mom laptop") so you know *who* is near you, choose full-MAC vs OUI-prefix,
  append to CSV. Name entry reuses the inline text-entry idiom (trackme whitelist label / eviltwin
  prompt); empty name falls back to `vendor` (ouiLookup) else the MAC string. Name is editable
  later by re-adding the same MAC (dedup on MAC → overwrite label). After name, optionally set the
  proximity threshold (`nearRssi`) or accept "any range".
- Watch view keys: `[a]` add · `[r]` remove selected entry (also `mw rm <#>`) · `[s]` toggle event
  log · `[c]` clear runtime presence · `[q]` quit. (No `[l]` — the main view IS the watchlist.)
- **Empty-state**: if the watchlist is empty on `mw`, show a hint and drop straight into `[a]` add.
- `mw bg` — start background watch (see below) · `mw stop` — stop it.

## Background mode (`mw bg` — like `ec bg`/`wg bg`)
Mirror the espchat-bg pattern EXACTLY (`radio/espnow/espchat/espchat_bg.cpp`): a new
`bluetooth/tools/macwatch/macwatch_bg.cpp` with `startMacwatchBg()/stopMacwatchBg()/
pollMacwatchBg()/isMacwatchBgActive()` + a `g_mwBgActive` flag.
- **Hook**: add `pollMacwatchBg();` in `getKeyboardInput()` right after `pollEspchatBg();`
  (`input_handling.cpp:92`). Self-throttle with a `s_lastPoll` (≥200ms) like espchat_bg.
- **Radio = BLE-only in bg** (deliberate): WiFi promiscuous in the background would fight every
  WiFi command AND `wg bg`. BLE coexists with WiFi (time-division) and is the reliable presence
  radio anyway (associated phones stop probing). Full dual-radio stays foreground-only. Continuous
  NimBLE scan started in `startMacwatchBg()`, drained in `pollMacwatchBg()`, presence SM reused.
- **Watchlist** loaded once in `startMacwatchBg()` (SD read). `events.csv` writes in bg.
  ⚠️ **CORRECTED 2026-06-26 (real crash fix):** the original "no promiscuous in bg → GDMA-trivial,
  no pause window needed" was WRONG. macwatch's *own* radio is BLE-only, but `pollMacwatchBg()`
  runs right after `wGuard.pollBackground()` in the same `getKeyboardInput()` tick — and **`wg bg`
  keeps promiscuous ON continuously**. So an `events.csv` write during a presence transition while
  `wg bg` is up corrupts FatFS (GPS + wg bg + mw bg → T-Deck rebooted after ~1h). **FIX:** wrap
  ALL macwatch SD access (`mwFlushEvts`/`mwSaveWatchlist`/`mwLoadWatchlist`) in `ScopedPromiscPause`
  (`wifi/core/wifi_sd_guard.h`) — it pauses whoever owns promiscuous for the write, no-op if off.
  Also: fg exit must NOT blanket `esp_wifi_set_promiscuous(false)` (kills wguard's IDS) — guard with
  `!wGuard.isBackground()`. **Lesson: "my command doesn't run promiscuous" ≠ GDMA-safe — another
  background command might. Default to `ScopedPromiscPause` on every mid-session SD write.**
- **Status-bar badge**: `MW` icon via a new `displayManager.setMwActive(true)` flag (add alongside
  `setEcActive`); flashes alert-colour on a hit.
- **Alert in bg** = beep (`NOTIF_ALERT`) + wake screen + **popup bar at y=222** (16px, lock-aware,
  `printCommandScreen()` to restore after ~4s) — non-takeover, since the user is at the CLI doing
  other things. Same locked-device rule (beep only, no draw, latch).
- **Conflict handling (critical)**: the NimBLE scan is a singleton. Every BLE-touching foreground
  command must call `stopMacwatchBg()` before starting — add it to the registrations for
  `scanblue/sbl`, `bleinfo/bi`, `bmon/bm`, `trackme/tm`, `blespam/bs`, `fast_pair/fp` (same way
  they/WiFi cmds call `stopEspchatBg()`). Foreground `mw` also stops bg first.

## Display
Header `[MAC::WATCH NN]` via `displayManager.updateStatusBar()` (NO custom banner — trackme's
ClockManager status-bar conflict lesson). Rows: `●/○ label  RSSI-bar  radio  last-seen`
(green=PRESENT, grey=ABSENT). Transient 1.5s notice line for ARRIVED/LEFT/toggle (trackme
`_uiNoticeMs`/`_uiNoticeText` idiom).

## Memory (rule 5c)
Watchlist (~24×40B) tiny → plain member array, freed on exit. Rings ISR-touched → internal DRAM
(small fixed `volatile`, like trackme). No PSRAM, no always-resident global.

## Verify ( flashes; trace logic first, one flash = one datapoint)
- `mw add` his phone (BLE) + a laptop (WiFi) w/ labels. `mw`: walk out → "LEFT" after timeout;
  back → single "ARRIVED" beep (not continuous); RSSI bar tracks. watchlist.csv survives reboot.
  `[s]` writes only in gap (no SD corruption after long run = GDMA path correct). OUI-prefix entry
  matches a randomized-MAC device of that vendor. Lock during `mw` → unlock → clean redraw.
- Bring-up diag: show BLE-ring drain count + WiFi sniff frame count so one session proves both
  radios feed the matcher.
- **bg test**: `mw bg`, return to CLI, run other commands → `MW` badge stays, watched tag arrival
  fires beep + popup bar (not full takeover). Then run `sbl`/`bm`/`tm` → confirm `stopMacwatchBg()`
  fires (no NimBLE double-scan crash). `mw stop` clears badge. Lock during bg → beep only, popup
  after unlock.

## Honest limitations (document in-app + README)
- **BLE is the more reliable radio** for presence. WiFi probes only fire when a device is actively
  probing — an **associated, idle/asleep** device may stop probing entirely → undetectable via WiFi.
- Phones randomize **both** WiFi probe MACs and BLE identity (RPA) → full-MAC watching of a phone
  degrades over time. Mitigations: OUI-prefix (WiFi only); best targets are tags/wearables/IoT.
- Proximity (RSSI→distance) is coarse — bodies/walls/orientation swing it ±10dBm. The gate is
  "roughly near," not metric distance.

## Notes / future
- WiFi probe ring is now duplicated across trackme/wifimon/macwatch → future `wifi/core/`
  extraction (improvement-backlog #3), NOT forced here.
- **Defeat MAC randomization (future):** `karma/km` already clusters randomized MACs into physical
  devices via PNL union-find (shared probed-SSID set). macwatch could borrow that to watch a
  *device fingerprint* instead of a MAC — research confirms IE/PNL fingerprinting hits 75-99%
  association accuracy (Bleach 2024). Big addition; note only.
