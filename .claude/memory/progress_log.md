---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

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
- NOT yet flashed/tested after the merges.

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
- macwatch/mw — MAC proximity watchlist
- LoRa scanner — lorascan/ls
- bmon — passive BLE ad sniffer (iBeacon/Eddystone/cleartext, PCAP linktype 251)
- ES7210 mic, GT911 touchscreen (pins: project_future_peripherals.md)
