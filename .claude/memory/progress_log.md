---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

## Session 2026-07-27 (gm fixes + license/SD reorg + Network MITM suite + keyboard plan) — ALL COMMITTED + PUSHED to feature/pentest-enhancements
- **2026-07-29: `wps` command BUILT (WiFi) — recon + PIN-calc + PBC, builds clean, NOT HW-tested.**
  Research settled (do NOT re-litigate): **automated WPS PIN brute-force / Pixie-Dust is IMPOSSIBLE on
  ESP32** — `esp_wps_config_t` has NO pin field (WPS_TYPE_PIN self-generates the enrollee PIN), registrar
  mode unsupported (`ESP_ERR_WIFI_REGISTRAR`), Pixie needs M1/M3 crypto the closed stack hides; no ESP
  framework (Marauder/Bruce/esp32-wifi-penetration-tool) ships one. So `wps` does the honest max: (a)
  `wps <idx>` beacon promiscuous-capture → WPS-IE decode (version/locked/methods + **device mfr/model/
  name/serial leak**) → `/apps/wps/wps.csv`; (b) BSSID→PIN calculator (ComputePIN+checksum, DISPLAY-only,
  test w/ Reaver); (c) `wps pbc <idx>` = the one real cred path (esp_wifi_wps PBC → SSID+PSK on button
  press → creds.csv). Reuses `sw` scan (added `getNetworkWps`). `wifi/attacks/wps/`. Commit 743a212.
- **2026-07-28c: `arpspoof`/`as` HW-VERIFIED by user ("works great") → PROMOTED OUT OF [EXP].** Removed the `[EXP]`
  tag everywhere (cmd desc, man, README, CLAUDE.md, docs/network.md, on-screen UI, arpspoof.h). `responder`/`rsp`
  STAYS `[EXP]` (SMB2 still unverified). Note: `as nd#` vs `ns#` was a targeting/index confusion, not a bug.
- **`gm` post-exit lock loop FIXED (HW-verified by user).** Root cause: `nesI2sInit()` omitted `.mck_io_num`
  → designated-initializer zero routed I2S MCLK onto **GPIO0 (= BOARD_BOOT_PIN, trackball click)** → pin read
  LOW after a game → lockscreen 3-s trackpad-hold detector fired on a loop. Fix: `.mck_io_num = I2S_PIN_NO_CHANGE`
  + `pinMode(BOARD_BOOT_PIN, INPUT_PULLUP)` after `i2s_driver_uninstall`. **RULE: always set `.mck_io_num`
  explicitly in any `i2s_pin_config_t`.** Also: gm now returns to the ROM library (not CLI) after a game,
  scrollbar + controls toast, session-wide auto-lock suppress.
- **Anemoia NES core license CORRECTED: it's `Shim06/Anemoia-ESP32` under GPL-3.0** (the old NOTICES claimed a
  nonexistent `TotalCaesar659` fork under MIT — that repo is a 404). Shipped verbatim GPLv3 LICENSE +
  README.T-REX.txt in `games/nes/anemoia/`; fixed NOTICES #20, CLAUDE.md, source header.
- **SD reorg: NES + Notes moved UNDER `/apps`** (owner decision): `/apps/nes/roms`, `/apps/nes/states`, `/apps/notes`
  (were `/roms/NES` + `/states` + `/notes`). bus.cpp patched to `/apps/nes/states` (buffers 32→40). Dropped the old
  "notes at root = disguise" rationale. User's SD card migrated in place.
- **Network MITM suite BUILT + reviewed + UI-polished, AWAITING USER LAB TEST** (`docs/plans/network-mitm-suite.md`).
  **`arpspoof`/`as <victim> [gw]`** = L2 ARP poisoning via raw ethernet+ARP pbuf → `netif linkoutput` (driver
  encrypts w/ PTK; mirrors Bruce ARPoisoner). Victim = ip/nd#/ns# via new shared `resolveNetTarget()`; heals both
  caches on exit; honest "redirect/DoS, no forwarding (1 radio)". **`responder`/`rsp`** = LLMNR(5355)+NBT-NS(137)
  poisoner (raw lwip udp pcbs) + fake HTTP :80 NTLM catcher → NetNTLMv2 (hashcat -m 5600) → `/apps/responder/hashes.txt`.
  Review fixes: dnsParseName OOB guard, LLMNR reply-buf clamp, Type-3 buffer 1024→2048. HTTP-NTLM only (SMB=2b).
  lgandx/Responder credited (methodology). Commits 49f2546/b08532f/8b23a9e.
