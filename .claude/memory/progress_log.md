---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

## Session 2026-06-30 (netspy Stage 1b COMPLETE + device probing + ping UI bug fix) — ✅ ALL HW-VERIFIED
- **netspy/ns Stage 1b** (DHCP/mDNS/SSDP hostnames + service enum + `[i]`/Enter detail view + UI polish +
  docs/man/README/CLAUDE/NOTICES/sdcard map): **✅ fully HW-verified.** Usability: detail opens on **Enter**
  (not click — user found click annoying), CSV saves ALL fields incl a `services` column.
- **Device probing from netspy** (✅ HW-verified): in-app `[p]` ping / `[o]` port-scan the selected row in
  place (`nsProbe` suspends promiscuous→runs tool→resumes); AND CLI `ps ns#`/`pg ns#`/`ps top ns#` targeting
  via a source prefix in `resolveTarget` (`ns#`=netspy, `#`/`nd#`=netdiscover) + a `#` index column on the
  table. netspy exports `netspyDeviceCount/Ip`. Commits `5c45ee8` (Stage 1b) + `dbceef8` (probe). Unpushed.
  **Full detail in [[project_netspy_isoscan_plan]] BUILD STATUS.** HW-validated end-to-end: `ns` found a
  device pingable but invisible to `nd` → soft/discovery-only isolation on the test net.
- **ping/pg UI BUG FIXED** (`wifi/monitor/netscanner/network_scanner.cpp` `pingHost`): old code used
  `println()` in a loop advancing via `getCursorY()`; with ~10 results + resolve + summary the cursor ran
  past the 240px screen bottom and **wrapped back to y=0, drawing OVER the status bar**. Rewrote with a
  **fixed-Y bounded layout** (explicit setCursor per element, never wraps): `[NET::PING]` header + target +
  rolling 7-row results ring (oldest→newest) + live `sent/recv/loss%` + `min/avg/max RTT` + `q=stop`,
  full-redraw each tick (lock-aware via `isBlocked`/`consumeJustUnlocked`). Now **continuous until q**
  (was fixed 10). Also wired `resolveTarget()` so `pg 0` (nd ARP index) works — docs promised it but code
  never did it (DNS-only). delay(20) poll keeps q responsive. man/docs/ping.md/README synced.
- **LESSON:** any screen that prints lines in a loop via `println`/`getCursorY` without a Y bound can
  overflow past y=240 and wrap over the status bar. Fixed-Y layouts (like bmon/netspy) are immune. Other
  simple println-based screens may have the same latent bug — audit if reported.

## Session 2026-06-29 (netspy/ns — AirSnitch client-isolation recon: Stage 0+1 BUILT, UNCOMMITTED) ⏸ RESUME HERE
**Full detail in [[project_netspy_isoscan_plan]] (BUILD STATUS section). Cold-resume summary:**
- **Goal:** `netspy`/`ns` (Network) — discover devices on a client-isolated WiFi that `nd` (ARP scan)
  can't see. Based on AirSnitch (Vanhoef NDSS 2026; techniques only, no code; credited in file header).
- **Feasibility resolved:** the AirSnitch GTK-*inject* core is gated by ESP32's CLOSED WiFi blob (no
  GTK-export API; `esp_wifi_80211_tx` won't CCMP-encrypt) — a firmware not silicon wall. The GTK is in
  the OPEN `wpa_supplicant`; its global `gWpaSm` is an EXPORTED symbol.
- **Stage 0 ✅ HW-VERIFIED — GTK extraction:** read live GTK at **`gWpaSm+0x174`** (len `+0x194`, =16
  CCMP). Offsets are framework-specific → **platform PINNED `espressif32@7.0.1`** (arduino 2.0.17 /
  IDF 4.4.7). `ns gtk` shows it; changes per reconnect. Found via struct-math + `install_gtk` disasm +
  an on-device `ns dump` (gWpaSm→`/apps/netspy/gwpasm.txt`). PMK is at `gWpaSm+0x00`.
- **🔑 BIG FINDING (HW-verified):** while ASSOCIATED, the ESP32 HW already DECRYPTS group/broadcast
  frames; promiscuous delivers them in CLEAR (CCMP header kept, payload plaintext at `hdrlen+8`). So
  **passive discovery needs NO software CCMP / NO GTK** — just sniff + parse. (The `ns dec` decrypt
  experiment MIC-failed because the bytes were already plaintext — that's how we discovered this.)
- **Stage 1 ✅ HW-VERIFIED (2026-06-29):** `ns` found 14 real devices (IP+MAC) on a client-isolated net
  where `nd` saw ONLY the gateway. Bypass proven end-to-end. CSV save works (real NTP datetime). `?`
  vendors = OUIs not in oui_lookup (cosmetic); LA-MAC rows correctly flagged RandMAC.
  **`ns` scanner internals:** promiscuous-capture group
  data frames (fromDS, A1 group, A2==our BSSID) → ring → main-loop parse LLC/SNAP: ARP (sender MAC+IP),
  IPv4 (A3 src MAC + src IP) → `NsDev[48]` table (MAC/IP/vendor via `ouiLookup`/how). UI table, `[s]`
  save `/apps/netspy/NNN.csv` (ScopedPromiscPause — promiscuous is live), `[c]` clear, `[l]/[a]` page,
  `[q]` quit. `ns gtk` / `ns dump` subcmds. Module `wifi/intel/netspy.cpp/.h`.
- **Files touched (ALL UNCOMMITTED):** `wifi/intel/netspy.cpp/.h` (new), `command_manager.cpp` (fwd-decl
  + register `netspy`/`ns` Network 60/64 + arg-hints `gtk dump`), `platformio.ini` (`-I wifi/intel` +
  pinned `espressif32@7.0.1` ×3 envs), `sdcard_manager.h/.cpp` (`SD_DIR_NETSPY` + ensureTree).
- **GOTCHAS:** DisplayManager has `printText()`/`println()`, NO `print()`. gWpaSm decl =
  `extern "C" { extern uint8_t gWpaSm[]; }`. SD writes during `ns` MUST use `ScopedPromiscPause` (GDMA).
- **NEXT STEPS (do in order):** (1) flash + TEST Stage 1 — `cw` then `ns`; confirm devices appear that
  `nd` misses; check `/apps/netspy/001.csv`. (2) Stage 1b — parse DHCP/mDNS/SSDP for hostnames/services.
  (3) Stage 2 INJECT (the active AirSnitch): software CCMP-encrypt a non-QoS broadcast data frame with
  the GTK + spoof AP MAC + high PN, send via `esp_wifi_80211_tx` (allows non-QoS data) → ICMPv6-RA DNS
  poison etc. — uses the Stage-0 GTK. Unproven; spike it. (4) docs/man/README/NOTICES + commit.
- **COMMITTED `c8d127e`** (Stage 0+1, both HW-verified). Unpushed (user pushes manually; HTTPS auth
  unavailable here).
- **USER DECISION: keep PASSIVE vs ACTIVE as TWO separate commands** — `netspy`/`ns` = PASSIVE recon
  (done, never transmits, safe default) · `isoscan`/`is` = ACTIVE attacks (Stage 2, opt-in, transmits;
  authorization warning + mandatory MAC-restore). Deliberate separation so running recon can't
  accidentally transmit. So Stage 2 = build the `isoscan` command. Feasible active module = GTK inject
  (software CCMP-encrypt a broadcast data frame w/ unicast victim IP + spoof AP MAC + high PN, via
  esp_wifi_80211_tx using the Stage-0 GTK) → ICMPv6-RA DNS poison; gateway-bounce/port-steal are
  murkier on ESP32. All ACTIVE work = authorized/own networks only.

## Session 2026-06-28 (lockscreen: lock-on-boot + SD reset-flag recovery — FINAL, built, UNTESTED)
- **DECISION (user, firm): PIN stays on the SD card ONLY — do NOT put it in NVS.** Mid-session I
  moved hash/salt to NVS to close the SD-removal bypass; user reverted it: they WANT removing the SD
  to be the forgot-PIN recovery, and want the card PC-readable. So the lock is SD-stored by design,
  and "remove SD → boots unlocked" is the intended recovery, not a bug. (NVS path fully reverted via
  `git checkout` + re-applied lock-on-boot; don't reintroduce NVS storage for the PIN.)
- **What the user verified on HW:** cold-boot WITHOUT SD → unlocked CLI (intended recovery). Re-insert
  SD → `ls` said "not mounted" (sdCardManager.ready latched false at boot, no hot-mount). NOTE for
  future: `wp` + `fp/bs/bd/btkbd` use bare `SD.open`/`SD.begin(39)` ignoring `canAccessSD()`, so they
  CAN still read a hot-inserted card — left as-is since this state is the intended recovery (unlocked).
- **Final feature set:**
  - `lock boot on|off` → `lockonboot=1` in `/config/lockscreen.conf` (default off); `init()` calls
    `lock()` after `loadConfig()` when `_hasPassword && _lockOnBoot` (display up by setup() line 62,
    `setBlocked(true)` stops `setupCommands()` painting over it). Guarded on `_hasPassword`.
  - Recovery: (a) remove SD + reboot (no config → unlocked); (b) one-shot `reset=1` line in the conf
    → `init()` clears hash/salt + rewrites file without the flag (keeps timeout/lockonboot).
  - `cmdStatus` shows "On boot: lock/no". cmdWipe left as original (SD delete + NVS wipe-flag).
- **SD-access GATE — BUILT then REVERTED same day. DO NOT REINTRODUCE.** I added `setLockGate`/
  `_lockGated` (canAccessSD gated while locked) + bare-path guards (wp, fp/bs/bd/btkbd SD.begin) +
  moved init() to end of setup(). User then pointed out the fatal flaw: it BLOCKS legitimate SD saving
  while locked — wardrive logging, `wg bg`/`mw bg` loggers, bmon, etc. must keep saving with the
  screen locked. And the gate was pointless: a locked device has NO CLI, so ls/wp/cat can't be typed
  regardless; the gate's only real effect was breaking background/foreground saves. Fully reverted via
  `git checkout` of sdcard_manager.{h,cpp}, wifi_creds.cpp, fast_pair/ble_spam/buddy/ble_keyboard,
  main.ino (init() back at its original spot) + removed the 3 setLockGate calls in lockscreen. Net: no
  SD gate; locking never blocks SD I/O.
- **USB MSC idle-lock fix (KEPT, separate good bug fix):** see below.
- **HONEST scope (told user):** lock protects the running device, NOT the SD contents (removable
  plaintext, PC-readable by their choice). Any physical holder of the card can recover/read.
- Surfaces: lockscreen_manager.h/.cpp (`_lockOnBoot`/`_pendingReset`, loadConfig parse, saveConfig
  write lockonboot, init reset+boot-lock, cmdBoot, cmdStatus line, cmd dispatch+usage),
  command_manager.cpp (reg desc + arg-hint `boot`), man_pages.cpp, docs/lock.md, docs/troubleshooting.md,
  docs/sdcard.md, CLAUDE.md. ⚠️ NOT compiled/flashed (user builds manually).
- **HW-found bug + fix (2026-06-28):** user's `reset=1` MSC edit "kept failing" — root cause was the
  **idle auto-lock firing DURING the USB-MSC session**. MSC loop (`usb_manager.cpp` startMSC) only
  reset the idle timer on a T-Deck keypress, but during MSC the user types on the PC → after
  `timeout` (their 300s) the screen auto-locked mid-session, breaking the host write/eject so the edit
  never persisted. FIX: call `LockScreenManager::updateActivity()` every MSC-loop iteration. (Same
  latent risk exists for any long PC-driven, no-keypress session, e.g. `ux` BadUSB script run — not
  fixed, note only.) ALSO clarified: MSC-based `reset=1` only works when already UNLOCKED (you can't
  run `um` while locked) — true forgot-PIN recovery is the card-reader / remove-SD path.
- Boot-security audit done (no SOFTWARE bypass at power-on, separate from the by-design SD-removal
  recovery): single input chokepoint `getKeyboardInput()→intercept()`; `processInput(0)` no-op;
  trackball swallowed while locked; no Serial cmd path; USB MSC `mediaPresent(false)` at boot (no auto
  SD expose to host); nothing draws over the lock between init() and loop().

_Original first-pass notes below — still accurate for the implementation details:_
- **Privacy ask:** show the lock screen at power-on, plus a forgot-PIN recovery flag in the SD conf.
- **lock-on-boot**: new `lockonboot` key in `/config/lockscreen.conf` (default off), toggled by
  `lock boot on|off` (new `cmdBoot`). `init()` calls `lock()` after `loadConfig()` when
  `_hasPassword && _lockOnBoot` — display is already up by line 62 of setup(), `setBlocked(true)`
  keeps `setupCommands()` from painting over it, so no setup() reordering needed. Guarded on
  `_hasPassword` (no PIN → nothing to protect, won't lock; cmdBoot warns).
- **one-shot reset flag**: hand-add `reset=1` to the conf on a PC → `loadConfig()` parses it into
  transient `_pendingReset`; `init()` clears hash/salt, `saveConfig()`s (which NEVER writes `reset`
  back → one-shot, can't keep wiping), keeps timeout/lockonboot, boots unlocked. Only works with SD
  present (loadConfig bails early with no SD, so `_pendingReset` stays false — correct, you need SD
  to have edited the file).
- **SECURITY FRAMING (load-bearing, in docs + CLAUDE + cmds):** the reset flag is OWNER CONVENIENCE,
  NOT extra security — the PIN hash is plaintext on the *removable* SD, so physical-access recovery
  is already inherent (pull SD + reboot → Space×3, or `lock wipe`). The lock deters someone grabbing
  the running/unattended device; it does NOT resist an attacker who has the SD card. (If we ever want
  SD-thief-proof lockdown → move hash to NVS; breaks the pull-SD recovery model — not done.)
- Surfaces: lockscreen_manager.h/.cpp (members `_lockOnBoot`/`_pendingReset`, loadConfig parse,
  saveConfig write, init reset+boot-lock, cmdBoot, cmdStatus "On boot" line, cmd dispatch+usage),
  command_manager.cpp (reg desc + arg-hint `boot`), man_pages.cpp (lock entry), docs/lock.md
  (lock-on-boot section + reset-flag recovery + config table), docs/troubleshooting.md,
  docs/sdcard.md, CLAUDE.md (LockScreenManager triggers/recovery/config lines).
- ⚠️ NOT compiled/flashed (user builds manually). Trace-verified: wipe path still returns before
  loadConfig; reset rewrite is one-shot; lockonboot persists across `lock new`/`update`/`clean`.

## Session 2026-06-26 (csidetect PRO upgrade + honest reframe → "WiFi motion detector" [EXP])
- Reworked the single-dot MVP into a richer build, then **reframed it honestly** after UX testing:
  it is NOT a radar. Renamed in-app `[CSI::MOTION]` ("exp - motion only, not a radar"); user-facing
  text everywhere says "motion detector / sweep-style display, not an actual radar". Tagged **[EXP]**.
- **Pro render**: double-buffered PSRAM `LGFX_Sprite` (~30 fps, no flicker; API mirrors buddy —
  setColorDepth/createSprite/pushSprite/deleteSprite + `setPsram(true)`); sweep cone (fan of fading
  triangles), pulsing CLEAR/CONTACT reticle. Sprite is radar region only (`SPR_W×SPR_H` at OY=40);
  panel/header/footer drawn straight to tft.
- **Per-subcarrier bands**: `csiCb` splits subcarriers into `CSI_BANDS=8` responsive-EMA bands →
  sector "contacts". GATED behind global motion + only bands reacting ABOVE average light up (fixed
  the "noisy when idle" complaint); strong reaction → nearer centre. Global windowed-variance presence
  core kept intact (proven). Snappy: hold/coast cut to ~1.2 s. Activity word STILL/FIDGET/WALK/RUN.
- **HONESTY (load-bearing, proven from the sources' own code)**: single antenna = ONE motion-energy
  signal — no bearing/count/position. Cardputer `pickBlipAngle()` is literally `random()`; ruview gets
  real placement only via multi-node mesh + ML. The 8 sectors are signal bands spread for readability,
  NOT directions. **Never reintroduce fake per-person positions.**
- Keys: `a`/`l` + trackball = sensitivity, `c` = recalibrate, `h` = full help overlay, `q` = quit
  (now clears screen on exit). Requires WiFi connected; bails under `wg bg`.
- **Docs/surfaces added**: man page (`man csi`), hlp desc, README row, docs/diagnostics.md section
  (honest limits + credits), CLAUDE.md module note + commands line. NOTICES already has #12 (Cardputer
  MIT) + #13 (esp-csi); ruview credited in code header.
- HW: motion detection confirmed working across iterations. **Next (optional)**: SD presence log +
  alert (turns it from toy → unattended tool); field-tune whether sectors give usable differentiation.

## Session 2026-06-26 (csidetect/csi — WiFi CSI presence radar MVP — ✅ HW-VERIFIED)
- **New `csidetect`/`csi` (Diagnostics)** — single-chip WiFi CSI human-presence detector with a
  phosphor **radar UI**. Ported the reference's single-device CSI path (skizzophrenic/
  Cardputer-CSI-Human-Detector, MIT). ✅ **works on the T-Deck** (user confirmed). Cmd 58→**59/64**.
  Full reference + next steps in [[project_csi_camdetect_plan]].
- **Algorithm (verbatim from ref)**: IRAM `csiCb` → per-subcarrier amp `sqrt(r²+im²)` + mean
  sin(phase), 50-frame window, amp+phase variance, asymmetric-EMA self-cal (0.1/0.002/0.005), blend
  `0.6·amp+0.4·pha`; hold/coast presence (thresh **0.15**, hold 150 @15Hz). `wifi_csi_config_t` =
  IDF-4.4 fields (correct for espressif32 6.x).
- **UI = HONEST radar** (single antenna → no bearing/count, just one motion-energy signal): sweep
  angle = TIME, blip radius = motion intensity, center reticle CLEAR↔CONTACT. `[`/`]` thresh, `c`
  calibrate, `q` quit. On-screen bring-up diag (`fr:<n>` + `CSI live|ERR|no frames`) per rule-7.
- **MVP scope (user: confirm radar first, then expand)**: NO SD, NO beep. Requires WiFi connected;
  bails if `wg bg` owns promiscuous.
- **camdetect DROPPED** — not CSI (a promiscuous OUI sniffer that shared the plan doc); separate later.
- Files: `wifi/sensing/csidetect.cpp/.h`, command_manager (fwd-decl+reg), platformio (`-I wifi/sensing`),
  NOTICES (#12 MIT, #13 esp-csi). **Next: sprite double-buffer → flicker fix + mockup look (soft sweep
  wedge / amber contact / presence box); then SD+beep; then coexistence.**
- Note: built/committed across PCs — this commit done in user's name only (no Co-Authored-By).

## Session 2026-06-26 (macwatch GDMA crash fix — gps + wg bg + mw bg → reboot after ~1h)
- **User field report:** ran `gps` on + `wg bg` + `mw bg` together; ~1h later the T-Deck had
  rebooted on its own. **ROOT CAUSE = GDMA-rule violation.** `pollMacwatchBg()` runs right after
  `wGuard.pollBackground()` in the same `getKeyboardInput()` tick, and **`wg bg` holds promiscuous
  ON continuously**. macwatch's `mwFlushEvts()` wrote `events.csv` on a presence ARRIVE/LEAVE while
  that promiscuous DMA was live → FatFS/GDMA corruption → reset. Only fires on a transition, hence
  "~1h". The old `mwFlushEvts` comment "bg never runs promiscuous → always GDMA-safe" was wrong —
  it only considered macwatch's OWN radio, not a co-running background command.
- **FIX (macwatch.cpp):** `#include "wifi_sd_guard.h"`; wrapped `mwFlushEvts()`, `mwSaveWatchlist()`,
  `mwLoadWatchlist()` in `ScopedPromiscPause` (reads current promiscuous, pauses wguard's for the
  write, restores; no-op when off → correct in fg-sniff / bg / standalone paths). Also fixed a
  second latent bug: fg `mwRunInteractive()` exit blanket-called `esp_wifi_set_promiscuous(false)`,
  which would silently kill `wg bg`'s IDS sniffing — now `if (!wGuard.isBackground())`.
- **Lesson (added to [[project_macwatch_idea]]):** "my command doesn't run promiscuous" ≠ GDMA-safe
  — a background command (wg bg) might. Default to `ScopedPromiscPause` on every mid-session SD write.
- ⚠️ NOT yet compiled/flashed (user flashes manually). Latent secondary risk noted but untouched:
  GpsManager's one-time first-fix NVS *flash* write could also collide with `wg bg` promiscuous
  (wardrive's "NVS write during live WiFi" lesson) — would crash ~4min in, not 1h, so not this bug.

## Session 2026-06-28 (csidetect upgrade: NBVI + Hampel + passive `csi auto` — built, UNTESTED)
- Web-researched 2025-26 CSI state of the art (WebSearch). Key findings: (a) direction/bearing is
  STILL impossible on a single antenna — needs multi-antenna AoA or multi-node mesh (confirmed by
  esp-csi repo + survey); auto mode does NOT add direction, only robustness. (b) Espressif's official
  `esp_wifi_sensing`/`esp-radar` components need **IDF ≥5.4** → unusable on our IDF 4.4 — so I ported
  the METHODS by hand instead. (c) ESPectre (GPL) validated passive/no-association sensing.
- **Implemented (user: "do it, keep radar"):** radar UI untouched.
  1. **NBVI auto subcarrier weighting** — per-subcarrier EMA mean/var in `csiCb` → weight `var/mean²`
     (capped 4), weighted-mean amplitude emphasises motion-responsive subcarriers vs plain average.
     Warmup fallback to uniform mean (`gScWarm` after CSI_WINDOW*2 frames); live A/B toggle `[n]`
     (`gNbviOn`). `gScMean/gScVar` float[CSI_MAXSC=128] in DRAM (IRAM cb writes them).
  2. **Hampel filter** (`csiHampel`, 7-sample median+MAD) on the motion stream in the main loop →
     kills lone glitches before threshold/hold-coast.
  3. **Passive `csi auto` mode** — `csiAutoScout()` (WiFi.scanNetworks, pick strongest AP) → park on
     its channel (`esp_wifi_set_channel`) + **single-source MAC lock** (`gLockActive`/`gLockMac`,
     filtered on `info->mac` in `csiCb`). No association needed → works vs ANY router. MAC lock is
     load-bearing: blending multiple transmitters' CSI = false motion. Connected `csi` unchanged
     (unfiltered, on-link, proven). Verified `wifi_csi_info_t.mac[6]` exists in the IDF-4.4 SDK header.
  4. Panel shows `AUTO c<ch> <mac>`/`LINK` + `NBVI on/off`; footer/help/man updated; `n` key added.
- **HONESTY kept:** still motion-only, no bearing/count/position in either mode. Auto = robustness,
  NBVI/Hampel = signal quality, NOT direction. Direction would need multi-antenna or multi-T-Deck mesh.
- Credit: ESPectre added as NOTICES #14 (methodology, no code) + file header; esp_wifi_sensing IDF-5.4
  gap noted in NOTICES #13. Surfaces: csidetect.cpp, command_manager (hasArgs true + `auto` hint),
  man_pages, CLAUDE.md, docs/diagnostics.md, NOTICES.
- **Follow-up (same session): adaptive threshold + SD logger added.**
  - **Adaptive threshold** `[t]` (`gAdaptive`): thresh = learned quiet-floor (`gNoiseEMA`, updated only
    while not-present) + `gMargin`; auto-raises the bar on a noisy source (the real fix for `csi auto`
    noise). `a/l` nudge `gMargin` when on, `thresh` when off. Panel shows `THR auto`.
  - **SD logger** `[s]`: presence transitions (CLEAR↔CONTACT edges only) → `/apps/csidetect/NNN.csv`
    (sequential never-overwrite; `SD_DIR_CSIDETECT` added to sdcard_manager + ensureTree). Columns
    `time,event,motion_pct,thresh_pct,zones,mode,source`; time = ClockManager or `@<ms>` fallback.
    **GDMA: every SD touch wrapped in `ScopedPromiscPause`** (CSI keeps promiscuous live). Panel shows
    `L<n>` on the `fr:` line. CSV = chosen format (compact event log, like macwatch/bmon style).
  - Keys now: a/l, c, t, n, s, h, q. Footer/help/man/docs/CLAUDE/sdcard all updated; help=13/13, man=31/31.
- Log `source` field upgraded to TWO columns **full bssid + ssid** (was last-2-mac-bytes): csv header
  now `time,event,motion_pct,thresh_pct,zones,mode,channel,bssid,ssid`; captured from scan (auto) or
  `WiFi.BSSID()`/`WiFi.SSID()` (connected). Build fix: connected BSSID uses the no-arg `WiFi.BSSID()`
  (the `BSSID(idx)` overload is scan-only → "invalid conversion" compile error).
- ✅ **FULLY HW-VERIFIED** (T-Deck-Plus, committed d775aa3 + a6f899b): `csi auto` locks an AP + detects
  motion; NBVI `[n]`, adaptive `[t]`, SD log → `/apps/csidetect/NNN.csv` (full bssid+ssid columns),
  and the panel `AUTO/LINK c<ch> <ssid>` source line all confirmed working. csidetect upgrade DONE.
  Note: adaptive `[t]` can read high if you nudge the margin up (user confirmed the 97% in an early log
  was them adjusting it, NOT a bug) — asymmetric/capped tuning deliberately NOT done (not needed).

## Session 2026-06-28 (macwatch/mw — ✅ HW-VERIFIED)
- User field-tested `mw` end-to-end on hardware: works, all good (add/presence/proximity/bg + the
  idempotent `mw bg/stop` tweak committed aad4dad). No longer "untested" — removed from CLAUDE Pending
  Features. Also `lock boot` + SD-while-locked logging confirmed this session (bmon).

## Session 2026-06-25 (macwatch/mw + BLE device identification — built, UNTESTED, committed 9cec839 + a853f62)
- **New `macwatch`/`mw` command (Bluetooth)** — watch specific WiFi/BLE MACs (or vendor OUI
  prefixes) with a name + proximity gate; **beep + screen-wake + centered popup on arrival**.
  Built as "trackme-lite" (dual-radio loop, presence state machine). Full design + reuse map in
  [[project_macwatch_idea]].
  - **Foreground** = bmon-style table UI. Add flow uses a **STABLE candidate list** ([[ui_rules]]:
    manual-refresh, not auto-updating, so selection doesn't jump) — `[u]` rescan · `[n]` near-only
    filter · `[h]` silent live-RSSI **hunt meter** (find which direction a tag is) · trackball select.
  - **Background** = BLE-only (`mw bg` / `mw stop`), `MW` status-bar badge, hooked into
    `getKeyboardInput()` like `ec bg`. Presence SM (3-min timeout) **persists across re-entry** so it
    doesn't re-alert; foreground exit **resumes bg** if it was running.
  - **No-Esc UI** (T-Deck keyboard has no Esc/arrows): cancel via trackball click / `q`.
- **BLE device identification (`ble_ident.h`, header-only)** — Bluetooth SIG company IDs + Apple
  Continuity message types + AirPods/Beats proximity-pairing model IDs. Wired into **bmon** (full
  label e.g. `Apple AirPods 2 (pairing)`), **sbl** (vendor name + new selectable table, captures BLE
  company id), and the **mw add** list.
- Surfaces: new `bluetooth/tools/macwatch/` (macwatch.cpp/.h + macwatch_bg), `ble_ident.h`,
  command_manager.cpp (reg), sdcard_manager (`/apps/macwatch` tree + watchlist.csv/events.csv),
  display_manager (`setMwActive()` MW badge), task_manager (`TaskResult.data` 48→56 so BLE company-id
  field never truncates), bmon.cpp, scanblue, NOTICES (credit Bluetooth SIG, furiousMAC/continuity,
  librepods, AppleJuice).
- ⚠️ **NOT yet compiled/flashed** — pending on-device verification. See verify checklist in [[project_macwatch_idea]].

## Session 2026-06-23 (rm: directory removal + dir autocomplete — ✅ tested, committed 58d1207 + pushed)
- **`rm -d <dir>`** = recursive directory delete (user hit: `rm` couldn't remove dirs). New static
  `rmTree()` in sdcard_manager.cpp — two-phase per level (gather child names into `vector<String>`,
  close dir, then delete) to avoid mutating a dir while its `openNextFile()` iterator is live;
  `name()` is the basename on this core so paths are rebuilt. Asks `y/N` first (recursive = dangerous);
  refuses to delete `/` or the cwd/any ancestor of it. Plain `rm <dir>` (no `-d`) now refused with a hint.
- **Autocomplete fix**: `rm` was `COMP_FILE` (files only) → couldn't tab into/complete dirs; changed to
  `COMP_ANY` (files + dirs), matching `cat`. Now you can tab through nested folders + target a dir for `-d`.
- Surfaces: sdcard_manager.cpp (removeFile rewrite + rmTree), command_manager.cpp (reg COMP_ANY + usage),
  man_pages.cpp, README.md, docs/sd-commands.md, CLAUDE.md (also fixed stale `sdrm/srm` → `rm/rm`).

## Session 2026-06-23 (text editor `edit`/`ed` — nano-style SD editor)
- **New module `core/editor/text_editor.cpp/.h`** — free function `runEditor(char*)` (wardrive
  pattern), registered `edit`/`ed` (SD Card, COMP_ANY). Command count 57→**58**.
- **Control scheme is forced by the I2C keyboard**: it's a separate ESP32-C3 that resolves
  Alt/Shift/Sym internally and sends ONE byte/key — so NO Ctrl/Esc/arrow codes ever reach us
  (verified via DeepWiki T-Deck keyboard-interface + the existing `getKeyboardInput()` 1-byte read).
  Therefore: keyboard = text (type/Bksp/Enter-split), **trackball U/D/L/R = cursor** (wraps across
  line ends), **CLICK = command menu** (Save/Save As/Find/Go to line/Cut/Paste/Exit). No `q` quit
  (`q` is typeable) — exit only via menu; unsaved → `[s]`save/`[d]`discard/click-cancel.
- Renders direct to global `tft` on the 6px Font0 grid (ssh-style): COLS=52 × ROWS≈12, inverse-cyan
  block cursor, h/v autoscroll, scrollbar. Buffer = file-static `std::vector<String>`, ≤500 lines
  (bigger → read-only, can't truncate on save), freed on exit (clear+shrink_to_fit, rule 5c).
  Save = `SD.open(path, FILE_WRITE)` (truncates) + `\n`/line; missing path → new file on first save,
  with `ensureParentDirs()` recursively creating any missing parent folders (ensureDir per prefix).
- Lock-aware: all draws guarded by `isBlocked()`, full redraw on `consumeJustUnlocked()`, and ALL
  input handling skipped while blocked (locked keys/trackball can't edit invisibly). Sub-loops
  (runMenu/promptLine/confirmSaveExit) all lock-aware. No WiFi → no GDMA concern.
- Surfaces: text_editor.cpp/.h, command_manager.cpp (fwd-decl + reg), platformio.ini (-I core/editor),
  man_pages.cpp (entry), CLAUDE.md (module note + SD cmd line), README.md (table row), docs/sd-commands.md.
- ✅ **HW-VERIFIED 2026-06-23** — flashed to T-Deck Plus, editor works (type/edit/save/menu/new-file).
  Build fix: removed direct `#include "LGFX_T-Deck.h"` (no include guard → redefinition when also
  pulled via display_manager.h); include only display_manager.h.
- **Polish pass (2026-06-23, ✅ tested + committed 58d1207 + pushed):** added 4 improvements — (1) **per-row dirty rendering**
  (`g_rowDirty[]`/`g_allDirty`/`g_hintDirty` → `flushDraw()`; typing/cursor redraws only affected
  row(s)+title, kills flicker); (2) **single-level undo** (`snapshot()`/`doUndo()`, coalesced per
  run via `g_lastAction`, freed on exit); (3) **auto-indent** on Enter (new line inherits leading
  whitespace, skipped if cursor inside the indent); (4) **trackball acceleration** (`accelStep()` —
  same-dir <90ms doubles step→16, fast roll pages big files) + **Top/Bottom** menu jumps. Menu now
  10 items (added Top/Bottom/Undo). Surfaces re-updated: text_editor.cpp, man_pages, CLAUDE.md,
  docs/sd-commands.md. Trace-verified (String ref-before-realloc in Enter auto-indent checked OK).

## Session 2026-06-22 (command merges — free up the 64-cmd cap, 62→57)
- **3 low-risk merges** folding sibling commands into sub-commands (kept all underlying functions;
  just collapsed the registrations + routed sub-args via `Utils::matchesCmd`):
  1. `wifiexport`+`clearwifi` → **`wifipass`**: `wp` (show) / `wp export` / `wp clear`.
  2. `topscan` → **`portscan`**: `ps <ip> <s> <e>` (full) / `ps top <ip|#>` (top ports). Routed by
     stripping "top" + spaces, passing rest to `topPortScan` (NULL if empty).
  3. `spktest`+`mictest`+`loratest` → **`test`/`tst`**: `test spk|mic|lora`, no-arg prints usage.
- Net: 62→**57 commands** (5 slots freed). Updated EVERYWHERE: command_manager.cpp (regs + kArgHints
  autocomplete entries for wp/ps/test), man_pages.cpp (merged 6 man entries → 3), README.md, CLAUDE.md
  (cmd lists), docs/* (wifi.md, wifi-scan.md, wifi-credentials.md, network.md, portscan.md,
  netdiscover.md, diagnostics.md, audio.md, troubleshooting.md, system.md, index.md, workflows.md).
- ESP-NOW (`es/est/ec/ev`) and USB families left FLAT — flagship apps, rename cost too high.
- ✅ **HW-VERIFIED 2026-06-23** — merged commands (`wp export/clear`, `ps top`, `test spk|mic|lora`) work on-device.

## Session 2026-06-22 (deep sleep command `sleep`/`slp` — ✅ HW-VERIFIED)
- **New `sleep`/`slp` System command** = ESP32-S3 deep sleep (~240uA), modelled on the LilyGo
  UnitTest.ino example. `PowerSaveManager::deepSleep()` (core/system/powersave_manager.cpp): WiFi→STA
  + 150ms settle (GDMA), fade backlight, `tft.sleep()` (LGFX SLPIN), `SPI.end()`+`Wire.end()`,
  `esp_sleep_enable_ext1_wakeup(1ULL<<BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ANY_LOW)`, `esp_deep_sleep_start()`.
- **Wake = trackball CLICK only (GPIO0, RTC-capable).** Keyboard CANNOT wake it — keyboard INT is
  GPIO46, not an RTC pin (ESP32-S3 ext1 needs GPIO0-21); also peripherals power down. Wake = full
  reboot via setup() (RAM-only state lost). BOARD_POWERON(GPIO10) deliberately NOT held → drops for
  the 240uA figure, setup() re-drives HIGH.
- **Command-only by design** (user requirement): deepSleep() is invoked ONLY from the `slp` lambda;
  pwrsave's inactivity timeout path is untouched (still just dims/blanks backlight). No auto-sleep.
- Gotcha fixed: `ESP_EXT1_WAKEUP_ALL_LOW` is deprecated on S3 → use `ESP_EXT1_WAKEUP_ANY_LOW` (same
  for 1 pin). LGFX `getBrightness()`/`sleep()` verified in LGFXBase.hpp.
- Surfaces: powersave_manager.cpp/.h, command_manager.cpp (reg + count now 62/64), man_pages.cpp
  (man entry), README.md, docs/system.md, CLAUDE.md System cmd line. To extend wake to trackball
  *roll*: add BOARD_TBOX_G01/G02/G03/G04 (GPIO 3/2/15/1, all RTC) to the ext1 mask.

## Session 2026-06-21 (wardrive: switched to Bruce's SYNCHRONOUS scan)
- **Replaced the async scan with synchronous `WiFi.scanNetworks(false,true)`** (Bruce's method,
  `BruceDevices/firmware` src/modules/gps/wardriving.cpp — studied via GitHub API/raw). RATIONALE:
  async kept producing a "scans APs but logs nothing" bug class (failed async *start* read back as
  `WIFI_SCAN_FAILED(-2)` and mistaken for a finished empty sweep); sync can't get wedged, so the
  ENTIRE async surface is deleted — no state machine, no watchdog, no `esp_wifi_scan_stop()`, no
  `esp_wifi.h`. Reliability from not using async (same as Bruce/Marauder).
- KEPT all project-specific wins: Phase 1 fix-gate (NVS-collision fix — Bruce has no equivalent),
  lazy file, PSRAM dedup, hidden-AP logging (`show_hidden=true`, which Bruce omits), GDMA-safe
  staged write, gate diagnostic. ADDED from Bruce: `vTaskDelay(1)` every 32 rows (WDT yield) +
  `vTaskDelay(120)` after `scanDelete()` before SD I/O. Cost: UI freezes ~3-4s/sweep (shown as
  "Scanning..."), `[q]` honoured in the 1s inter-sweep pace window.
- **AccuracyMeters changed HDOP×5 → HDOP×1** to match Bruce's convention (user endorsed Bruce's
  method). Altitude unchanged (`gm.altitude()` == Bruce's `gps.altitude.meters()`, confirmed identical).
- Web-verified the whole design against Bruce + Marauder; altitude/HDOP/dedup/scanDelete all match.
- ✅ **FIELD-VERIFIED 2026-06-22** — sync scan method works on T-Deck Plus (wardrive logs APs to WiGLE
  CSV with GPS fix). The "scans APs but logs nothing" async bug class is gone for good.

## Session 2026-06-21 (wardrive: real alt/HDOP + logging-regression fix)
NOTE: the async logging-regression fix + scan self-healing below were SUPERSEDED same day by the
sync switch above (async removed entirely). Alt/HDOP exposure in GpsManager still stands.
- **GpsManager now exposes `altitude()` (m MSL) + `hdop()`** — two `volatile float` members read in
  `gpsTask` from TinyGPS++ `altitude.meters()` / `hdop.hdop()` (verified the vendored
  `lib/TinyGPSPlus` API: both inherit `TinyGPSDecimal`, units already real). Reset in `start()`.
- **wardrive CSV upgraded**: AltitudeMeters = real `gm.altitude()` (was hardcoded `0`); AccuracyMeters
  = `hdop×5 m`, falls back to the old sats heuristic (8/20 m) when HDOP is 0.
- **FIXED the "scans APs but logs nothing" async regression.** ROOT CAUSE: the loop ignored
  `WiFi.scanNetworks(true,..)`'s return and set `scanning=true` unconditionally; a failed *start*
  read back as `WIFI_SCAN_FAILED(-2)`, and the old `n != WIFI_SCAN_RUNNING` test mistook `-2` for a
  *finished* sweep → `scans++`, torn down, zero rows logged. Now a **three-state machine**: only
  `WIFI_SCAN_RUNNING` enters the wait state; `scanComplete()` routes `-1`→wait, `-2`→discard+retry
  (not counted), `>=0`→real sweep. Also `logged`/`lastWrote` now count only **successful**
  `appendLine()` returns (failed write no longer inflates the total).
- **Added a one-glance gate diagnostic** on screen: `sweep n=.. fix=.. new=.. wr=..` (APs returned /
  GPS valid / rows staged / rows written) — per [[feedback_rules]] #7, one flash now pinpoints any
  remaining logging-gate failure instead of guessing.
- Surfaces updated: gps_manager.h/.cpp, wardrive.cpp, docs/wardrive.md, CLAUDE.md.
- **Scan self-healing (added later 2026-06-21, not yet field-tested):** shared `onScanFail()` lambda
  handles start-fail / `-2` / watchdog uniformly — `esp_wifi_scan_stop()` (the REAL abort) +
  scanDelete + paced retry; `SCAN_TIMEOUT_MS=8s` watchdog aborts a sweep that started but never
  completes. NOTE (caught in self-review): `WiFi.scanDelete()` only frees results and
  `WiFi.mode(WIFI_STA)` short-circuits to a no-op when already STA — so `esp_wifi_scan_stop()` is the
  only thing that actually un-sticks a wedged async scan. Builds clean; verify on the outdoor run.
- ✅ **HW-VERIFIED 2026-06-21** — flashed to T-Deck Plus (build RAM 60.8% / Flash 37.4%), wardrive
  logging works again. (Flashed by Claude this session via `~/.platformio/penv/bin/pio run -e
  T-Deck-Plus -t upload --upload-port /dev/ttyACM0`. Port is `/dev/ttyACM0` (`root:dialout`).
  UPDATE 2026-06-22: user is NOW in `dialout` (re-login done) — no `chmod` needed anymore. The
  PlatformIO `99-platformio-udev.rules` warning on upload is cosmetic (dialout already grants access);
  optional silence = install the rules file to `/etc/udev/rules.d/`.)

## Session 2026-06-21 (jg ble / btkbd — Linux BlueZ discoverability)
- **FIXED: `jg ble` (BLE mouse jiggler) + `btkbd` invisible in Ubuntu Bluetooth Settings** (paired fine
  on Windows). Cause: `beginHid()` advertised appearance + HID service UUID but never set the device
  name — NimBLE v2.x doesn't auto-include it, and BlueZ/GNOME only lists a device once it sees the
  COMPLETE LOCAL NAME (Windows is lenient, lists by HID appearance). Fix = buddy-style
  `adv->enableScanResponse(true); adv->setName("T-REX-KBD");` before `startAdvertising()`. Shared
  `beginHid()` so both btkbd + jg ble fixed. This is exactly [[nimble_v2_rules]] #1 — the code just
  wasn't following it. ✅ **HW-VERIFIED 2026-06-21** (user confirmed it shows up + works on Ubuntu).

## Session 2026-06-20 (wardrive)
- **wardrive/wd** (Plus only) — WiFi scan + GPS → WiGLE WiFi-1.4 CSV. New module `wifi/tools/wardrive/`.
  Field-tested working (logged real APs). Key hard-won fixes during bring-up:
  - **ROOT CAUSE of "90 APs then 0 after GPS fix":** GpsManager does a one-time NVS *flash* write
    on first fix (`saveGpsFixFlag`); a flash write during a live WiFi scan corrupts the scan engine
    (all later scans return 0). Fix = **Phase 1 waits for the first fix with radio IDLE, then Phase 2
    scans.** `_fixSaved` latched so re-fix never re-triggers it. (This is a general GDMA-class lesson:
    flash/NVS writes collide with live WiFi scans just like SD writes do.)
  - **async** scan (not sync) for responsive UI — sync froze the screen ~3-4s/sweep. Async is safe
    once Phase 1 is in place (the earlier async "last saw 0" was the NVS collision, not async itself).
  - **Lazy file** via `ensureFile()` — CSV created only on the first AP logged; no-fix/quick-quit
    sessions leave no empty file.
  - SD rows staged in RAM `vector<String>`, flushed after `WiFi.scanDelete()` (radio idle) — GDMA-safe.
  - On-screen `Heap NNk min NNk` leak watch ("scanNetworks returns 0" is documented as heap exhaustion).
  - **WiGLE 1.4 format verified against the official spec** (api.wigle.net/csvFormat-1_4.html) + Bruce
    + dkyazzentwatwa/esp32-gps-wifi-wigle references. MAC lowercase, FirstSeen=GPS UTC, Alt=0 (not exposed). [superseded 2026-06-21: real alt+HDOP now]
  - All surfaces updated: command_manager (+arg none, no-arg cmd), man_pages, docs/wardrive.md, docs/index,
    README, CLAUDE.md, sdcard_manager (SD_DIR_WARDRIVE + tree + apps README), platformio.ini (-I).
  - Lesson (user feedback): I can't flash hardware — stop iterating speculative fixes in chat; instrument
    + get one diagnostic datapoint instead. See [[feedback_rules]].

## Session 2026-06-20 (later)
- **jg ble — BLE mouse jiggler** (committed `2337d4e`). `jg` now takes a transport arg:
  `jg`/`jg usb` = USB HID (unchanged), `jg ble`/`bt` = BLE HID. Reuses the btkbd HID stack —
  extracted btkbd's NimBLE init→`BleKeyboard::beginHid()` and cleanup→`endHid()` (shared by
  `start()`+`jiggle()`; endHid keeps the "do NOT deinit(true)" rule). New `BleKeyboard::jiggle()`:
  advertise T-REX-KBD → wait pair → `sendMouseMove(±2,0)` every 30s, with disconnect/reconnect
  + lock-unlock redraw. Updated all 4 surfaces: command_manager dispatch **+ arg-autocomplete
  table** (`jg → usb ble`), man_pages, docs/jiggle.md.
- **Bug fixed — jg re-ran after `q`:** jigglers only read the keyboard, so latched trackball
  clicks + stale direction edges queued during the session leaked to the CLI on exit (stale edge
  reloads "jg ble" from history → latched click re-executes it). Fix: drain
  `while(getTrackballEvent()!=TBALL_NONE){}` + `clearPendingClicks()` before returning. Applied to
  USB jiggler too (same latent bug). btkbd unaffected — it reads the trackball every loop.
- Reminder: btkbd exits via center-hold 1.5s (easy to overshoot into the 3s lock trigger); jg ble
  exits via `q` so no exit-vs-lock conflict.

## Session 2026-06-20
- **Lock-unlock repaint — crack screens + ssh** (field-validated ✅). Root bug: an app's
  MAIN loop handling `consumeJustUnlocked()` does NOT cover its long-running SUB-loops.
  Fixed the crack-progress + result/`[q]`-wait loops in `ws` `crack()`, `pm` `crack()`,
  `cc` `runCrack()` (+ its wordlist `pickList` inner wait), and `karma` `karmaCrack()` —
  each: static header wrapped in a `drawHeader()`/`drawStatic()` lambda, `consumeJustUnlocked()`
  added to every crack loop + result wait → repaint header+status. **ssh** also fixed: it
  draws terminal direct to `tft` (bypasses DisplayManager) → guarded `termRender()`+`drawHeader()`
  with `isBlocked()` (was a content-LEAK: SSH output painting over the lock screen) + task-loop
  `consumeJustUnlocked()` → `s_allDirty=true`+`redrawHeader()`. Details in [[project-lock-display-blocking]].
- Known minor gap left: karma's `[any key] back to list` result waits (span two funcs).
- **Improvement flagged (not done):** ws/pm/cc/karma crack screens are ~90% duplicate code
  (header+source-picker+Trying-loop+result+SD-log); only the verify call differs
  (`verifyHandshake` vs `verifyPMKID`). Extract a shared `wifi/core/wpa_crack_ui` helper taking
  a verify callback — kills the dup (rule 5b) and centralizes the lock-unlock handling so the
  next crack screen can't regress. Lower-risk than the dot11_tx/promisc extraction (backlog #2).
- Changes uncommitted (user compiles/flashes manually).

## Session 2026-06-14
- **ssh** — autocomplete host-name reload made stack-light (no 3.2KB HostProfile[] in 8KB main task); committed `681cc7f`. Terminal buffers (s_buf/s_col ~12.5KB) moved to lazy PSRAM, freed on exit; committed `b5d18a7` (pushed manually by user).
- **karma/km Phases 1-3** — all field-tested working. P1 probe harvest+table; P2 PNL fingerprinting (defeats MAC rand) + DEVS view/intel cards; P3 WPA2 handshake bait (save-only .cap, no on-device crack) via `km hs <ssid>` + interactive `[h]` from list. HW-confirmed: own softAP M2 reaches promiscuous → deauth-free WPA2 capture works on S3. New module `wifi/attacks/karma/`. Full status in [[project_karma_plan]]. Next: Phase 4 open+captive portal. NOT committed yet.
- User compiles/flashes manually — do NOT run `pio run` (saved to [[feedback_rules]]).

## Session 2026-06-13
- **ssh/sc** — interactive SSH client, built + first-connect working. LibSSH-ESP32 (`ewpa/LibSSH-ESP32`, real lib_deps). Runs in a 50KB FreeRTOS task; reuses `cw` STA conn; password auth + PTY shell. Colour terminal (ANSI-16, scrollback ring 120 lines, trackpad scroll, scrollbar). KNOWN: HW-SHA concurrency caveat (can't disable on precompiled core) → suspect for connect-phase crashes. `/apps/ssh/` reserved for known_hosts/keys (not written yet). Full ref: [[project_ssh_client]].
- **espchat PIN fix** — known contacts skip PIN (reuse stored LMK+channel); private always inits via ecCoreInitWithLmk (fixed empty-PIN→public fallthrough). PIN = first-time pairing only.
- **espvoice diagnostics** — drp/heap on stats line, esp32_exception_decoder, RX mic-DMA drain (crash still under watch).
- Memory cleanups: bmon marked implemented; next_steps/MEMORY reconciled.

## Session 2026-06-12
- **espvoice/ev** — ESP-NOW half-duplex walkie-talkie, field-tested working (both directions + sound). HD voice via **G.722** (16kHz/64kbps Mode 1), vendored public-domain `lib/libg722/` (sippy/libg722, auto-discovered by LDF, NOT in lib_deps). Wire: `EvMsg{type=0x02,kind,seq,g722[160]}`=163B, kind 0=voice 1=EOT. PTT = **TOGGLE** (keyboard has no key-up). Walkie signaling: `RECEIVING <mac>` tag + Roger beep both ends (EOT ×3 + 500ms silence fallback). App-local audio: RX vol 0-150% (`+/-`, capped — higher hard-clips/brownout), TX mic gain 0-37.5dB (`o/p`, clean louder) — neither touches global vol. Channel `,/.`
- **CRASH FIX** — installing/uninstalling I2S drivers on every PTT toggle while ESP-NOW DMA live = crash (brownout). Fix: install BOTH I2S ports once at start, keep resident (mic=I2S1, speaker=I2S0, separate peripherals), gate by PTT state. Also explains "300% volume → crash": clipped full-scale audio at high gain browns out the rail.
- **mictest/mt** — ES7210 mic diagnostic: live level meter, VAD, record 3s + replay. Fixed 2x-slow/devil-voice (ALL_LEFT delivers 2 int16/sample — must de-dup `raw[2*i]`).
- **Board-gate correction** — mic+speaker are on BOTH T-Deck and T-Deck Plus; only GPS is Plus-only. Removed wrong `#ifdef BOARD_TDECK_PLUS` from mictest+espvoice. (Old peripherals memory wrongly said mic=I2S_NUM_0 — it's I2S_NUM_1.)
- Docs/man/README/CLAUDE.md all updated for ev + mt.

## Session 2026-05-29
- **buddy** — NimBLE v2.x name fix (`enableScanResponse`+`setName`); `onConnect` sets `s_connected` immediately; stale bond detection (reasons 0x05/0x06); cleanup removes `init("T-REX")` — field tested, working
- **btkbd** — auto-bond-delete on auth fail; stale bond UI — field tested, working
- **ble_spam** — `bsRestoreStack` no longer re-inits; Android wait 10s per cycle; `spamAll` Android slot 8s
- **fast_pair** — `scan()` rewritten: FreeRTOS task removed, `start(0)` + millis() 5s loop; `spam()` wait 10s; hijack prompt cursor fix (promptY saved before poll loop)
- **wifi_functions** — `readPassword()` cursor fix: inputY saved before loop, redraws at fixed Y

## Session 2026-05-25 (summary)
Lock screen write-block + unlock auto-redraw; backspace hold-repeat; btkbd/bk BLE HID keyboard+mouse; buddy MITM bonding; WiFi wrong-password fix; NTP sync fix; status bar 3s live refresh; wguard WiFi isolation; `sdrm→rm` rename.

## Not Yet Built
- LoRa scanner — lorascan/ls
- GT911 touchscreen (pins: project_future_peripherals.md)