- **Keyboard-firmware plan written + in-repo** (`docs/plans/keyboard-firmware.md`): fork `hreikin/tdeck-keyboard`
  (MIT, has HELD state) → add long-press as a generic app event + real modifiers; **host auto-detects the T-REX
  keyboard via a version-sentinel byte** and only enables extended features on it (else = today's legacy behavior).
  NOT started. **Also paused: MITM plan Phase-2b SMB catcher.**
- Behavioral: **keep commit messages general — no host paths / usernames / device serials** (user-requested,
  see [[feedback_commit_message_style]]).
- **2026-07-28 follow-ups (COMMITTED+PUSHED, still awaiting lab test):** (a) `arpspoof` now **captures+logs
  the redirected uplink** — promiscuous sniff of the AP-relayed victim frames → dst IP + DNS domain (UDP 53) /
  HTTP Host (TCP 80) / HTTPS → live "victim reaching:" list + `/apps/arpspoof/NNN.csv` (`asCapCb`/`asParseFrame`,
  isoscan offsets; ScopedPromiscPause for SD). So the blackholed victim's requests are visible. (b) `responder`
  expanded to the user's full pick: **mDNS(5353)** poisoning added; **every** poisoned query logged to
  `poison.csv`; **NTLMv1 (-m 5500)** + **HTTP Basic** capture besides NTLMv2; **WPAD PAC** served; **best-effort
  SMB2(:445)** catcher (NEGOTIATE + SPNEGO-wrapped Type-2 challenge → Type-3 via shared `parseType3`) — **SMB is
  UNTESTED, SMB2/SPNEGO framing may need iteration vs a real Windows victim.** Commits 36c4b22 / 3485776.
- **`nd#` vs `ns#` note:** the two lists number devices independently — `nd3` ≠ `ns3`. Target by the index shown
  in that tool's own table, or by IP. Under client isolation `nd` may only see the gateway (use `ns`).
- **2026-07-28b review fixes + features (COMMITTED+PUSHED):** (c) responder review: bound HTTP/SMB socket reads
  (`setTimeout 250`) + poll `q` inside handlers so quitting stays responsive; **dedup captures** (last-8 NT-response
  keys) so a re-auth doesn't re-log/re-count; `poison.csv`→held-open handle. (d) **`rsp passive`** listen-only mode
  (`s_passive` gates every send; no HTTP/SMB servers) — pure recon, transmits nothing. (e) **SD reorg**: responder
  now writes a **per-session folder `/apps/responder/NNN/`** = `hashes.txt` (hashcat NetNTLM only — Basic no longer
  pollutes it) + `creds.txt` + `captures.csv` + `queries.csv`. (f) **BUG FIX — undercover passphrase exit** failed
  after a typo+backspace: `cover::feedPassphrase` ignored backspace so the rolling window kept stale chars. Reworked
  to apply backspace as undo in a 65B buffer + match the last `plen` chars (`cover_kit.cpp`). Commits 7519f38 /
  97925c1 / 38f46f9. Docs/man/autocomplete(`rsp passive` kArgHints)/README/network.md all updated.

## Session 2026-07-09b (wpa3down Phase 2 — WPA3 transition-downgrade attack) — code-complete, UNCOMMITTED, NOT HW-tested
- **Built Phase 2 of the wpa3down plan** (the core downgrade attack) right after the `sw` manager rework. New module
  `wifi/attacks/wpa3down/wpa3down.{cpp,h}`, free fn `runWpa3Down(char*)`, registered `wpa3down`/`w3d` [EXP] WiFi.
- **Almost pure ORCHESTRATION of karma's `roguehs` engine (rule 5b).** Key discovery: `roguehs`'s beacon is ALREADY
  WPA2-PSK-only (RSN AKM `0x000FAC02`, no SAE) — i.e. it IS the "WPA2-only rogue AP" the downgrade needs; it injects
  its own M1 (known ANonce) + sniffs the victim's M2 → crackable half-handshake, and keeps beacon+M1+M2 raw frames.
  So Phase 2 ≈ karma `km hs` + a deauth of the REAL AP + the `sw` TD-target filter.
- **Flow:** require prior `sw` scan → TD picker (`getNetworkSec==WSEC_TD`, trackball/Enter) or `w3d <idx>` →
  `roguehs::begin(ssid,ch)` → loop `roguehs::poll()` + broadcast-deauth real AP every ~800ms (26-byte frame,
  src=real BSSID, `WIFI_IF_STA`, same channel, no hop — copied from karma `injectDeauth`) + live Prb/Ath/Asc/M2
  (chrome-once + stats-in-place, flicker-free per the isoscan lesson) → on `gotM2`: `roguehs::end()` (idle STA,
  GDMA-safe) THEN save `/apps/wpa3down/<ssid>.cap` (beacon+M1+M2, shared `pcap::writeRecord` lt105, never-overwrite)
  → crack with `cc`.
- **Wiring:** `SD_DIR_WPA3DOWN` + ensureDir + apps-README map; platformio `-I .../wpa3down` (src_dir=t-rex-firmware,
  no build filter → auto-compiled); command_manager fwd-decl + register; man `w3d` entry; README row; CLAUDE module
  block + SD-layout line + cmd list; NOTICES #19 (Dragonblood CVE-2019-9494..9499 / TrustedSec / RedLegg — technique
  reference, no code used, per [[feedback_rules]] #8).
- **HONEST SCOPE (in-UI + docs):** works on transition APs with PMF off/optional; **PMF-required blocks the deauth**
  (victim won't drop) → empirical PMF probe + pre-assoc flood = **Phase 3, NOT built**. Pure WPA3 (SAE-only) not
  downgradeable. Output = `.cap` (crackable via cc/hashcat), NOT a separate HCCAPX writer. Inherits `roguehs`'s
  no-HW-ACK caveat (association depends on the client tolerating missing ACKs — HW-proven in karma).
- **Static-reviewed** (roguehs `begin` is self-contained WiFi setup; State persists after `end()` so save-after-end is
  safe — karma's exact idiom; all APIs/getters/include-paths verified). **NOT compiled/flashed** (user builds manually).
- **NEXT:** flash + HW-test on a REAL transition-mode router (PMF off/optional); then Phase 3 (PMF probe + pre-assoc
  flood for PMF-required), Phase 4 (wguard downgrade-detection). See [[project_wpa3down_plan]].

## Session 2026-07-09c (wpa3down Phase 2 — FIRST HW TEST vs a phone hotspot) — attack code works mechanically, capture BLOCKED by client PMF (documented limit)
- **Compile fix:** wpa3down.cpp missing `extern SDCardManager sdCardManager;` — added (was the only build error; the rest compiled clean incl. the sw manager).
- **Test rig:** target = an Android **phone hotspot** SSID `TESTNET`, **WPA2/WPA3-Personal transition**, **ch6 (2437, 2.4GHz)**. Victim = the dev's own **Linux laptop** (Intel `iwlwifi`, iface `wlp0s20f3`, **MAC `xx:xx:xx:xx:xx:xx`** — permanent, NM not randomizing it). The laptop IS the machine Claude runs on → could inspect victim state directly (`iw`, `nmcli`, journal).
- **Symptom:** `w3d` broadcast-deauth counter climbed but **Prb/Ath/Asc/M2 all stayed 0** — victim never dropped, never came to the rogue.
- **Diagnosis from the victim laptop (key facts):**
  - AP RSN: `Authentication suites: PSK SAE` (transition), **`MFP-capable (0x008c)` = MFPC=1, MFPR=0 → PMF OPTIONAL, not required.**
  - Client is on **WPA2-PSK, not SAE** (journal: `associating → 4way_handshake`, no SAE auth step; NM `key-mgmt=wpa-psk`, `pmf=0(default)`). BUT default pmf against an MFP-capable AP → **modern wpa_supplicant negotiates PMF anyway** → forged deauths cryptographically rejected → client won't drop. **This is the wall.**
  - **Phone hotspot BSSID changes ONLY on hotspot off/on toggle — STABLE while it stays up** (saw `xx:xx:xx:xx:xx:xx`, `xx:xx:xx:xx:xx:xx/80`, `xx:xx:xx:xx:xx:xx`, `xx:xx:xx:xx:xx:xx` across toggles; all randomized LA MACs). So the scan→attack BSSID is fine IF you re-`sw` right before and DON'T toggle after.
  - Real AP signal at the laptop ≈ **−32 to −39 dBm (phone sitting on top of the laptop) — can't be moved** per the dev.
- **Code changes made this session (all in wpa3down.cpp, UNCOMMITTED, still NOT capture-verified):**
  1. **Directed deauth** — `w3d [idx] [victim-mac]` (`w3dParseMac`, `w3dDeauthDir` = AP→STA + STA→AP pair). Broadcast deauth is widely ignored (esp. iwlwifi); directed is the effective form when PMF is off. Chrome shows the deauth target; arg-parse guards a MAC token from being read as the scan index. man/README/desc updated.
  2. **Continuous deauth flood** — interval 500ms → **20ms (~50/s)** to make a strong un-movable real AP unusable so the victim is forced onto the rogue.
- **Test results:** 50 directed deauths to the exact victim MAC → still no drop → **PMF confirmed as the blocker.** One run (PMF possibly off) gave **63 probes / 600s but 0 assoc** = background-scan rate, i.e. victim occasionally sees the rogue but re-picks the far-stronger real AP; not truly being kicked.
- **Tried to disable client PMF to prove the pipeline:** `sudo nmcli connection modify TESTNET 802-11-wireless-security.pmf 1` + `up` → **broke the NM profile** ("Secrets were required, but not provided"), laptop auto-jumped to another SSID (`TESTNET2`). Fragile; couldn't get a clean PMF-off run. (To restore TESTNET: `nmcli device wifi connect TESTNET password <pw>`.)
- **HONEST CONCLUSION:** `w3d` is **mechanically working** (rogue WPA2 AP beacons, victim probes it once deauth lands). It CANNOT capture on this target because **a WPA3-transition client that uses PMF is not downgradeable by deauth — by design** (matches the in-UI "PMF? may not drop" note + the plan's honest scope). Blockers = (1) client PMF rejects deauth, (2) unmovable stronger AP, (3) phone rotates BSSID on toggle.
- **▶ RESUME PLAN (next session, when there's usage budget):**
  1. **Prove the pipeline with a NON-PMF victim** — older phone / IoT / ESP32 / older Android usually join a transition hotspot over plain WPA2 **without PMF**. Point `w3d <idx> <that-device-MAC>` at it; continuous deauth should drop it → downgrade → M2 → `/apps/wpa3down/<ssid>.cap`. This is the real end-to-end validation (a modern PMF laptop is the wrong victim).
  2. Decide on the **20ms deauth flood** — may be too aggressive / could starve the rogue's own probe/auth/assoc responses. If a real test shows **Asc rising but M2 not**, add a "pause deauth while the victim is associating to OUR rogue" guard. Otherwise consider ~20/s (50ms).
  3. **Live-BSSID tracking** = LOW priority for this phone (BSSID is stable while up; re-scan suffices). Only needed for APs that rotate BSSID mid-session.
  4. **Phase 3 (PMF probe + pre-assoc flood)** — note pre-assoc flooding does NOT rescue an already-connected PMF client (can't force re-assoc without a drop, which PMF prevents); it only helps NEW associations. So it won't fix this exact laptop scenario. Lower value than first proving capture on a non-PMF client.
  5. **Commit** the whole batch (sw manager + wpa3down + Phase-1 detection) once capture is HW-proven on a non-PMF client (his name, no Co-Authored trailer).
- Full detail + test-rig notes also in [[project_wpa3down_plan]].

## Session 2026-07-09 (sw → WiFi Manager: on/off + interactive connect/disconnect/forget) — code-complete, UNCOMMITTED, NOT HW-tested
- **User asked to rework `sw` into a WiFi MANAGER before continuing the wpa3down plan.** Done (option A + forget +
  security/TD/WPS column). `sw` is now `hasArgs=true`; routes `sw on` / `sw off` (non-interactive radio power) vs
  bare `sw` = interactive manager. Files: `wifi/core/wifi_functions.{cpp,h}`, `wifi/core/wifi_creds.{cpp,h}`,
  `command_manager.cpp`, `man_pages.cpp`, `docs/wifi-scan.md`, `README.md`.
- **`scanWiFiNetworks()` REMOVED**, replaced by `runWifiManager(char*)`. The old paged scan view is gone; `show wifi`
  (`showWiFiResults` → `renderScanPage`) is unchanged and still the passive last-results viewer.
- **Interactive manager** (`renderManager`): top **status line** (`drawMgrStatus`) = current connection `* SSID IP RSSI`
  green / `Not connected` yellow / `Radio: OFF (idle)` red; then a **trackball-cursor selectable list** (highlight bar
  0x0841 + `>`, auto-scroll, MGR_VISIBLE=9 rows from y=74). Actions: **trkbl U/D=select · CLICK or Enter=connect ·
  `[d]`isconnect · `[f]`orget · `[o]` radio on/off · `[l]/[a]` page · `[u]` rescan · `[q]`**. Polls trackball
  (`getTrackballEvent`) AND keyboard each loop, like wm/netspy. Lock-aware via `consumeJustUnlocked` (all draws through
  displayManager → no-op while blocked).
- **CRITICAL preserved:** the manager still populates `scanCache` + `numberOfNetworks` exactly as before (via the same
  static `runAsyncScan`/`populateScanCache`), so the shared **scan-index contract** used by `cw/da/et/hs/ws/pm/bf/wg`
  (`getNetworkInfo`/`isScanDone`/`getNetworkCount`) is intact — no attack command breaks.
- **Connect** reuses `connectToWiFiCommand(idxStr)` (rule 5 — full password prompt + NVS + wpa_supplicant save flow).
- **Radio off = option A** (`s_radioOff` file-static): `WiFi.disconnect(false)` + stay STA idle, **never `WIFI_OFF`**
  (GDMA-safe, matches the 2026-07-08 home-Settings fix). Entering the interactive manager forces radio on to scan.
  Honest limit: `s_radioOff` is a manager-local convenience flag; other WiFi cmds ignore it (cw still connects).
- **Forget** (`forgetNetwork(ssid)`): `Preferences.remove()` (NVS "wifi") + new `removeWpaNetwork(ssid)` in wifi_creds
  (rewrites `/wpa_supplicant.conf` dropping the matching `network={...}` block; preserves globals/header; `ensureBackup`
  first; FILE_WRITE truncate + retry). `[f]` shows a 1.1s toast (Forgot / Not saved / Hidden-can't-forget). Hidden nets
  resolve their ssid via `lookupHidden` before forgetting. GDMA: SD write only from STA (same as appendWpaNetwork).
- **DRY (rule 5b):** extracted `drawNetRowCells()` (SSID/RSSI/SEC-tag incl **TD** yellow/WPS) shared by BOTH
  `renderScanPage` (show wifi) and `renderManager`.
- Static-reviewed (APIs verified: `Preferences::remove` bool, `Utils::printUsage(const char*)`, `WiFi.SSID()/RSSI()`
  no-arg, `TrackballEvent`/`getTrackballEvent`, man `lines[32]` cap OK). Fixed one ordering bug: forward-declared
  `ensureBackup()` before `removeWpaNetwork`. **NOT compiled/flashed** (user builds manually — [[feedback_user_compiles_manually]]).
- **NEXT:** user flashes + HW-tests the manager, then **continue the wpa3down plan Phase 2** (attack: PMF probe →
  WPA2-only rogue AP → ws/pm capture). See [[project_wpa3down_plan]].

## Session 2026-07-08 (home launcher → working apps + AA polish + OOP refactor) — ✅ HW-VERIFIED, UNCOMMITTED
- **✅ HW-VERIFIED end of session:** whole launcher works on glass (7 apps, alarms, AA, covert guarantees, OOP refactor). Last fix: app background not cleared on open (home-grid bled through) → `Ui::appBar()` now `fillScreen(bg)` first (the old `paint()` cleared up front; the new `render()` didn't — faithful-port miss). READY TO COMMIT (his name, no Co-Authored trailer).
- **Notes UI search made functional + moved down** (was clipping the title): live filter (title/body substring), trackball-focusable pill → back-chevron chain, `x` clear, result count. Also **back button trackball-navigable** (cards→pill→back chevron→Home).
- **Home launcher turned into real apps** (`home_ui.cpp` screen-state machine, tiles remapped, Music→Flashlight): **Calculator, Clock (stopwatch+timer), Reminders (SD-persisted `/apps/home/reminders.csv`), Weather (loading spinner), Calendar (proper bordered-grid icon), Flashlight, Settings→Wi-Fi (two-page: menu → Wi-Fi page; scan/connect reuse `wifi_creds`+NVS "wifi", NOT WiFiFunctions' blocked UI)**. Icons redrawn (calendar/flashlight/reminders) + all anti-aliased (`fillSmooth*`/`drawWideLine`) + tile drop-shadows.
- **Alarms via NotificationManager** with a new `notify(level,force,allowCovert)` param — timer=`NOTIF_SUCCESS`, reminder=`NOTIF_INFO` (friendly chimes, NOT the ALERT klaxon — user corrected). Rings **5× until any input dismisses**; banner 12s, touch-dismissable. `allowCovert=true` is the ONLY sound that bypasses `g_covert` (user's "always beep" choice); every other notify stays covert-silent.
- **Bugs fixed:** wguard shield / EC / MW badges were drawn straight to `tft` with no `isBlocked()` guard → **exposed the cover**; now guarded in `display_manager.cpp` (`setWGuardState`/`setEcActive`/`setMwActive`). Cover status-bar signal icon now reflects real `WiFi.RSSI()`. **`WiFi.mode(WIFI_OFF)` GDMA-rule violation** in Settings toggle → now `WiFi.disconnect(false)` STA-idle.
- **Refactor #1 DONE — shared cover shell `cover_kit.{h,cpp}`** (`namespace cover`): owns the PSRAM sprite+`G`, the 4 baked fonts, the passphrase rolling buffer (`feedPassphrase`), and touch-wake (`handleTouchWake`). home_ui + notes_ui both use it via `static lgfx::LovyanGFX*& G = cover::G;` aliases (call sites unchanged). ~120 lines of mirrored plumbing collapsed to one module (rule 5b).
- **OOP refactor DONE (3 staged, each compiled):** `home_app.h` (`HomeApp` abstract base — State/Strategy + `Nav` enum), `home_widgets.{h,cpp}` (`Ui` widget manager: palette + statusBar/appBar/twoButtons/toggle/signalBars/lockGlyph/wifiGlyph/tileIcon/weatherIcon + shared alarm banner/ring), `home_apps.{h,cpp}` (7 app classes: Calculator/Clock/Reminders/Weather/Calendar/Flashlight/Settings, each `HomeApp`, Ui injected via ctor), and **home_ui.cpp rewritten ~1580→325 lines as `HomeLauncher`** (owns Ui + app instances, runs loop + covert plumbing via cover_kit, `tick()`s ALL apps every loop so timers/reminders fire from any screen, dispatches input to `_active` or the home grid, Notes hand-off, alarm-banner overlay). Patterns: State/Strategy, dependency injection, Facade (Ui). Behavior ported faithfully; launcher file-static (state persists) w/ per-session resets. **Then split further: one class per file** — `home_apps.{h,cpp}` (782 lines) → `app_calculator/app_clock/app_reminders/app_weather/app_calendar/app_flashlight/app_settings.{h,cpp}` (14 files); `home_apps.h` is now an umbrella `#include`, `home_apps.cpp` emptied to a placeholder (defs moved out, no dup symbols). **WHOLE REFACTOR COMPILES CLEAN (2026-07-08).** Bonus: flash DROPPED ~44KB (2400593→2356029) + RAM −448B from the dedup (cover_kit collapsed the sprite/font/passphrase code mirrored in home_ui+notes_ui; Ui removed repeated draw helpers) — less code despite cleaner structure. File map: cover_kit(shell) · home_app.h(base) · home_widgets(Ui manager) · app_*.{h,cpp}(7 apps, one class/file) · home_ui.cpp(HomeLauncher, ~325 lines). home_apps.cpp deleted (umbrella home_apps.h only). **NEXT: HW test the whole launcher, then commit (his name, no Co-Authored trailer).**
- **User feedback captured** ([[feedback_rules]] 8b): never use "god" casually (god-object/file) — This reflects a user style preference. Also honoring: no AskUserQuestion, no redundant post-edit verification.
- **NEXT:** compile Stage 1 → Stage 2 (app classes) → Stage 3 (orchestrator). Then HW test the whole launcher.

## Session 2026-07-07b (home-launcher undercover UI — BUILT) — code-complete, UNCOMMITTED, pending HW test
- **HOME is now the undercover BASE + `notes` command removed (user decision, end of 07b):** `runUndercover()`
  and boot-cover `ucInit()` now call **`runHomeUi()`** (was `runNotesUi()`); undercover.cpp includes home_ui.h.
  The **standalone `notes`/`nt` command was REMOVED** from command_manager (Notes reachable only via the home
  launcher's Notes tile; `runNotesUi()`/notes_ui.cpp stay). Boot-cover boots straight into the home launcher.
  All "Notes disguise" wording → "home-screen disguise" across uc status/boot messages, man (uc/home),
  CLAUDE.md, README, docs/system.md. `man notes` still resolves (PAGES[] entry kept). Command count 65→**64/128**.
- **Charging indicator (home UI):** the status-bar battery now shows a **green body + dark lightning-bolt
  overlay while `isCharging()`** (classic charging cue the user noticed was missing); non-charging = proportional
  fill, red ≤15%.
- **SHARED status bar across both covers (2026-07-07b):** extracted the phone status bar into
  `core/system/undercover/cover_statusbar.{cpp,h}` — `drawCoverStatusBar(G, metaFont)` (caller passes its own
  VLW meta font) with an internal 10s battery cache. **BOTH home_ui AND notes_ui now call it**, so they're
  identical and can't drift. Fixes the notes bar which had a **HARDCODED "14:32"** and no charging bolt — it now
  shows real ClockManager time + real battery + charging bolt (dark theme, "CRIMSON MOBILE"). Notes `#define SB_H`
  changed 18→`COVER_SB_H`(22) so the taller shared bar fits (content shifts down 4px; SB_H-relative offsets
  APPBAR_Y/DOC_TOP/back-bar/tap-regions all scale). Removed the per-file battery caches + `battery_manager.h`
  includes from home/notes (now only in cover_statusbar.cpp). **Added a 20s live-refresh tick to the notes loop**
  (repaints current view) so time/battery update while idle — matches the home cover's clock tick. Verified both
  paintList/paintDetail draw the bar first + content fills from SB_H down (no overlap).
- **Battery "looks fake" clarification:** it IS the real `batteryManager` reading — but while on USB it reads as
  charging (green/full, static), which looks fake but is correct; unplug to see it track. No bug found.
- **New `home`/`hm` [EXP] command** — a BlackBerry/modern-phone home-launcher disguise, alternative to
  the Notes-only cover. Files `core/system/undercover/home_ui.{cpp,h}`, `bool runHomeUi(bool standalone=true)`.
  Long-standing spec in [[project_undercover_home_launcher]] (was LOW-PRIORITY; user asked to build it).
- **Look:** dark theme; fake status bar (real HH:MM via ClockManager · "CRIMSON MOBILE" · signal+battery)
  + clock/weather hero (live time+date, static sun/"24°") + **4×2 grid** Phone/Messages(3)/Email(12)/
  Browser/Music/**Notes**/Calendar/Settings. Icons = **vector primitives** (no icon-font — baked Noto VLW
  is text-only). Only the **Notes tile is real** (→ `runNotesUi(false)`); other tiles show a "No service"
  toast and open nothing.
- **Reuses the Notes cover plumbing** (mirrored per-TU, not shared): PSRAM `LGFX_Sprite` + `flushCanvas`,
  the 4 baked fonts, `setBlocked(true)`, dim→wake repaint, LockScreen `isLocked()` stand-down, touch
  double-tap screen-off wake, and the **secret-passphrase rolling buffer** → typing the phrase from home
  drops to CLI; `q` fallback only when no passphrase.
- **Load-bearing refactor:** `runNotesUi()` → **`bool runNotesUi(bool standalone=true)`** (backward-compat
  default arg; `notes`/`undercover` callers unchanged). `standalone=false` skips the CLI restore + keeps
  `setBlocked(true)` so Home retakes the screen flash-free; the bool = "exited via passphrase". Home frees
  its OWN sprite/fonts before handing off (avoids 2×150KB PSRAM peak), rebuilds+repaints on a normal Notes
  back, or propagates a passphrase exit straight to the CLI. Same contract on `runHomeUi`.
- **COMMAND CAP HIT:** this is command **64/64** — `Command commands[64]` in command_manager.h is now FULL
  (`registerCommand` silently drops a 65th). Bump to `[128]` (~2KB RAM, `commandCount` is uint8_t so fine)
  before any further new command.
- **Real WEATHER in the home hero (2026-07-07b) — Open-Meteo, KEYLESS:** new `WeatherManager`
  (`core/system/weather_manager.{cpp,h}`, singleton mirroring ClockManager) + `weather`/`wx` command.
  **Weather CANNOT be offline** — live server data; GPS only supplies the query LOCATION. **Switched
  OpenWeather → Open-Meteo** (user's call, and the right one): **no API key** = zero setup + nothing secret on
  the SD card (OPSEC win for a pentest device). Cost: Open-Meteo is HTTPS-only → `WiFiClientSecure` +
  `setInsecure()` (trivial, mbedTLS already linked). ArduinoJson 7 (`JsonDocument`, buddy.cpp idiom) parses
  `current.temperature_2m`/`current.weather_code` (**WMO codes**, not OWM). Location = **GPS fix (Plus,
  `#ifdef BOARD_TDECK_PLUS`) → configured lat/lon**; on the Plus with a fix it works with ZERO config. Reading
  cached in RAM (shown offline til reboot). Config `/config/weather.conf` (`lat`/`lon`/`units`, no apikey),
  **self-seeds** a commented template on first boot; loadConfig strips `#` inline comments + trims values.
  `wx` subcmds: `loc <lat> <lon>` · `units metric|imperial` · `now` · `status` (no `key` anymore). **GDMA-SAFE
  trigger:** fetch runs ONLY from `wx now` + Home-launcher ENTRY (benign cover, WiFi idle STA) — NOT the global
  input hook (netspy/isoscan run associated-with-promiscuous → an HTTP(S) GET there corrupts FatFS). Home hero
  draws a real vector weather icon (sun/cloud/rain/snow/thunder by WMO code) + temp + condition; muted "--"
  placeholder when no reading. `WeatherManager::instance().init()` wired in main.ino after clock init. 65/128 cmds.
  - **Automatic best-source location (2026-07-07b):** `locate()` = **GPS fix (Plus, most accurate) > manual
    `wx loc` > WiFi IP geolocation** (`locateByIp` → ip-api.com free/keyless/HTTP `?fields=status,lat,lon`,
    cached in `_haveIpLoc`). So `wx now` "just works" everywhere and **`wx loc` is only an optional override,
    never required** (GPS was always preferred; IP geoloc is the new automatic WiFi fallback). `configured()`
    now always true (a source always exists when online; fetch still gated on WL_CONNECTED). Privacy: IP geoloc
    reveals the public IP to ip-api.com — a GPS fix or manual `wx loc` avoids that call. printStatus shows the
    active source (GPS/manual/IP/auto).
- **Notes → Home exit (2026-07-07b):** when the notes UI is launched from the Home launcher
  (`runNotesUi(false)`), there's now a **visible back-to-Home chevron** in the notes LIST appbar (top-left,
  "‹ Notes"; title shifts right to make room) PLUS **`[q]`** as the keyboard shortcut — both return to Home
  regardless of passphrase (returning to the disguise launcher is not a covert CLI reveal; the passphrase check
  still runs first so a phrase containing 'q' completes). Gated by a `s_fromHome` flag (= `!standalone`), so the
  standalone `notes`/`uc` covers are unchanged (no chevron, `q` = old fallback behavior). Chevron hit-box =
  appbar band, x≤44; tap/`q` `break` with secretExit=false → Home retakes the screen. In DETAIL view `q` stays
  typeable — the detail back-chevron goes to the list first. Fixes the gap where a configured passphrase left NO
  discoverable way back to Home from Notes.
- **Real battery in both covers (2026-07-07b):** the fake status-bar battery in BOTH `notes_ui` and `home_ui`
  now shows the real level via the existing global `batteryManager.getPct()`/`isCharging()` (same source as the
  real status bar). Cached 10s (`getPct()` = READS=20 ADC samples; status bar repaints on every cursor
  blink/tick). Fill colour: charging=green, ≤15%=red, else theme ink. Was a hardcoded 72%.
- **Command cap raised 64→128** (`Command commands[128]` in command_manager.h + `idx[128]` in utils.cpp help +
  CLAUDE.md note). ~2KB RAM; `registerCommand` guards via `sizeof`, `commandCount` uint8_t. Now 64/128 used.
- **Known quirks (HW-test targets):** panic-key hook still fires inside *standalone* `home` (g_covert=false
  there) → lands at CLI not home; harmless, non-issue once wired as a covert cover. NOT yet offered as an
  `undercover` cover-style choice (notes vs home) — obvious next step. Icons upgradeable later.
- **NEXT:** user compiles/flashes + eyeballs on glass. Then optionally: wire into `undercover`, bump the
  command cap, commit (his name, NO Co-Authored trailer — [[feedback_rules]]).

## Session 2026-07-07 (isoscan back-stack nav + CLI bad-arg DRY rejection + bs-android reboot fix) — ✅ HW-TESTED, COMMITTED + PUSHED
- **All work HW-tested, committed, and pushed manually by the user.** 4 file-aligned commits on
  `feature/pentest-enhancements` (his name, NO Co-Authored trailer — [[feedback_rules]]):
  `fcdedaa` blespam · `0f44fca` cli bad-arg · `49b3a6e` isoscan · `d8fe577` lockscreen.
- **isoscan usability (`wifi/attacks/isoscan/isoscan.cpp`):**
  - **Navigable back-stack** — was: `[q]` in any tool dropped out of the whole `is` command. Now a two-level
    loop: *running attack* --[q]--> *attack menu* --[q]--> *victim picker* --[q]--> CLI. Chain attacks on one
    victim without relaunching. Every attack fn changed `void`→`bool` (`isoInjectArp/GwPoison/Bounce/PortDown/
    Mitm/SmartAuto/RaDns/RunAttack`; `isoCcmpTest` stays void): **`true`=clean exit** (caller re-shows menu),
    **`false`=hard error** (msg+`printCommandScreen` already drawn → exit `is`). Normal exits went from
    `clearScreen();printCommandScreen();` to `clearScreen();return true;`. A CLI-supplied victim (`is ns3 …`)
    has no picker so `[q]` at its menu exits (`victimFromCli` flag).
  - **Result-only screens** (`bounce`, `auto`, `bcast` note) now wait for a keypress before returning so they're
    readable.
  - **Every SD-writing attack shows the exact file it's writing** — `mitm`/`dns` previously didn't. mitm pcap
    **renamed `mitm_%03u.pcap`** (distinct prefix from portdown's `NNN.pcap` → the two capture types are
    identifiable, no shared namespace). Each picks next-free `NNN` via `SD.exists` (verified: NO overwrite on
    any attack). File-path display pixels checked non-colliding (mitm y178 below stats fillRect; dns log y108).
- **CLI bad-arg rejection — DRY via the command table (`core/system/utils.{h,cpp}` + ~10 handlers):**
  - **Root idea (user's, for design-pattern's sake):** a handler that gets an unrecognized arg should print
    **what's in `hlp`**, not a hand-copied usage string. New `Utils::printUsage(const char* cmd)` looks the
    cmd up in `commandManager.commands[]` and prints its registered `description` (red "bad arg —" + grey desc)
    — **single source of truth = the command table**. So fixing a usage string = editing the `description` only.
  - **`Utils::checkChannelArg(a, cmd)`** — the numeric-channel commands (`wm/es/est/ev`) accept `""`(default) or
    an int 0..13, else `printUsage`. This is the `wm ff` fix (user: "wm ff did not have any problem with ff wtf"
    — `atoi("ff")→0` was silently running channel 0). `cw` deliberately stays free-form (`cw <ssid>`).
  - **Per-handler, NOT a central validator** — proved a kArgHints-based central check would break working
    commands: `bs` accepts synonyms (`ios/windows/microsoft/galaxy`) not in kArgHints. Each handler rejects only
    args that are present AND unrecognized; bare/interactive invocation always preserved.
  - Migrated handlers to `printUsage`: `wp`, `show`, `tm`(silent only), `jg`(usb/ble), `mc`, `mw`(add), `km`,
    `ns`(gtk/dump), `csi`(auto), `gps`, `bs`, `fp`. Some needed `#include "utils.h"` added. A few `description`
    strings tightened to read as real usage (mc/ns/jg/fp). kArgHints fixed: test→"spk mic lora touch",
    is→"auto inject bounce portdown mitm portup dns bcast cctest".
- **`bs android` reboot fix (`bluetooth/attacks/ble_spam/ble_spam.cpp`):** user hit a reboot on `bs android`.
  Root cause was **pre-existing** (proved via diff it wasn't my change): `spamAndroid()` did per-cycle
  `NimBLEDevice::deinit(true); vTaskDelay(20); init("")` to rotate the MAC — tearing down/reinit the whole
  NimBLE stack mid-advertise crashed. Fix = **NimBLE-native address rotation** (same pattern as `mc` /
  [[nimble_v2_rules]]): `macutil::randomBleMac(mac); setOwnAddrType(BLE_OWN_ADDR_RANDOM); setOwnAddr(mac);` —
  no stack teardown. Stack init'd once at top; only remaining `deinit(true)` is the exit path
  (`bsRestoreStack`). Added `#include "utils.h"` + `#include "mac_util.h"`.
- **lockscreen (`ui/lockscreen/lockscreen_manager.cpp`) — NOT authored this session** but committed with the
  batch: `intercept()` gained `if (g_covert) return k;` so the padlock doesn't blow the Notes undercover
  disguise. Flagged to user as pre-existing before committing.
- **Docs updated:** CLAUDE.md + docs/isoscan.md for the back-stack nav, `mitm_NNN.pcap` naming, save-path
  display, no-overwrite behavior.

## Session 2026-07-06b (isoscan interactive-UI flicker/clarity rework + dns dual-stack listener) — code done, UNCOMMITTED
- **User complaint: isoscan interactive mode "flickers so much" + "tools not understandable".** Root cause of
  the strobe: in inject/gwpoison/radns a `lastDraw = 0;` sat INSIDE the ~20 ms TX tick, defeating the redraw
  throttle → full-screen `clearScreen()` (via `isoHeader`) at ~50 fps. Fixed across the board:
  - **Menu** = chrome-once + body-on-selection-change (no timer redraw); each row now shows the attack's
    one-line **description** AND its CLI **`[keyword]`** on the right (fixes "auto says portdown but menu says
    'Capture victim->SD'" — they now match, and map to `is ns# <keyword>`). `isoMenuChrome`/`isoMenuBody`.
  - **Picker** = same chrome/body split (`isoPickChrome`/`isoPickBody`) — no flash on scroll.
  - **inject + gwpoison** = VALUE-IN-PLACE: header+labels drawn once, only the changed numbers repaint
    (`fillRect` the value cell + reprint), like portdown already did. Genuinely flicker-free.
  - **Plain-language "what it's doing" line** added to every running screen (inject: "send encrypted ARP;
    wait for a reply"; gwpoison: "tell victim WE are its gateway"; portdown: "save victim's frames to SD…";
    mitm: "poison + capture; is it holding? see verdict"; dns: "make victim use us as DNS; log its lookups").
  - **dns live query list**: shows a rolling **last-5 DNS lookups** on screen (`victim is reaching:`), repainted
    only when a new query arrives (event-driven). Full record still → `/apps/isoscan/dns_NNN.csv`.
- **dns DUAL-STACK listener fix (the "RA sent up, DNS rcvd 0" bug):** `WiFiUDP.begin(53)` is IPv4-ONLY but the
  RA points the victim at us over IPv6 → queries never arrived. Replaced with a raw lwip pcb
  `udp_new_ip_type(IPADDR_TYPE_ANY)` + `udp_bind(IP_ANY_TYPE,53)` + `udp_recv(isoDnsRecv)` (v4+v6); cb parses +
  rings (tcpip thread), main loop drains → list + SD log (`ipaddr_ntoa_r` src). `udp_remove` on exit.
  Includes `<lwip/udp.h>`; dropped `<WiFiUdp.h>`.
- **dns is IPv6-ONLY (RA/RDNSS has no IPv4 form) — CONFIRMED DEAD on the mobile hotspot (HW):** victim's
  `ip -6 addr` = only `fe80::` link-local, `ip -6 route` = no v6 default, `resolvectl` = IPv4 DNS (`.55`). So
  nothing to poison. `dns` needs a home router with real client IPv6 (global `2xxx:` + v6 route). IPv4 DNS
  redirect would need DHCP-spoof/DNS-MITM (not feasible). Docs (CLAUDE/README/man/docs-isoscan) all updated.
- **NEXT:** user is HW-testing the UI + verifying menu/inject are steady. Commit the UI pass once confirmed
  (his name, NO Co-Authored trailer — [[feedback_rules]]).

## Session 2026-07-06 (isoscan/is — L1 smart-auto + L2 MITM + HONESTY pass + AirSnitch research + docs) — ✅ HW-TESTED, DONE
- **Built L1+L2 then made the whole tool HONEST after HW testing showed the MITM was over-claiming.** All in
  `wifi/attacks/isoscan/isoscan.cpp`. UNPUSHED (user compiles/flashes + pushes manually).
- **L2 `mitm` (NEW combined attack `isoMitm`)** — gateway poison (TX) + promiscuous capture (RX→pcap) at once.
  The cap cb (`isoCapCb`) now classifies each frame the AP relays back to us (`from-DS, A1=our MAC, A3=victim`)
  by **ethertype**: ARP(`0x0806`)=`s_capArpAck` (victim merely reacted), IP(`0x0800/0x86dd`)=`s_capRedir` (real
  data), else `s_capCand` (payload unreadable). Added `s_capOur[6]`/`s_capRedirOn`. Payload IS decrypted for
  frames addressed to us (SNAP+ethertype at `hdrlen+8+6`, hdrlen 24 or 26 QoS).
- **THE BUG we found + fixed (HW):** first `mitm` run showed "all green / MITM LIVE" but the victim's ping+web
  were UNDISTURBED = false positive. Root cause via pcap (003.pcap): the redirect counter was counting the
  victim's **ARP reply to our poison** (from-DS A1=us A3=victim) as "data redirect" — a reaction, not
  interception. Fix = the ethertype split above + **rate-gating**: "MITM LIVE" now fires ONLY on a **sustained
  ≥8 frames/s** data rate (2s window); a few stray frames read `leak N — NOT held` (yellow). auto's stage-5 uses
  `redir>=16` over its 6s window = viable, else leak. Confirmed honest on HW: lab router (no isolation) →
  `1 ACK, 0 data`, verdict correctly "poison seen, not holding → portdown".
- **L1 smart `auto` (`isoSmartAuto`, replaced thin bounce-only stub)** — 6-stage probe→verdict (CCMP · GTK ·
  normal-ARP · GTK-inject-reach ~6s · poison-hold L2 test ~6s · ICMP) → prints VERDICT + recommended attack.
  **HW-verified 2026-07-06.**
- **`bounce` reworked ARP-first (HW-verified)** — `etharp_request`+`find_addr` (works vs a Windows firewall that
  drops ICMP), ICMP kept as secondary datapoint. Old ping-only wrongly said Windows victims unreachable. auto
  stage-6 relabeled "ICMP ping … no reply (fw ok)" so it doesn't contradict `bounce`.
- **Refactor (rule 5b):** extracted `isoTxGtkArp(...)` — inject/portup/mitm/auto all share it. Menu reordered
  (auto first) + honest `[exp]` labels: auto,inject,bounce,portdown,mitm[exp],portup[exp],dns[exp],bcast.
- **RESEARCH — AirSnitch/Bruce/Marauder (web):** settled that **real traffic MITM is NOT achievable on the
  single-radio T-Deck.** AirSnitch's actual interception = **port stealing** (spoof victim MAC on a *2nd BSSID*
  → AP switch remaps its port to us) = needs **2 radios** (they use 2 USB adapters). Our `inject` = AirSnitch's
  GTK-abuse primitive (proven, novel on ESP32 — neither Bruce nor Marauder has it; Bruce's "MITM" is classic
  random-MAC ARP-poison DoS, Marauder has no L3). ARP-poison also can't hold vs Windows / without forwarding.
- **VERDICT: isoscan is DONE as an honest *isolation-AUDIT + recon + inject* tool** — reliable working parts =
  `ns` discovery, `inject` (bypass proof), `bounce` (reachability), `portdown` (capture), `auto` (probe+
  recommend). `mitm/portup/dns` `[exp]` run + report truthfully that a poison doesn't hold. NOT a traffic
  interceptor. User accepted this ("so its not actual mitm" — confirmed).
- **DOCS updated (2026-07-06):** man_pages (`is` entry — attacks + honest MITM-ceiling note), README (Network
  table row + feature checklist), CLAUDE.md (IsoScan module — mitm/auto/bounce + HONESTY rewrite), docs/network.md
  (table row + section), **new docs/isoscan.md** (full page, mirrors netspy.md). NOTICES already credits AirSnitch.
- **▶ OPTIONAL NEXT (only if user wants MORE):** a **port-stealing spike** — can the single radio re-associate to
  a 2nd BSSID under a spoofed victim MAC? That's the ONLY path to real interception, and it'd be half-duplex/
  clunky on one radio — spike feasibility before promising. Otherwise isoscan is complete. L3 sweep / L4
  keyid-auto still deferred. `dns` RA ICMPv6 checksum/ND still UNVERIFIED (needs an isolated L2 AP test bed).

## Session 2026-07-05 (isoscan/is Stage 2 — targeting + CCMP inject crypto) — IN PROGRESS
- **isoscan/is command created** (`wifi/attacks/isoscan/`, Network, [EXP], registered #63/64 — cap now
  FULL). Active counterpart to netspy; TRANSMITS, so it's a separate opt-in command. Free-function
  `runIsoscan(char*)`. Include path + `SD_DIR_ISOSCAN` + ensureTree added.
- **Targeting layer built (compiles, not yet HW-tested):** victim picked from the netspy device list —
  CLI `is ns3 <attack>` (reuses the `ns#` idea) OR in-app picker (trackball select) + attack menu +
  **confirm-before-fire** (echoes MAC/IP/name, requires `y`). netspy.h gained exports the picker needs:
  `netspyDeviceMac(idx,out)` / `netspyDeviceName(idx)` (only IP was exported before). Attacks:
  inject/bounce/bcast/portdown/portup/auto — all STUBS ("attack core not built yet") except crypto below.
- **✅ CCMP inject crypto — HW-VERIFIED 2026-07-05 (`is cctest` → PASS + live GTK shown).** `iso_ccmp.h/.cpp`:
  software AES-CCMP encrypt (IEEE 802.11-2016 §12.5.3) via **mbedTLS `mbedtls_ccm_*`** (HW AES). Builds
  CCMP header (PN0 PN1 rsvd keyid|0x20 PN2-5), 13B nonce (`0x00‖A2‖PN-big-endian`), 22B AAD (`FC&0x8f ‖
  (FC1&0xc7)|0x40 ‖ A1‖A2‖A3 ‖ SeqCtrl&0x0f‖0`), MIC=8, iv_len=13 (L=2). `isoCcmpSelfTest()` = encrypt→
  auth_decrypt round-trip (no TX) → user confirmed PASS on HW. GTK exported from netspy via
  `netspyGetGtk(out,&len)` (reads gWpaSm+0x174, same as `ns gtk`); confirmed readable + 16B on HW.
- **✅ Inject TX primitive — HW-VERIFIED 2026-07-05 (`is` → inject → TX ok climbing, rc=0, err=0).**
  `isoInjectArp()`: broadcast FromDS Dot11 hdr (A1=ff.., **A2=BSSID spoofed**, A3=our MAC) + LLC/SNAP +
  ARP who-has(victimIP, tell ourIP) → `isoCcmpEncrypt` (PN starts 0x800000000000, incrementing; keyid 1,
  `[k]` toggles 1/2 live) → `esp_wifi_80211_tx(WIFI_IF_STA, frame, len, false)` ~5fps. **KEY UNKNOWN
  RESOLVED:** the ESP32 ACCEPTS a raw 80211_tx frame whose A2 ≠ our STA MAC (the spoofed BSSID) while
  associated — rc=ESP_OK. So GTK-inject TX is feasible on ESP32 (the last feasibility worry beyond the
  closed-blob CCMP, which we do in SW). **CAVEAT: TX ok = API queued the frame; NOT yet confirmed the AP
  forwards it / the victim accepts+reacts** (that needs external Wireshark OR an on-device RX response
  detector — NEXT). keyid 1 vs 2 + PN high-water still unswept for real victim acceptance.
- **✅✅ FULL GTK-INJECT PIPELINE HW-VERIFIED END-TO-END 2026-07-05 — the make-or-break result.** Injected
  a GTK-encrypted broadcast ARP from the T-Deck; confirmed on the VICTIM (laptop, tcpdump on its wlan):
  `<tdeck-LAmac f6:20:3c..> > ff:ff:ff:ff:ff:ff Request who-has <victimIP> tell <tdeckIP>` ARRIVED
  **decrypted**, and the laptop **replied** `<victimMAC> is-at ..` to the T-Deck. Victim-side decrypt success
  proves CCMP framing + keyid + PN + spoofed-A2 TX + AP-forwarding are ALL correct. AirSnitch active GTK
  inject is REAL on ESP32-S3. (Test net had NO client isolation — correct network to validate the primitive
  itself; isolation-bypass value proven separately later.)
- **On-device RESP detector — promiscuous was WRONG and REMOVED.** v1 used a promiscuous RX cb to catch the
  victim's ARP reply → stayed "none" even with tcpdump proof, because the reply is UNICAST to the T-Deck and
  the promiscuous cb only gets GROUP frames decrypted (netspy's mechanism). WORSE: enabling promiscuous
  likely DIVERTS the unicast reply away from the lwip data path too. **FIX (built, not yet HW-confirmed):
  dropped promiscuous entirely; detect via our own lwip ARP cache** — `etharp_find_addr(netif_default,
  &victimIP,...)` under LOCK_TCPIP_CORE (mirrors network_scanner), polled 500ms in the inject loop. The
  unicast reply lands in the IP stack → cache hit = "VICTIM REPLIED (ARP cache)". lwip inc: etharp.h/netif.h/
  tcpip.h. **Attribution caveat:** on a NON-isolated net the entry may pre-exist (green instantly, not
  attributable) — on an ISOLATED net the stack normally can't resolve the victim so a hit IS proof. If the
  ARP-cache read still shows nothing post-fix, lwip isn't caching the unsolicited reply → fall back to
  tcpdump-as-proof (the attack is already proven) and rely on RA-DNS-poison's own UDP-53 socket signal.
- **Delivery RATE low (follow-up, not a bug):** victim tcpdump saw only ~1 injected req / ~23s though TX ok
  climbs fast; framing is fine (they decrypt), so medium loss / AP buffering of client-originated group
  frames. Fine for RA DNS poison (RAs periodic). Investigate if higher rate needed.
- **NETWORK REALITY (2026-07-05, changes the plan):** the test net is a **mobile hotspot = CLIENT ISOLATION
  ON** (user thought it wasn't). `nd` on the T-Deck sees ONLY the gateway (10.184.171.55), NOT the victim
  laptop (.232) — isolation confirmed. So this IS the real isoscan use case, and the inject bypassing it
  (victim tcpdump got our frame) = the feature working. **Why the ARP RESP can NEVER fire here (not a bug):
  the victim's reply is client→client UNICAST, which the hotspot blocks on the RETURN path** — it leaves the
  laptop (tcpdump sees it) but never reaches the T-Deck. Reply-based on-device detection is the wrong tool
  under isolation; VICTIM-SIDE tcpdump is the proof. (Also: T-Deck's IP was transiently .55 = the gateway IP
  during those runs → injected `tell .55` accidentally poisoned the laptop's GATEWAY arp entry to the T-Deck
  MAC; now T-Deck is .159. The accidental poison = a free demo of gateway-impersonation.)
- **THE LINCHPIN (unproven, gates ALL interception attacks incl. RA DNS poison + MITM):** can the victim
  send data TO the T-Deck under this isolation? Injection TO victim = proven; victim→T-Deck = unknown (the
  ARP reply is blocked coming back). If NO, RA DNS poison's queries won't reach us either → IPv6 effort
  wasted. **IPv6 + UDP server are BOTH absent from the firmware** (grep: IPv6 only in netspy comments, zero
  UDP-server code) → RA DNS poison is a big untested lift (v6 enable + link-local + ICMPv6 RA + UDP-53
  server), NOT a payload swap.
- **Gateway ARP-poison BUILT + HW-tested** (`is` → "Gateway poison (uplink)" = ISO_PORTUP → `isoGwPoison`,
  reuses ARP builder with srcIp=WiFi.gatewayIP()). **RESULT — poison LANDS but does NOT intercept on a
  mobile hotspot:** `ip neigh` on the laptop showed `gateway.55 -> T-Deck MAC REACHABLE` (poison installed),
  yet `ping 8.8.8.8` = **295/295, 0% loss over ~5 min** with the poison held. **CONCLUSION: a phone hotspot
  routes uplink by L3 (all client TX goes to the phone anyway; it NATs by dest IP, ignoring the poisoned L2
  gateway MAC) → L2 ARP-MITM is architecturally defeated there.** Same wall blocks RA DNS poison (victim→
  T-Deck query = client↔client, isolation-blocked).
- **Delivery RATE cap (fundamental):** injected broadcasts reach the victim only ~1 per 15-28s regardless of
  `iw set power_save off` on the victim → **DTIM-aligned broadcast RX** (GTK inject MUST be broadcast; managed
  clients gate broadcast to DTIM windows; mobile-hotspot DTIM ~255×beacon ≈ 25s). Not a code bug, hard to fix
  our side. Enough for RA-poison-style persistent config, too slow to out-race a real gateway for sustained
  ARP-MITM.
- **MAC note:** `mc off` sets the base MAC (e.g. xx:xx:xx:xx:xx:xx) but the LIVE association keeps the old
  random MAC (f6:20:3c..) until a `cw` RECONNECT — `esp_wifi_get_mac(WIFI_IF_STA)` (what inject uses) returns
  the live one. Pin MAC = `mc off` THEN `cw`. Within a session it's stable, so not a blocker.
- **VERDICT: GTK inject primitive = DONE + HW-proven (one-way inject bypassing isolation). Traffic
  INTERCEPTION (MITM/DNS) is network-dependent and a mobile hotspot is architecturally the WRONG test bed
  (L3-routes past L2 attacks). NEXT to validate interception = a REAL Wi-Fi router with "AP/client isolation"
  enabled (does L2 bridging → ARP-poison redirection behaves predictably).** See [[netspy + isoscan plan]].
- **FULL attack suite BUILT (2026-07-05) — all compile-ready, static-reviewed, NOT HW-tested. Commits:
  ff7f4fd (inject/portup/cctest), a0aedf2 (bounce/portdown-v1/auto), + this one (RA DNS + portdown rework).
  7 menu attacks; `is` = 63/64 cmds.**
  - `bounce` — reach the victim at L3 via the gateway: static lwip ARP entry (victim IP → gateway MAC,
    `etharp_add_static_entry`) + `Ping.ping`; reply = isolation bypassed at IP layer (if the gateway
    hairpins). Entry removed on exit; gateway MAC via `isoResolveGw` (etharp).
  - `portdown` — **REWORKED to non-disruptive victim CAPTURE → SD** (was a MAC-spoof that dropped WiFi;
    user asked for no-drop + SD). Promiscuous grabs every data frame mentioning the victim MAC (A1/A2/A3)
    → RAM ring (`isoCapCb`, 16×288B) → `/apps/isoscan/NNN.pcap` (`pcap::writeRecord` lt105), flushed with
    `ScopedPromiscPause` (GDMA). No MAC change, stays associated.
  - `dns` — **RA DNS poison flagship, NOW BUILT** (`isoRaDns`): GTK-encrypt a MULTICAST (A1=33:33::01,
    ff02::1) ICMPv6 RA w/ RDNSS = our link-local → victim uses us as DNS. `WiFi.enableIpV6()` +
    `esp_netif_get_ip6_linklocal(WIFI_STA_DEF)`; `isoBuildRA` (SNAP+IPv6+ICMPv6-RA), `isoIcmp6Cksum`
    (pseudo-hdr internet cksum). WiFiUDP:53 + `isoDnsName` → log to `/apps/isoscan/dns_NNN.csv`. RA every
    2s, `[k]` keyid. **Still faces the isolation return-path wall (victim→us query must arrive) → needs an
    L2 AP; checksum/ND correctness UNVERIFIED (the #1 lab-debug target).**
  - `auto` = one-shot bounce probe. `bcast` = honest note (to-DS needs our PTK). Inject/poison rate tuned
    200ms→20ms (~50fps) to hold a poison on an L2 AP. Dispatch = switch.
  - **BUILD FIX (compiles clean now, RAM 60.9% / Flash 33.8% on T-Deck-Plus):** `bounce`'s
    `etharp_add_static_entry`/`_remove_static_entry` were compile errors — gated behind
    `ETHARP_SUPPORT_STATIC_ENTRIES` (OFF in ESP-IDF lwip, symbols don't exist). Reworked `bounce` into a
    plain `Ping.ping` reachability probe (lwip's broadcast ARP is still relayed by most soft-isolation APs).
    Commits: ff7f4fd, a0aedf2, 260afc7, 8d1e932 — all on feature/pentest-enhancements, UNPUSHED (user
    pushes manually + compiles manually — see [[feedback_user_compiles_manually]]).
- **▶▶ RESUME HERE — isoscan status + next work (2026-07-05, user is HW-testing the suite now, then continue):**
  - **STATE:** full `is` suite BUILT + COMPILES, only `inject`+`cctest` are HW-proven; bounce/portup/portdown/
    dns/auto are compile-ready but UNVERIFIED. `portdown`=non-disruptive capture→`/apps/isoscan/NNN.pcap`;
    `dns`=RA DNS poison→`/apps/isoscan/dns_NNN.csv` (ICMPv6 checksum/ND untested = #1 lab-debug target).
    Interception needs a REAL L2 router w/ client isolation (a phone hotspot L3-routes past it — proven).
  - **NEXT (user asked "make it smarter/automatic" — currently every attack is INDEPENDENT, manual pick-run-
    stop; `auto` is thin = just bounce). Agreed plan, 4 levels:**
    - **L1 — smart `auto`**: probe→decide→recommend engine. Run cctest + inject-reachability (timed ~10s,
      check ARP cache) + bounce + a **poison-hold test that auto-detects L2-vs-L3** (does a brief gateway
      poison actually break the victim's ping? = is MITM viable here) → print a summary + recommend the best
      attack. This auto-answers the L2/L3 + reachability questions we found by hand.
    - **L2 — combined MITM (the big one)**: run `portup` (redirect) + `portdown` (capture) SIMULTANEOUSLY in
      one command → poison the gateway AND log the redirected traffic to a pcap. Turns the independent pieces
      into one real automatic MITM.
    - **L3 — whole-network sweep**: iterate ALL netspy `s_dev[]` devices, probe each, print a matrix
      (reach? L2? best-attack) instead of one victim at a time.
    - **L4 — auto-tuning**: auto-sweep keyid 1/2 (instead of manual `[k]`), auto params.
    - Recommended build order when resuming: **L1 + L2 first** (biggest value). Honest limit: automation
      can't beat a network that blocks the return path — but smart-auto DETECTS + reports it.

## Session 2026-07-04 (undercover PANIC BUTTON — instant-hide key) — ✅ HW-VERIFIED
- **The "panic-chord" from the plan, shipped via a simpler re-entrant model — no `UndercoverManager`
  refactor.** A single keyboard byte (default `@`=0x40, Sym+P) drops the device into the Notes cover
  from ANYWHERE, even mid-command. Hook lives in `getKeyboardInput()` right before the lock `intercept()`:
  `if (key==(char)ucPanicKey() && !g_covert && !g_ucCapturingPanic && ucHasPassphrase() && !isLocked())
  { runUndercover(nullptr); return 0; }`. Re-entrant: `runUndercover()` sets `g_covert=true` before the
  blocking `runNotesUi()`, so the cover's own nested `getKeyboardInput()` calls skip the hook (`!g_covert`)
  and `@` just types normally inside the cover; on passphrase-exit it returns and the trigger keypress is
  swallowed (`return 0`) so the underlying command resumes. **Only armed once a passphrase exists** (always
  a way back). Every blocking loop reads through `getKeyboardInput()`, which is why it works mid-command.
- **Config:** `panic_key=<byte>` in `/config/undercover.conf` (default 64='@', 0=off). `ucPanicKey()`/
  `ucSetPanicKey()` in undercover_config. `uc panic set` captures a key live — `g_ucCapturingPanic` (extern
  in undercover.h) suppresses the hook during capture so the CURRENT panic key can be re-bound without
  firing. Reserved keys blocked: `'` (autocomplete), `q` (cover exit), space, Enter, Backspace. `uc panic
  off` disables; `uc status` shows the armed/inactive/off state.
- **Key choice `@` (Sym+P):** user first proposed `#` (Sym+Q) but `#` is used for index targeting
  (`ps #2`/`pg nd#`) → chose `@`, no clash. TRADEOFF (documented): while armed, that key can't be typed in
  commands (it hides instead); inside the cover it types fine.
- **Bugfix — mid-command redraw (found on HW with `ws`):** panic-hiding mid-`ws` then exiting left the
  command half-drawn (only its live counters repainted, static header/layout gone). ROOT CAUSE: cover-exit
  never raised a repaint signal, so commands that repaint on unlock never learned to. FIX: added
  `LockScreenManager::signalRedraw()` (sets the existing `_justUnlocked` flag) and call it in `runNotesUi()`'s
  exit cleanup. **Cover-exit is now byte-identical to a lock→unlock** → every command that already survives
  idle-lock survives panic. Audited ALL ~40 interactive commands: all do a full redraw on the signal EXCEPT
  (a) `ux` BadUSB — intentionally doesn't repaint (`scriptDelay` consumes the flag, no redraw; script is
  what matters, not the screen), (b) eviltwin/karma portal-template picker (`captive_portal.cpp`) — repaints
  on next keypress. Both pre-existing, harmless.
- **Clarified (corrected a wrong claim mid-session):** panic-hiding during `ux` **pauses** the BadUSB script
  (it blocks at `scriptDelay`'s `getKeyboardInput()` until the cover exits) — it does NOT run under the cover.
  The "script keeps running" comment in bad_usb is about the idle-LOCK path (non-blocking), not panic (blocking).
  True covert-execution-behind-the-cover would need a non-blocking cover / `ux`-in-a-task — NOT built.
- **Build:** compiles clean both envs (T-Deck-Plus RAM 59.4%/Flash 33.6%, T-Deck RAM 59.1%/Flash 32.9%).
  Fixes along the way: man `uc` entry overflowed the fixed `const char*[32]` array (trimmed to 32);
  `undercover_config.h` needed `#include <stdint.h>` for uint8_t; installed missing `intelhex` py module into
  the PlatformIO penv (esptool dep, pre-existing gap); a base-T-Deck link error was a stale build tree
  (`pio run -e T-Deck -t clean` fixed it — main.ino.cpp.o hadn't regenerated). Docs: man page, CLAUDE.md.
- **Still NOT built:** decoy/duress passphrase (user said not useful → dropped); ops-policy "freeze
  transmitters under cover" (user dropped). Optional polish: make `ux`/portal-picker repaint on cover-exit.

## Session 2026-07-02b (undercover Phase 2 — real SD notes + cursor editor + bugfixes) — all HW-verified ✅
- **SD-backed real notes** (plan's last Phase-2 gap, now closed). `/notes/*.txt` at SD ROOT (`SD_DIR_NOTES`,
  outside `/apps/` — disguise: a PC-browsed card shows an ordinary notes folder). One file per note,
  `NNN.txt` sequential (never reused); line 1 = title, remaining lines = body, plain text (checklist
  rendering was demo-only, dropped). `NoteRec{title,body(vector<String>),path,dirty}`, loaded into
  `s_notes` on cover entry, freed on exit. No SD → 4-note in-RAM fallback (`kSeeds`), edits session-only
  (`saveNote()` bails via `canAccessSD()` check). First SD run with an empty `/notes/` auto-seeds the same
  4 starter notes as real files (plan: "ship believable decoy notes"). List view dropped the old
  pinned/tint demo decoration — flat list, card meta now shows real line count.
- **Real cursor-addressable editor** (not append-only). Cursor = `(curLine, curCol)`, line 0=title,
  1..N=body[i-1] — unifies title/body editing so moving the cursor UP out of the body naturally lands in
  the title (no separate "titling mode"). `layoutNote()` word-wraps into `LayoutRow{vline,colStart,text,y}`
  with exact column offsets into the UNWRAPPED source string, reused by measurement/cursor-placement/
  touch-hit-testing/rendering (single wrap pass per repaint). Touch a line → `moveCursorToTap()` resolves
  tap (x,y) to the nearest row then a column via width-scan. Trackball UP/DOWN move by stored line
  (clamping column); LEFT/RIGHT move by character, wrapping across line boundaries; CLICK is now the ONLY
  back-to-list trigger (LEFT/RIGHT repurposed from "back" to cursor movement). Backspace/Enter/printable
  insert/delete/split at the cursor position via `vline()` pointer helper — careful ordering around
  `n.body.insert/erase` (copy needed values BEFORE the vector mutation, never deref a body pointer after).
- **Save button wired for real** (was decorative "download" icon) — tap saves immediately, shows green
  "Saved" / amber "No SD" toast (`s_saveNoticeMs/Ok`) reflecting the ACTUAL `saveNote()` return value (a
  first draft showed "Saved" unconditionally — caught before shipping). Auto-save also fires on back-nav
  (`leaveDetail()`: saves if dirty, else drops a never-typed FAB placeholder so it doesn't ghost the list)
  and on the `q` fallback exit (only reachable with no passphrase set, so no opsec concern).
- **OPSEC: passphrase-match exit skips saving the open note.** The phrase's characters were already typed
  into the note's title/body across earlier loop iterations (typed live, since this is now a real visible
  editor) before the match could be known — saving on that exit path would write the passphrase itself
  into a plaintext SD file. Skipping the save keeps the SHA-256 hash the only place it ever persists.
- **Bugfix — real T-REX status bar + "Locked: HH:MM:SS" leaking over the cover.** Root cause:
  `runNotesUi()`'s loop calls `getKeyboardInput()` every iteration (for the passphrase scan), which
  internally pumps `LockScreenManager::intercept()`; if the device is ALSO actually locked (its own
  independent idle timer, unrelated to undercover), `intercept()`'s `refreshDuration()` fires every 1s and
  draws the real status bar + duration counter DIRECTLY to `tft` — bypassing the Notes UI's sprite
  compositing entirely (it explicitly force-unblocks around itself). Fix: while `LockScreenManager::
  isLocked()`, Notes UI now stands down completely (still pumps `getKeyboardInput()` so PIN entry works,
  `continue`s past all drawing) — same pattern wguard/buddy already use via `isBlocked()`, just via
  `isLocked()` since Notes UI intentionally bypasses `isBlocked()` for its own content.
- **Bugfix — touch-wake dead code.** First draft put the dim/screen-off touch-wake logic in `main.ino`'s
  loop — but that loop never runs while `runUndercover()`→`runNotesUi()` blocks, so it never fired. Moved
  into `runNotesUi()`'s own loop (where touch is actually polled); `main.ino` reverted to keyboard/
  trackball-only wake on the terminal (touch never wakes there — `#include "covert.h"` removed, no longer
  needed there). Undercover touch-wake: half-dim = single touch; screen-off = double-tap (500ms window).
- **Bugfix — trackball way too sensitive + accidental click-through.** Raw trackball events fire far
  faster than deliberate rolls; added a 300ms throttle. Also dropped RIGHT-to-open in the list (user found
  it fired unintentionally) — list nav is UP/DOWN + CLICK only now; RIGHT is reserved for cursor movement
  inside a note.

### What's still NOT built (next increments)
- **Panic-chord entry** (fire mid-command) — needs the non-blocking `UndercoverManager` intercept model +
  stateful `runNotesUi` refactor (begin/handleEvent/end). Panic chord is the real entry path; `uc` cmd is stopgap.
- **Phase 3**: boot-cover (`boot_cover=1`), decoy/duress passphrase (`decoy_hash`/`decoy_salt` in config),
  ops-policy (freeze transmitters under cover).

## Session 2026-07-01 (undercover Phase 1a — g_covert flag + sound-leak audit) — NOT compiled/tested by me
- After Phase 0 (touch) + Phase 2 UI (Notes cover) shipped & pushed (b273edd), started Phase 1 = "glance cover"
  (silent + invisible). Did **Phase 1a** = the flag + the audio audit + a deliberate entry.
- **`covert.h`** — `extern volatile bool g_covert;` (tiny header so leak points include just the flag).
  **`undercover.{h,cpp}`** — defines `g_covert=false` + `runUndercover()` (`uc` cmd): sets g_covert=true, runs
  the blocking Notes cover (`runNotesUi`), clears on exit. New cmd **`undercover`/`uc`** [EXP] System (count 63).
- **Sound-leak audit — 2 gates, that's all it took:** (1) `NotificationManager::notify()` early-returns when
  `g_covert` — ONE gate covers wguard (notifyThrottled) + macwatch + espchat-bg + all notif sounds, and kills
  the screen-wake tell too (checked: all route through notify()). (2) `hidden_ssid` `playBeep()` (direct
  `i2s_write`, bypasses NotificationManager) gates on g_covert. No other bg-capable emitters (buddy/trackme are
  foreground-only; they won't run under the blocking cover).
- **Visual tells need no g_covert gate for the blocking cover:** the Notes UI already calls
  `displayManager.setBlocked(true)`, and wguard shield/popup + macwatch popup already honour `isBlocked()`, so
  they're suppressed while the cover is up. g_covert is specifically for AUDIO (bypasses display blocking).
- **NOT built (next increments):** panic-chord entry (fire mid-command — needs the non-blocking
  `UndercoverManager` intercept model, a notes_ui refactor to stateful begin/handleEvent/end); secret-passphrase
  exit (still `q`, which is a tell); duress/decoy; boot-cover. See PLAN-undercover-touch.md.
- Docs: man `uc`, CLAUDE.md undercover entry + System cmd list. Uncommitted; Abdallah builds/tests manually.
- **Notes UI layout fixes** (HW feedback): (a) card day-meta clipped → CARD_H 54->58, meta to screenY+43;
  (b) "Notes header still above [the] watch a little" = the fake status-bar CLOCK ("watch") was oversized
  (drawn at s_fTitle 15px vs mockup ~10px), so it filled the 18px status bar and crowded the "Notes" title
  right below it → shrank the clock to s_fMeta (~11px, middle-left centered) + more gap (APPBAR_Y +6->+8,
  SEARCH_Y 52->54, LIST_TOP -> 88). Pure font/layout-constant tweaks. Awaiting Abdallah's confirmation.

## Session 2026-07-01 (undercover Phase 2 UI — Notes cover, first pass) — compiles clean, awaiting HW feedback
- Abdallah reordered: build the Notes UI as a **test first** ("not full feature like duress password, just
  simple ui"), ahead of Phase 1. Reference = `~/Downloads/trex-undercover-notes.html` (mockup at 2x; real
  px = mockup/2).
- **New module** `core/system/undercover/notes_ui.{h,cpp}` + command **`notes`/`nt`** ([EXP], System). Renders
  fake status chrome (clock/signal/battery), Notes LIST (appbar + "A" avatar + search pill + Pinned/Recent
  labels + tinted `fillSmoothRoundRect` cards w/ title+2-line preview+meta + amber `+` FAB) and note DETAIL
  (back chevron bar + title + word-wrapped paragraphs + checkbox items). Draws straight to global `tft` with
  anti-aliased **Noto Sans** VLW smooth fonts (see below). `displayManager.setBlocked(true)` suppresses the
  real status bar (lock-screen pattern), restored on exit. Nav: touch tap/drag + trackball select/click/
  scroll + `q` quit. 6 hardcoded sample notes.
- **Smooth fonts (Abdallah wanted "modern like an Android app").** First built with LovyanGFX bundled FreeSans
  (bitmap/aliased) — then baked **Noto Sans** (Android's family) Regular+Bold to LovyanGFX **VLW smooth fonts**
  for anti-aliased text. `convert_font.py` (root, mirrors convert_splash.py, `pre:` build step) renders glyphs
  with Pillow → `t-rex-firmware/core/system/undercover/notes_fonts.h` (4 sizes: BIG 20/TITLE 15/BODY 14/META
  11, ~45KB). VLW format reverse-engineered from LovyanGFX `VLWfont::loadFont` (24B header + 28B/glyph metrics
  `unicode,h,w,xAdv,dY,gdX,0` BE sorted + row-major 8-bit-alpha bitmaps). Loaded as 4 persistent `lgfx::VLWfont`
  + `PointerWrapper` (memcpy_P from flash) once/session; `setFont()` switches are free. Script is best-effort:
  no-ops keeping the committed header if Pillow/TTFs absent (PlatformIO's Python has no Pillow → it used the
  committed header, build still clean). Noto Sans OFL-1.1, credited NOTICES #18.
- **Deliberately NOT built yet** (deferred): SD `/notes/*.txt`, secret-passphrase exit, duress/decoy dual
  passphrase, `g_covert` leak-suppression wiring, boot-cover. This is a pure look/feel + nav test.
- Compiles clean both `T-Deck` + `T-Deck-Plus`. Man page + CLAUDE/plan updated.
- **Feedback 1 — "not retro, want modern like an Android app":** replaced bitmap FreeSans with anti-aliased
  Noto Sans VLW smooth fonts (see the font entry above). Compiled clean.
- **Feedback 2 — "the ui flickers so much when I touch the screen":** root cause = every touch/drag event
  repainted the whole view straight to the panel (clear→redraw visible). Fix = **double buffering**: all
  draws routed through a `G` pointer targeting a full-screen 320x240 PSRAM `LGFX_Sprite` (150KB, alloc on
  entry / `deleteSprite` on exit — rule 5c), composed off-screen then one `pushSprite` per frame (csidetect
  pattern). Added `paintList`/`paintDetail` wrappers; graceful fallback to direct-panel if PSRAM alloc fails.
  ✅ **HW-CONFIRMED SMOOTH** by Abdallah ("ui is smooth") — flicker gone; the `lgfx::LovyanGFX*` pointer +
  `pushSprite(&tft,...)` approach compiled + ran fine on his manual build.
- **Visual tuning pass** (2026-07-01, after "ui is smooth"): brought the render closer to the mockup —
  (1) card **tints** restored (blue/green/pink per mockup: Groceries=green, Lemon cake=blue, Movies=pink;
  `Note.tint` is now a 0-3 index → `cardTint`/`cardBorder`); (2) soft **drop shadow** under each card
  (C_SHADOW, 2px offset) for depth; (3) tint-matched hairline **card borders**; (4) **preview text smaller**
  (moved to s_fMeta 11px, secondary look — was same size as title); (5) cleaner **magnifier** icon
  (drawWideLine handle); (6) decorative **share/download** icons in the detail bar (accent, non-functional).
  NOT compiled by me (manual build). Awaiting Abdallah's manual test → then commit "when all is good".
- ✅ Visuals approved ("all good like it") → **committed `b273edd`** ("feat(undercover): GT911 touchscreen +
  Notes cover UI (phase 0+2)", 42 files) and **PUSHED** to origin/feature/pentest-enhancements.
- Next: Phase 1 (g_covert leak audit + UndercoverManager + `uc`/panic-chord triggers), then rest of Phase 2
  (SD `/notes/*.txt` + hidden-exit passphrase), then Phase 3 (boot-cover + dual/decoy duress).

## Session 2026-07-01 (touchscreen activation — Phase 0 of undercover-mode plan) — ✅ HW-VERIFIED & WORKING
- **`test touch` field-tested OK**: GT911 @ 0x5D detected, crosshair tracks finger 1:1 into all four
  corners (orientation correct out of the box — vendor `setSwapXY(true)`/`setMirrorXY(false,true)` config
  was right, no flip needed), tap/long-press/drag classify correctly, fast/responsive, keyboard+trackball
  unaffected on the shared I2C bus. Abdallah: "all works amazing and fast and all good." **Phase 0 complete.**
  Next: Phase 1 (undercover glance cover — `g_covert` flag + `UndercoverManager`).
- **TouchManager** (`core/input/touch/`) — GT911 singleton, `begin()`/`poll()` mirroring `InputHandling`'s
  event-poll style. Reuses existing global `Wire` (SDA=18/SCL=8, already begun by `DisplayManager::init()`).
- **Driver switched mid-session**: first built against `mmMicky/TouchLib`, then Abdallah linked LilyGo's
  official T-Deck example (`Xinyuan-LilyGO/T-Deck examples/Touchpad/Touchpad.ino`), which uses a different
  driver — `lewisxhe/SensorLib`'s `TouchDrvGT911`. Switched to match. Two concrete wins: (1) TouchLib's
  `init()` was found to unconditionally `return true` regardless of I2C ACK (checked the vendored source) —
  would have broken 0x5D->0x14 fallback / "no panel" detection; SensorLib's auto-probe instead verifies the
  actual product-ID register (`==911`). (2) Coordinate mapping is no longer a guess — copied LilyGo's own
  `setMaxCoordinates(320,240)`+`setSwapXY(true)`+`setMirrorXY(false,true)` verbatim for this exact board;
  `poll()` reads already-mapped coords straight from the driver, no hand-rolled axis math.
  `platformio.ini` lib_deps: `mmMicky/TouchLib` -> `lewisxhe/SensorLib @ ^0.4.1`; unused vendored
  `lib/TouchLib` removed. **Registry dep** (Abdallah installed 0.4.1 via PlatformIO into `.pio/libdeps/`),
  not hand-vendored under `lib/` — large multi-driver lib, left as a pinned registry dep like AceButton/NimBLE.
- **0.4.x API version gotcha (handled):** LilyGo's example ships SensorLib 0.2.x (header-only `.tpp`), but
  0.4.1 was refactored (`.cpp/.hpp` split, new `getTouchPoints()` API). Verified the code against the
  *installed* 0.4.1 source, not 0.2.x: `setPins`/`begin(Wire,addr,sda,scl)`/`setMaxCoordinates`/`setSwapXY`/
  `setMirrorXY`/`isPressed` unchanged, but the old `getPoint(int16_t*,int16_t*,n)` overload is now
  `__attribute__((deprecated))` → migrated `poll()` to `getTouchPoints()` → `TouchPoints`/`TouchPoint{x,y}`
  (`hasPoints()`/`getPoint(0)`) to avoid per-compile warning spam.
- **Compiled clean on request** (both `T-Deck` + `T-Deck-Plus` envs, zero warnings/errors, RAM 65.2% /
  Flash 38.9% on Plus). The warning Abdallah hit was `#pragma message: TouchDrvGT911.hpp is deprecated.
  Include TouchDrv.hpp instead` — SensorLib 0.4.x deprecated the per-driver top-level headers for the
  umbrella `TouchDrv.hpp`; `touch_manager.cpp` now includes that instead. (One-off exception to the usual
  don't-compile rule in [[feedback_rules]] — he explicitly asked me to build + diagnose the warning.)
- `poll()` classifies `TAP`/`LONG_PRESS`/`DRAG_START`/`DRAG_MOVE`/`DRAG_END` by travel/hold-time. Wired into
  `main.ino` (`begin()` after `inputHandler.begin()`; `poll()` in `loop()` feeds `PowerSaveManager`/
  `LockScreenManager` activity, then passed through new `LockScreenManager::interceptTouch()` which swallows
  touch while locked, mirroring `interceptTrackball`).
- **`test touch`** diagnostic — folded into the existing `test`/`tst` HW-test dispatcher (NOT a new `tt`
  command — keeps the 64-cmd cap headroom per [[project_improvement_backlog]] #5). Full-screen crosshair +
  4 corner brackets + live raw/mapped x/y + event-type readout — this is the hardware verification step.
- Full spec + build-order + acceptance checklist: `.claude/memory/PLAN-undercover-touch.md`. Phase 0 only;
  Phases 1-3 (undercover Notes-app disguise mode) not started — Phase 1 needs Phase 0 HW-verified first.
- **Next action for the user:** flash + run `test touch`, confirm the dot tracks 1:1 into all four corner
  brackets — should just work now since the mapping is the vendor's own config. Changes uncommitted (user
  compiles/flashes manually).

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
- Undercover mode Phases 1-3 (glance cover / Notes UI / duress) — see PLAN-undercover-touch.md; GT911 touch itself is Phase 0, code-complete pending HW verify (this session)
