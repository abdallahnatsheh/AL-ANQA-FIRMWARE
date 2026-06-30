# T-REX-FIRMWARE — Claude Project Context

## Project
Pentesting firmware for LilyGo T-DECK / T-DECK Plus (ESP32-S3). PlatformIO + Arduino. All source in `t-rex-firmware/`. Build: `env:T-Deck` / `env:T-Deck-Plus` (GPS + speaker). Screen: 320×240, status bar 30px, `outputY=38`.

## Hardware (T-Deck Plus extras in parentheses)
- Display ST7789 · Keyboard PS/2 I2C 0x55 · Battery ADC GPIO4 · SD CS=39
- Audio I2S: BCK=7, WS=5, DOUT=6 — **must use i2s_driver_install(); tone() fails**
- GPS (Plus): RX=44, TX=43, 9600 baud — L76K or u-blox M10Q, ~4 min cold fix
- Power: GPIO10 must be HIGH

## Architecture

**Command system** (`command_manager.cpp/h`): `registerCommand(name, shortName, fn, desc, hasArgs, category, compType=COMP_NONE)` — max 64, all one-liners in `setupCommands()`. Categories: System · WiFi · Network · Bluetooth · SD Card · Diagnostics.
- Dispatch uses `Utils::matchesCmd(cmd, prefix)` — requires space or NUL after prefix (not bare `startsWith`). Critical: prevents a short name matching a longer one (e.g. `psv` must not dispatch as `ps`).
- `CompType`: `COMP_NONE` (no file args) · `COMP_ANY` (ls/cat/rm) · `COMP_DIR` (cd) · `COMP_FILE` (ux)
- History: 16-entry ring buffer in `_hist`; trackpad UP/DOWN navigates; `_histSaved` preserves in-progress line
- Autocomplete: `'` key (Sym+K = 0x27, defined `KEY_AUTOCOMPLETE` in `input_handling.h`); fills common prefix, single match adds space, multiple lists up to 8

**Display** (`display_manager.cpp/h`): all output via `displayManager` — never `tft` directly. `clearScreen()` = below header only. `tdeck_begin()` = full reset.

**Input** (`input_handling.cpp/h`): `getKeyboardInput()` → `char` or `0`. Every blocking loop must poll for `q`.
- Backspace hold-repeat: `_repeatKey` / `_repeatStart` / `_lastBsReturnMs`. Hold **1500 ms** → auto-delete at 80 ms intervals. Any char key stops repeat and resets `_lastBsReturnMs = 0` so the next `\b` press starts a fresh hold immediately. Second `\b` while timer is armed cancels it (prevents accidental auto-delete on rapid taps). usbkbd / btkbd use identical logic via `bsLastBsMs` / `lastKey == '\x08'`.

**GpsManager** (`gps_manager.cpp/h`) — T-Deck Plus only, singleton:
- FreeRTOS task core 0, 30 ms poll, volatile primitives (no mutex needed on ARM32)
- Init order: `begin(9600)` → `initL76K()` ×3 (≈4.5s M10Q boot window) → `begin(38400)` + `recoverUblox()` → `updateBaudRate(9600)` + `recoverUblox()` — must mirror test_gps.cpp exactly
- Status bar: grey=off, yellow=searching, green=fixed

**TrackMe** (`trackme.cpp/h`):
- Gate1=signature · Gate2=score(max 100) · Gate3=GPS≥200m OR time≥5min — WARNING/ALERT need Gate3
- BLE scan: continuous (`pScan->start(0,false)`, `setScanCallbacks(&s_bleCb,true)`) — `TmBleScanCb::onResult()` writes into lock-free `volatile _bleRing[TM_BLE_RING]` (mirrors `bmon.cpp` ring pattern); `drainBleRing()` processes all pending entries every fast-loop tick. `pScan->stop()` + `setScanCallbacks(nullptr)` + `clearResults()` on exit — prevents dangling-ptr crash from `scanblue` reusing the singleton
- `lastSightingMs` gates per-device scoring (sightings/rssiHistory/gap-return/distinctWindows) to ~1Hz so continuous scanning doesn't break Gate2/Gate3 time-based thresholds; RSSI Kalman smoothing still updates on every ring entry
- Main loop: fast tick (~50ms) drains BLE ring + polls keyboard (`_pendingKey` lets `doWiFiSniff` exit early on keypress); slow tick (1s) runs GPS/WiFi-sniff/`markGaps`/`runScoring`/draw via `needDraw` flag
- WiFi probe sniff: Plus only (`#ifdef BOARD_TDECK_PLUS`)
- UI: RPP=7 rows · `[v]` toggles Tier1/Tier2 view (`viewMode`, separate `page`/`page2`) · `[o]` cycles sort `NONE→SCORE→RSSI→NONE` (Tier2 has no score, falls back to RSSI) — active sort column highlighted yellow in table header · `[f]` toggles alert-only filter on Tier1 (`[ALERTS]` tag in status line) · `[h]` opens full-screen help overlay (legend + keys, any key dismisses) · `[c]` clears, `[w]` whitelist, `[s]` save, `q` quit
- Sort/filter build a local `int idx[]` index array (insertion sort, ≤100 entries) — never reorders `tier1[]`/`tier2[]` themselves
- Transient `_uiNoticeMs`/`_uiNoticeText`/`_uiNoticeColor` (1.5s) confirms view/sort/filter toggles in the alert bar; `_sdNoticeMs` (3s) shows "Saved to SD"; empty-state messages shown when a view/filter has no rows
- `drawHeader()` calls `dm.updateStatusBar()` only — no custom red banner (previously conflicted with `ClockManager`'s 3s global status bar refresh, both targeting y=0-30); `[MUTE]` indicator moved to the alert bar
- Whitelist: `/apps/trackme/known.csv` · Signatures: `/apps/trackme/signatures.csv`
- **Two-mode signature matching** (`TrackerSig` carries both `companyId`/`payloadByte`/`minMfrLen` AND `svcUuid`/`svcByte`): Apple matched by **manufacturer data** (company `0x004C` + payload type, AirTag=`0x12`); non-Apple trackers matched by **16-bit service-data UUID** — Tile `0xFEED`, Samsung SmartTag `0xFD5A`, Chipolo `0xFE33`, Pebblebee `0xFA25`, Google FMDN `0xFEAA` (requires service-data[0]==`0x40` to skip Eddystone beacons). Verified against seemoo-lab/AirGuard. (Eufy/Motorola/Hama/Jio/Rolling Square tags ride Google FMDN → caught by `0xFEAA`.) `onResult()` extracts the tracker service UUID via `dev->getServiceData(NimBLEUUID((uint16_t)x))` (same API bmon uses) into the ring (`TmBleEntry.svcUuid/svcByte`); `matchSig(companyId,mfrType,mfrLen,svcUuid,svcByte)` checks service-UUID sigs first, then manufacturer. Old company-ID rows for Tile/Samsung/Chipolo/Google were removed — they rarely matched real tags in finding mode and Samsung/Google IDs also matched ordinary phones (false positives). Eufy/Pebblebee tags ride Apple Find My (`0x12`) or Google FMDN and are caught by those.
- **Signatures = built-ins + SD (merge, not replace)** (`loadSignatures()`): `fillBuiltinSigs()` always loads first, then the SD file is **appended** (exact dup on companyId+payloadByte skipped). CSV format: `name,companyId,payloadByte,minLen,level` — only name+companyId required; `payloadByte` blank/`any`=wildcard, `minLen` blank=0, `level` `NONE|NOTICE|WARNING|ALERT` (blank=WARNING, `tmParseLevel()`). SD rows are company-ID only (`svcUuid=0`); service-UUID trackers are built-in and not addable via CSV. SD file should contain only EXTRAS (e.g. extra Apple message types `0x03/0x0A/0x0B/0x0C/0x0D/0x0E`→NONE). Ready-made extras CSV in `sd_dropins/apps/trackme/`

**PowerSaveManager** (`powersave_manager.cpp/h`) — singleton:
- Hooked into `getKeyboardInput()` — `update()` every poll, `updateActivity()` on keypress — works globally, no per-command changes needed
- Inactivity dim + battery-aware dim (force dim below threshold)
- `init()` calls `tft.setBrightness()` directly · SD config: `/pwrsave.json` (key=value)

**LockScreenManager** (`lockscreen_manager.cpp/h`) — singleton:
- `intercept(k, now)` hooked at the return of `getKeyboardInput()` — swallows all keys and draws lock overlay when locked; `interceptTrackball(evt)` called in `main.ino` loop before `processTrackball` — swallows all trackball events while locked
- Lock triggers: `lock` command (immediate) · hold trackpad center 3 s (GPIO0 LOW; `clearPendingClicks()` called before `lock()` to suppress stale TBALL_CLICK) · **lock-on-boot** (`lock boot on` → `lockonboot=1`; `init()` calls `lock()` after `loadConfig()` when `_hasPassword && _lockOnBoot`)
- Idle auto-lock: `_timeout` seconds of no `getKeyboardInput()` activity; 0 = disabled
- No-password mode: Space ×3 to unlock (shows `(1/3)` / `(2/3)` progress on instruction line); all trackball events are swallowed while locked
- **Storage = SD ONLY** (`/config/lockscreen.conf`), by explicit user choice — the PIN deliberately does NOT go in NVS. Trade-off the user accepts: removing the SD = a cardless boot loads no PIN → boots unlocked (this IS the forgot-PIN recovery), and the card stays plain-FAT/PC-readable. Do NOT move this to NVS (was tried 2026-06-28, user reverted it — see [[progress_log]]).
- Recovery (forgot PIN): (a) **remove SD + reboot** → no config → boots unlocked; or (b) one-shot **`reset=1`** line hand-added to `/config/lockscreen.conf` on a PC → `init()` clears hash/salt + rewrites the file without the flag (keeps timeout/lockonboot). Both are owner-convenience, not extra security — the lock protects the running device, NOT the SD contents (removable plaintext)
- **NO SD-access gate while locked** (tried + REVERTED 2026-06-28). Locking does NOT block SD I/O — apps must keep saving while the screen is locked (wardrive logging, `wg bg`/`mw bg` loggers, bmon, etc.). A gate was pointless anyway: a locked device has no CLI, so `ls`/`wp`/`cat` can't be typed regardless; the gate's only real effect was breaking legitimate background/foreground saves. Do NOT reintroduce it.
- **USB MSC keeps the idle-lock alive** (`usb_manager.cpp` MSC loop calls `LockScreenManager::updateActivity()` every iteration) — during MSC the user types on the PC, not the T-Deck, so without this the idle timeout auto-locks mid-session and breaks the host write/eject. Same latent risk for any long no-keypress session (e.g. `ux` BadUSB run) — not yet addressed
- PIN mode: type PIN → Enter to confirm; wrong-PIN → 1.5 s red flash cooldown; Esc → back to dormant; up to 16 chars, any printable keyboard character
- PIN stored as SHA-256(saltHex + pin) using mbedTLS context API; 8-byte random salt via `esp_random()`
- Config: `/config/lockscreen.conf` (key=value: `timeout`, `hash`, `salt`, `lockonboot`; `reset` parsed-only, never written back). `saveConfig()` returns `bool` — cmd functions check it and print yellow "No SD — active this session only" on false. NVS `lockscreen` namespace is used ONLY for the legacy `wipe` flag, not the PIN
- Dormant screen: Nokia-style ASCII padlock art, instruction line, live locked-duration counter (HH:MM:SS, refreshed every 1 s)
- **Display blocking**: `lock()` calls `displayManager.setBlocked(true)` — all `DisplayManager` output methods no-op while blocked; lock screen draw functions (`drawDormant`, `drawPinScreen`, `refreshDuration`) temporarily call `setBlocked(false)` → draw → `setBlocked(true)` to bypass
- **Unlock redraw**: unlock paths call `setBlocked(false)` + set `_justUnlocked = true` + `clearScreen` + `printCommandScreen`. Interactive apps poll `consumeJustUnlocked()` each iteration — paginated tables (sw, sbl, nd, ps, ts, man) break their inner wait-loop triggering a re-render; wguard redraws full header+layout; cat viewer sets `needsRedraw`; ls redraws the "any key" prompt; beacon flood / trackme / hiddenssid skip timed draws while blocked; buddy redraws left panel + pet sprite (`lastPid[0] = '\1'` + `s_petDirty = true`); usbmsc (um) redraws MSC screen via `drawMscScreen()` lambda
- **buddy lock guard**: `drawStatus()`, `drawPopup()`, `petTick()` all write directly to `tft` (bypassing `DisplayManager`) — each is guarded with `displayManager.isBlocked()` checks; `spr.pushSprite()` inside `petTick` returns early if blocked

**EvilTwin** (`eviltwin.cpp/h`):
- OPEN → clone exact MAC + channel; WPA2 → random LA-MAC (`(x & 0xFE) | 0x02`)
- Deauth pauses automatically when portal clients connected
- Templates: 2 built-in (Google/Router) + `/apps/eviltwin/portal/*.html` (up to `ET_TEMPLATE_MAX`=64) · Logs: `/apps/eviltwin/creds.csv`
- `[p]` opens unified `pickTemplate()` picker — built-ins + all SD `.html`/`.htm` files, paginated `ET_PER_PAGE`=8, current selection highlighted green; switching stops/restarts `server`+`dns` (both must restart — `dns.start()` after `dns.stop()` was missing, broke captive-portal popup after switching)
- Transient `_uiNoticeMs`/`_uiNoticeText`/`_uiNoticeColor` (1.5s) confirms portal switch in green; `handleRoot()` shows red "Portal file missing — fell back" if a custom template file disappears mid-session
- **Cred capture is RAM-only** — `handlePost()` stores `{user,pass,ts}` into `_creds[ET_MAX_CREDS=30]` with no SD write (GDMA rule: never write SD while soft-AP/promiscuous DMA is live; the form-submit moment is the worst time to corrupt FatFS). `_captureCount` counts all POSTs; overflow past 30 is dropped (shown red in creds table), not silently "see SD log"
- **Incremental GDMA-safe flush** — `flushCredsToSD(bool wifiSafe)` appends only `_creds[_savedCount..total)` (tracks `_savedCount`, never `SD.remove()`/rewrites, preserves timestamps). `[s]` calls it with `wifiSafe=false` → pauses promiscuous around the write, mirrors wguard pattern, shows "Saved N new"/"Already saved" in the transient notice (no full-screen takeover). Exit calls it with `wifiSafe=true` after AP down + promiscuous off + `WiFi.mode(STA)` — fully safe, persists the unflushed remainder
- `[c]` creds table services `dns.processNextRequest()`+`server.handleClient()` in its wait loop so the portal stays live while the operator views captures
- **Path- and field-agnostic capture** — portals disagree on form target and field names. Built-in templates POST to `/post` (`user`/`pass`); Bruce-style SD portals in `/apps/eviltwin/portal/` GET/POST to `/get` (`email`/`password`, `uname`/`psw`…). `setupRoutes()` registers BOTH `/post` and `/get` for `HTTP_ANY`, plus `onNotFound` → all funnel to `handleCapture()`. `captureArgs()` iterates `server.args()` and classifies each by case-insensitive substring (`etIsUserField`: email/user/login/uname/account/phone/identifier/id/name/tel · `etIsPassField`: pass/pwd/psw/passcode/pin · `etIsIgnoreField`: remember/token/csrf/captcha/submit/viewport… never taken as username in fallback). Fallback: if a password is found but no recognized username field, take the first non-junk non-empty arg. This is why SD portals that previously captured nothing now work — the old code only read POST args `user`/`pass` on `/post`, so every `/get`-based portal fell through to the redirect and lost the creds
- `handleRedirect()`: 302 + `Captive-Portal-URL` header + HTML meta-refresh body — empty body was breaking iOS/Windows. `handleCapture()` reuses it after grabbing args (reload looks like a failed login → victim re-enters → captured again)

**HiddenSSID** (`hidden_ssid.cpp/h`):
- Deauth burst every 3s + promiscuous sniff for subtype 5 (probe response) or 0 (assoc request) matching target BSSID
- `snifferCb` + `WiFiMonitor::extractSSID()` both `IRAM_ATTR` (callback chain cache safety)
- On found: stop sniff immediately, I2S two-tone beep (unless `silent`), save `BSSID,SSID,ch` → `/apps/hiddenssid/found.csv`
- Dedup: `_wf.refreshHiddenCache()` + `isHiddenKnown()` before append — no duplicate lines
- Scan table integration: known-hidden shows `~name` in cyan; unknown stays `<hidden>` in grey

**NetworkScanner** (`network_scanner.cpp/h`):
- ARP scan full /24 · Port scan: `std::vector<int> openPorts` collected once then paginated

**NetSpy** (`wifi/intel/netspy.cpp/h`) — `netspy`/`ns` (Network) [EXP] — client-isolation device recon:
- **Purpose:** discover devices on a WiFi with **client isolation** (AP blocks client↔client unicast), where `nd` (ARP scan) sees only the gateway. Technique from **AirSnitch** (Vanhoef, NDSS 2026) — paper only, no code; credited in file header + NOTICES #15. **Own networks only.**
- **100% PASSIVE — never transmits.** Key finding (HW-verified): while ASSOCIATED, the ESP32 WiFi HW already DECRYPTS the group/broadcast frames the AP relays to all clients; promiscuous delivers them in CLEAR (CCMP header kept, payload plaintext at `hdrlen+8`). So discovery needs **NO software CCMP / NO GTK** — just sniff group data frames (fromDS, A1 group, A2==our BSSID) + parse. Active attacks are a deliberately SEPARATE future command (`isoscan`/`is`, not built) so recon can't accidentally transmit.
- **Parsers** (`nsParse` on LLC/SNAP): ARP (sender MAC+IP), IPv4 (src MAC+IP), then UDP → **DHCP** (67/68: chaddr MAC + yiaddr/opt50 IP + **opt-12 hostname**), **mDNS** (5353: `<host>.local` from A/AAAA + service types from PTR/SRV names, responses/QR=1 only), **SSDP** (1900: `SERVER:` product string + DLNA). `dnsName()` handles DNS label compression (0xC0 ptrs) with a jump-guard + full bounds checks. **`NS_PL_MAX=400`** capture cap (DHCP options sit past the BOOTP sname/file fields @240).
- **Device table** `NsDev[48]`: MAC, IP, `name[24]` (DHCP/mDNS host, or SSDP product — `strongName` so a model can't clobber a real hostname), `vendor`/`type` (via `oui_lookup.h`), `svc` 11-bit service bitmask (AirPlay/Cast/Apple/Printer/SSH/SMB/HomeKit/Spotify/Alexa/HTTP/DLNA), `how` flags. Capture ring drains in the main loop (not the cb) → parse.
- **UI:** trackball U/D row select (highlight bar + `>`), color-coded (name=cyan, vendor=grey, sel=yellow), `+` marker on rows with services. **Enter (or `[i]`)** = full detail overlay (MAC/IP/Name/Vendor/Seen-flags/Services) — any key returns. (Trackball CLICK is deliberately NOT the detail trigger — user found it annoying; Enter is the primary.) **`[p]` ping / `[o]` port-scan the SELECTED device in place** (`nsProbe()`) — suspends promiscuous (ps/pg need normal TCP/ICMP), calls `networkScanner.pingHost`/`topPortScan` with the row's IP, then resumes sniffing (s_dev table preserved); better UX than the `nd`-style index shortcut (no stale indices, probes the device you're looking at). `[s]` save → `/apps/netspy/NNN.csv` (**`ScopedPromiscPause`** — promiscuous is live, GDMA rule), `[c]` clear, `[l]/[a]` page, `q` quit. HOW flags: **A**=ARP **I**=IPv4 **D**=DHCP **M**=mDNS **S**=SSDP. CSV saves ALL fields incl. the services column.
- **CLI targeting:** table shows a `#` index column; netspy exports `netspyDeviceCount()`/`netspyDeviceIp(idx)` (netspy.h, read `s_dev[]` which persists after exit like the ARP cache). `network_scanner.cpp` `resolveTarget()` parses a source prefix: bare `#`/`nd#` = netdiscover ARP index, **`ns#` = netspy index** → so **`ps ns3`, `ps top ns2`, `pg ns0`** target the netspy list (all 3 scan paths share `resolveTarget`). Two ways to probe a discovered device: in-app `[p]`/`[o]` (interactive) or `ps/pg ns#` (CLI).
- **Subcmds:** `ns gtk` (show live group key from `gWpaSm+0x174` — reads RAM, doesn't transmit; Stage-2 groundwork), `ns dump` (`gWpaSm` hex → `/apps/netspy/gwpasm.txt`). **Platform PINNED `espressif32@7.0.1`** (the gWpaSm offset is framework-specific).

**TextEditor** (`core/editor/text_editor.cpp/h`) — `edit`/`ed <path>`, nano-style SD editor:
- Free function `runEditor(char*)` (wardrive pattern). Buffer = file-static `std::vector<String>`, freed on exit (`clear()`+`shrink_to_fit()` — rule 5c). Loads ≤`ED_LOAD_CAP`=500 lines; larger file → `g_readOnly` (edits no-op, can't save — data safety). Missing path → new empty buffer, created on first save.
- **Control scheme forced by the I2C keyboard** (one resolved byte/key, no Ctrl/Esc/arrow codes): keyboard types/backspaces/Enter-splits (**auto-indent**: new line inherits leading whitespace); **trackball U/D/L/R = cursor** (wraps across line ends; **`accelStep()` acceleration** — same-dir moves <90ms apart double the step up to 16, so a fast roll pages big files); **CLICK = command menu** (Save/Save As/Find/Go to line/Top/Bottom/Undo/Cut line/Paste line/Exit). No `q` quit — exit only via menu (`q` is a typeable char). Exit-with-unsaved → `[s]`save/`[d]`discard/click-cancel prompt.
- **Single-level undo** (`snapshot()`/`doUndo()`): one full-buffer copy, coalesced per run via `g_lastAction` (ACT_TYPE/ACT_DEL/ACT_NONE) — snapshots before the first char of a typing run, first of a delete run, and every structural op (Enter/Cut/Paste). Freed on exit.
- Renders direct to global `tft` on the 6px Font0 grid (ssh-style): `ED_COLS`=52 × `ED_ROWS`≈12, inverse-cyan block cursor, h/v auto-scroll (`adjustScroll`), scrollbar. **Per-row dirty rendering** (`g_rowDirty[]`/`g_allDirty`/`g_hintDirty` → `flushDraw()`): a keystroke/cursor-move redraws only the affected row(s) + the title; scroll/structural changes set `markAll()`. Lock-aware: all draws guarded by `displayManager.isBlocked()`, full redraw on `consumeJustUnlocked()`, and **all input handling is skipped while blocked** (so locked keys/trackball can't edit invisibly). Save = `SD.open(path, FILE_WRITE)` (truncates) + one `\n` per line + `ensureParentDirs()` (recursive mkdir for new folders); no WiFi so no GDMA concern. Sub-loops (`runMenu`/`promptLine`/`confirmSaveExit`) are all lock-aware.

**BeaconFlood** (`beacon_flood.cpp/h`):
- `bf [list|seq <base>|file [path]]` — raw 802.11 beacon injection, 109-byte fixed-length frames
- Modes: `list` (built-in 40 SSID list, PROGMEM), `seq <base>` (base1…base9999), `file` (one SSID/line from SD, default `/wordlist_beacons.txt`)
- Random LA-MAC per beacon (`(mac[0] & 0xFE) | 0x02`), channel hops 1→6→11→2→7→12… every 20 beacons
- WiFi setup: `WIFI_MODE_APSTA` + `softAP` + promiscuous = same pattern as deauth. Teardown: `WiFi.mode(WIFI_STA)`.
- No SD access during flood (GDMA rule) — file is opened before injection starts, closed after stop
- Display: `[BCON::FLOOD]` header, live Ch/Sent/Err/Rate/SSID stats, `[q]` stop

**WGuard** (`wguard.cpp/h`) — passive WiFi IDS:
- `wg <index|bssid> [ch]` interactive · `wg <index|bssid> [ch] bg` background · `wg stop` · `wg view`
- Detects: BCAST DEAUTH · DEAUTH storm · EVIL TWIN · HANDSHAKE harvest · BSSID CLONE · BEACON FLOOD · AUTH flood · PROBE storm · PMKID grab · KARMA attack
- Evil twin: two-tier — `_pendingForeign[]` INFO until deauths arrive → WARNING upgrade. RSSI filter > -82 dBm prevents extender false positives. 3s beacon-silence expiry on `_evilTwinSeen[]` allows re-detection after attacker restarts AP.
- **Clone detection — 3-signal logic**: `0xFD` (ts-jump) → WARNING. `0xFA` (BCN interval compression) alone → WARNING "BCN COMPRESS+TS-JUMP (no deauth, possible enterprise AP)". CRITICAL fires only when `0xFD` + `0xFA` + deauth burst all present (`_cloneDeauthSeen`/`_cloneDeauthTs`). Enterprise APs (Fortinet, Cisco mesh) produce ts-jump + compression legitimately but never deauth clients — 3rd signal eliminates false alarms.
- **Clock-skew fingerprint (case 0xF9)**: fires INFO only — does NOT upgrade `_cloneWarnActive`. Reboot resets BSS timestamp to ~0 triggering both 0xFD + 0xF9 simultaneously (indistinguishable from clone). 0xF9 also beatable by attacker syncing TSF. Only deauth+0xFA combo is reliable upgrade signal.
- Rate limits: BCAST DEAUTH / DEAUTH storm / HANDSHAKE harvest all throttled to once per 30s per source MAC via `lastFired` field in `WgCounter`. Notification sound throttle: WARNING ≤1/10s, ALERT ≤1/5s via `notifyThrottled()`.
- Session files: `/logs/wguard/NNN.csv` — scans SD on init to find next free number (never overwrites). Columns: `time,severity,rssi_dbm,message`. Timestamps are session-relative (`e.ts - _sessionStartMs`). Each save block writes only new events via `_savedEvCount` tracker — no duplicates. Save types: AUTO-SAVE (ring full, clears ring + resets `_savedEvCount`) · CHECKPOINT (every 2 min, skipped if nothing new) · MANUAL ([s] key, footer shows `Saved N events` / `Nothing new to save` for 2.5s) · FINAL (session end).
- GDMA: all SD writes pause promiscuous (`s_active=false`), write, resume.
- Background: `pollBackground()` drains ring, triggers saves, shows popup bar + shield icon in status bar. After `doAutoSave()` clears the ring, `_lastBgHead` is reset to 0 — prevents stale popup on the next poll cycle.

## WiFiMonitor (`wifimon_functions.cpp/h`) — enhanced `wm`
- Two views: **Nets** (`[v]` to switch) shows BSSID/Ch/RSSI/client-count/SSID; **Clients** shows MAC/vendor/type/RSSI/AP
- Client count is computed live from `_clients[]` on every draw — never stale
- Client detection: data frame DS bits (ToDS/FromDS), probe requests (unassociated), assoc requests
- Trackpad UP/DOWN moves cursor in Clients view; `[d]` deauths selected client (directed deauth to that STA only)
- Targeted deauth: stop promiscuous → APSTA → inject AP→STA + STA→AP deauth+disassoc × 5 rounds → STA+promiscuous resume
- **Raw PCAP sniffer**: dual ISR pipelines — parsed ring (display) + raw ring (PCAP file)
  - Saves to `/apps/wifimon/<uptime_ms>.cap` — libpcap linktype 105 (LINKTYPE_IEEE802_11), Wireshark/aircrack-ng compatible
  - Flush every 2s or ring 25% full: pause promiscuous ~5ms → write SD → resume (GDMA rule)
  - Drop counter (`s_pcapDropped`) shown on screen as `1234 frm -N`; drops don't corrupt file
  - `[s]` toggles PCAP on/off; auto-starts on launch if SD available
  - Ring: 64 slots × 262 bytes = ~17KB DRAM
- **Probe logger**: passive directed-probe harvest → `/apps/wifimon/probes.csv`
  - `[p]` toggles on/off; starts OFF — user must press `[p]` to begin logging
  - Only logs directed probes (non-empty SSID) — wildcard broadcast probes skipped
  - Dedup in RAM: 64-entry circular ring (MAC+SSID), never writes the same pair twice per session
  - Flush piggybacked on PCAP pause window; standalone 5s flush when PCAP is off
  - Stats row shows `Log:N` (unique pairs saved) when active; cyan `[p]` in controls when logging
  - CSV: `time_ms,mac,vendor,ssid,rssi` — human-readable, no Wireshark needed
- Client expiry: unassociated clients dropped after 90 s silence via `expireClients()`
- Status banner: green = deauth ok, red = fail/unassoc, yellow = info; auto-clears after 3.5 s

**OUI lookup** (`oui_lookup.h`) — shared header-only, ~350 entries, returns `{vendor, type}`:
- Types: Phone / Laptop / Router / IoT / TV / Gaming / Attack / Embed / RandMAC
- LA-MAC (locally administered bit) → `{"LA-MAC", "RandMAC"}` — no table entry needed
- Covers: Apple, Samsung, Huawei, Xiaomi, OnePlus, Oppo, Sony, Nintendo, Xbox, LG, Motorola, Intel, Dell, HP, Lenovo, ASUS, TP-Link, Netgear, D-Link, Ubiquiti, Cisco/Linksys, MikroTik, Amazon, Google, Roku, Philips Hue, Alfa, Hak5, RPi, Espressif
- `wguard.cpp` uses `ouiVendor()` (backward-compat wrapper); replaces old private `lookupOui()`

## Commands
System: `help/hlp` `info/inf` `clear/clr` `MATRIX/matrix` `pwrsave/psv` `sleep/slp` `lock/lk`
WiFi: `scanwifi/sw` `connectwifi/cw` `wifipass/wp` (`wp export`/`wp clear` — merged wifiexport+clearwifi) `wifimon/wm` `deauth/da` `eviltwin/et` `hiddenssid/hs` `macchanger/mc` `wpasniff/ws` `pmkid/pm` `karma/km` `crack/cc` `wguard/wg` `beaconflood/bf` `wardrive/wd` `espsniff/es` `esptest/est` `espchat/ec` `espvoice/ev`
Network: `netdiscover/nd` `netspy/ns [EXP]` `portscan/ps` (`ps top <ip|#>` — merged topscan) `ping/pg` `ssh/sc`
Bluetooth: `scanblue/sbl` `bleinfo/bi` `trackme/tm [silent]`
SD: `sdinfo/sdi` `sdls/ls` `cd/cd` `cat/cat` `edit/ed` `rm/rm` (`rm -d <dir>` = recursive dir delete) `sdf/sdf`
Diagnostics: `gps/gps` `test/tst` (`test spk|mic|lora` — merged spktest+mictest+loratest) `i2cscan/isc [EXP]` `csidetect/csi [EXP]`

**ESPChat** (`espchat/ec`, `espsniff/es`, `esptest/est`) — `radio/espnow/espchat/`, `espsniff/`, `esptest/`:
- Wire format: `EcMsg{type(1)+seq(1)+name[12]+text[100]}` = 114 bytes, type=0x01; broadcast ch compatible with any ESP32/ESP8266
- Public chat: `WIFI_STA` + broadcast peer (FF:FF:FF:FF:FF:FF) unencrypted
- Private chat: ESP-NOW CCMP AES-128, LMK = SHA-256(PIN + sorted(mac_A, mac_B))[:16]
- Pairing: initiator derives LMK immediately, adds encrypted peer; receiver sends "* pair ok" encrypted; initiator replies "* pin ack" encrypted — wrong PIN = frame dropped; 3 attempts, fail = `ecRemoveContact()`
- Background (`ec bg`): `pollEspchatBg()` hooked in `getKeyboardInput()`; `EC` badge in status bar; public=PING, private=INFO notification
- Contacts: SD → `/apps/espchat/contacts.csv`; no SD → `g_ecContacts[]` RAM only, cleared on reboot
- UI layout: PAIR_Y(y=66) · MSG_Y0(y=80) · EC_VIS=7 rows · SEP2_Y(y=178) · FOOT_Y(y=192) · INPUT_Y(y=206); 4px scroll slider at x=316
- All WiFi commands call `stopEspchatBg()` before starting to avoid ESP-NOW/WiFi mode conflicts

**ESPVoice** (`espvoice/ev`) — `radio/espnow/espvoice/espvoice.cpp` — half-duplex ESP-NOW walkie-talkie, HD voice:
- **NOT board-gated** — ES7210 mic + speaker exist on both T-Deck and T-Deck Plus (only GPS is Plus-only). Same applies to `test mic` (mic test).
- **Codec**: ITU-T G.722 wideband (16 kHz, 64 kbps Mode 1) via vendored public-domain `lib/libg722/` (sippy/libg722, auto-discovered by PlatformIO LDF — NOT in `lib_deps`, same as `lib/es7210`). One 20 ms frame = 320 PCM samples → 160 G.722 bytes. Stateless across loss (dropped frame = 25 ms gap, no drift). Encoder/decoder ctx = `g722_encoder_new(64000, G722_DEFAULT)` / `g722_decoder_new(...)`, created at start, destroyed at end.
- **Wire format**: `EvMsg{type=0x02, kind, seq, g722[160]}` = 163 B. `kind`: 0=voice, 1=EOT (end-of-transmission/Roger marker). Broadcast peer FF:FF:FF:FF:FF:FF, unencrypted.
- **PTT is a TOGGLE** — the I2C keyboard reports no key-up (only `\b` auto-repeats), so true hold-to-talk is impossible. SPACE toggles TX↔RX.
- **Both I2S ports stay resident** the whole session (mic=I2S_NUM_1 RX, speaker=I2S_NUM_0 TX — separate peripherals). Installing/uninstalling I2S drivers on every PTT toggle while ESP-NOW DMA is live CRASHED (brownout/DMA); coexist-resident fixed it. Audio gated by PTT state, not by driver install.
- **Mic read**: ES7210 `ALL_LEFT` delivers 2 int16/sample (L/R dup) — de-dup (`raw[2*i]`) to mono 16 kHz before encode. RX: decode → duplicate mono→stereo → speaker 16 kHz. No resampling (G.722 is natively 16 kHz).
- **Walkie-talkie signaling**: talker shows `TRANSMITTING`; listener shows `>> RECEIVING <mac>` while frames arrive; on PTT release talker broadcasts EOT ×3 (lossy net) → both ends play a 120 ms 1500 Hz Roger beep; silence-timeout fallback (500 ms, `EV_RX_SILENCE`) ends RX if all EOTs lost.
- **App-local audio controls (never touch global `vol`/NotificationManager)**: `s_vol` RX playback 0–150 % (`+/-`, capped — >150 % hard-clips into distortion + brownout risk); `s_gain` TX mic gain 0–37.5 dB (`o/p`, ES7210 `es7210_adc_set_gain_all`, applied live — the CLEAN way to be louder). Both reset to defaults (100 %, GAIN_30DB) each launch. Channel on `,/.`.
- RX ring `s_rxRing[EV_RING=24][160]` filled by `onRecv` (WiFi task), drained in main loop; lock-aware (`consumeJustUnlocked()` redraw, draws no-op while blocked); clean screen on quit. Registered with `stopEspchatBg()` prefix.

**SSHClient** (`ssh/sc`) — `wifi/tools/sshcon/ssh_client.cpp`:
- Interactive SSH client on **LibSSH-ESP32** (`ewpa/LibSSH-ESP32 @ ^5.8.0` in `lib_deps` — a real registry dep, unlike the vendored `lib/` ones; libssh 0.11.x, uses the SDK's mbedTLS + HW AES/SHA). Both boards.
- **Runs in a dedicated ~50 KB FreeRTOS task** (`xTaskCreatePinnedToCore(..., 51200, ..., core 1)`) — libssh needs far more stack than the 8 KB main-loop task. `runSshCon()` (main task) prompts user/password, creates the task, then blocks on `while(!s_taskDone) vTaskDelay()`. Only the SSH task touches `tft`/`inputHandler` while main waits → no concurrency on display/keyboard.
- Reuses the existing `cw` STA connection (checks `WiFi.status()==WL_CONNECTED`) — does NOT re-init WiFi. Password auth via `ssh_userauth_password`; `SSH_OPTIONS_PROCESS_CONFIG=0` to skip nonexistent `~/.ssh/config`. PTY = `xterm` (16-colour). Host-key verification SKIPPED for now (TOFU).
- **Terminal**: scrollback ring `s_buf[SB=120][COLS=52]` + per-cell colour `s_col[][]` (hi nibble fg, lo bg, ANSI-16 → `PAL[16]` RGB565). Visible ROWS=13 below a 1-line header. Minimal VT100 (`termPut` state machine: SGR colours incl 256-skip, cursor `H/A/B/C/D`, erase `J/K`). Trackpad UP/DOWN scrolls `s_view`; typing snaps to live; CLICK disconnects. Per-row dirty + 30 fps throttle.
- **KNOWN ISSUE**: docs recommend `CONFIG_MBEDTLS_HARDWARE_SHA` disabled for concurrency stability — impossible with precompiled Arduino core. Crash during connect/key-exchange → shared HW SHA engine is the suspect.
- **Host profiles** (`ssh save/list/rm`): `/apps/ssh/hosts.csv` = `name,ip,port,user` (NO password — plaintext on removable card). `ssh <name>` resolves a saved profile first, else uses the token as ip/hostname directly. User precedence: arg > profile > prompt. Read/write at command time (WiFi idle), not during the session. `hostLoadAll/Find/Save/Remove/List` in ssh_client.cpp; `FILE_WRITE`="w" truncates on ESP32.
- SD: `known_hosts`/`keys/` (host-key pinning, key auth) still planned. No SD writes during a live session (GDMA: SSH keeps WiFi DMA busy).

## SD Layout
v2 reorg: every tool gets its own self-contained folder under `/apps/<tool>/`
(logs, captures, wordlists, and tool-specific config all together); device-wide
settings live in `/config/`. `ensureTreeStructure()` creates the FULL `/config` +
`/apps/<tool>` tree eagerly on `begin()`, `performFormat()`, and
`initializeTDeckStructure()` — the tree exists from first boot/format, not
created lazily on first use. `ensureAppsReadme()` writes `/apps/README.txt` once,
mapping each `/apps/<folder>` to its owning command for anyone browsing the card
on a PC. Fresh-start reorg — old files at pre-reorg paths (e.g. root
`/pwrsave.conf`, old `/logs/...` or `/config/...` paths from the v1 layout) are
orphaned, not migrated.

**Root**
`/wpa_supplicant.conf` — saved WiFi credentials (Linux-compatible key=value, stays at root by convention)
`/wpa_supplicant.bak` — auto-backup before first T-Rex modification

**`/config/`** — device-wide settings, key=value unless noted
`/config/pwrsave.conf` — power save config
`/config/macchanger.conf` — MAC changer config
`/config/lockscreen.conf` — lock screen config (`timeout`, `hash`, `salt`)
`/config/clock.conf` — timezone (`tz=...`)
`/config/notif.conf` — notification settings + per-level audio paths
`/config/notification/*.wav` — shared per-level notification audio (raw WAV: 16-bit PCM, 22050Hz, mono — `playWav()`, NOT MP3), referenced by `/config/notif.conf` (`SD_DIR_CONFIG_NOTIF`)

**`/apps/`** — one self-contained folder per command (see `/apps/README.txt` on-device)
`/apps/README.txt` — auto-generated folder→command map (never overwritten)
`/apps/trackme/session.csv` — trackme session log (`SD_LOG_TRACKME`)
`/apps/trackme/known.csv` — trackme whitelist (`SD_LOG_TRACKME_KNOWN`)
`/apps/trackme/signatures.csv` — custom BLE tracker signatures (`SD_CFG_SIGNATURES`)
`/apps/eviltwin/creds.csv` — captured portal credentials (`ET_LOG_PATH`)
`/apps/eviltwin/portal/*.html` — custom HTML portal pages (`ET_PORTAL_DIR`)
`/apps/hiddenssid/found.csv` — discovered hidden SSIDs (`SD_LOG_HIDDEN_SSIDS`)
`/apps/wpasniff/wordlist.txt` — custom WPA crack wordlist (one password per line, ≥8 chars) — used by `ws` (`SD_CFG_WORDLIST_WS`)
`/apps/wpasniff/<BSSID>.cap` — WPA handshake pcap (`SD_DIR_WPASNIFF`, libpcap linktype 105)
`/apps/wpasniff/cracked.csv` — on-device crack results from `ws` (`SD_LOG_CRACKED_WS`)
`/apps/pmkid/wordlist.txt` — custom WPA crack wordlist for `pm` (`SD_CFG_WORDLIST_PM`)
`/apps/pmkid/<BSSID>.cap` — PMKID capture pcap (`SD_DIR_PMKID`)
`/apps/pmkid/cracked.csv` — on-device crack results from `pm` (`SD_LOG_CRACKED_PM`)
`/apps/karma/<ssid>.cap` — karma rogue-AP half-handshake (beacon+M1+M2, linktype 105) + `cracked.csv`/`creds.csv`/`connects.csv` (auto-mode engagements)/`wordlist.txt`/`NNN.csv` (saved tables) + `portal/*.html` (`SD_DIR_KARMA`)
`/apps/capcrack/cracked.csv` — offline cap-cracker results from `crack`/`cc` (`SD_DIR_CAPCRACK`)
`/apps/wifimon/NNN.cap` — raw 802.11 PCAP files from `wm` (`SD_DIR_WIFIMON`, linktype 105, Wireshark-compatible)
`/apps/wifimon/probes.csv` — directed-probe log from `wm [p]` (`SD_LOG_PROBES`)
`/apps/wguard/NNN.csv` — `001.csv`, `002.csv` … session files (never overwritten; new number on each boot/start)
`/apps/beaconflood/wordlist.txt` — custom SSID list for `bf file` (`SD_CFG_WORDLIST_BCN`)
`/apps/bmon/NNN.csv` — `001.csv`, `002.csv` … BLE advertisement logs (never overwritten; sequential on each start)
`/apps/csidetect/NNN.csv` — CSI motion presence-transition logs (`SD_DIR_CSIDETECT`, sequential, `[s]` in `csi`)
`/apps/netspy/NNN.csv` — client-isolation device recon from `ns` (`SD_DIR_NETSPY`, sequential; `time,mac,ip,name,vendor,type,how,services`) + `gwpasm.txt` (`ns dump`)
`/apps/i2cscan/results.csv` — I2C scanner results (`timestamp,0xADDR,chip_name,type,ACK/DEAD`)
`/apps/fastpair/keys.csv` — saved Fast Pair anti-spoofing keys
`/apps/fastpair/paired.csv` — successful pairings log
`/apps/fastpair/sniff.csv` — passive scan/sniff log
`/apps/espsniff/NNN.csv` + `NNN.pcap` — ESP-NOW capture files
`/apps/bleinfo/<mac>.txt` — BLE GATT enum/sniff/replay saves
`/apps/espchat/contacts.csv` — ESPChat paired contacts (MAC, name, channel, LMK hex)
`/apps/espchat/config.conf` — ESPChat default public channel
`/apps/espchat/pub/chN.log` — public chat logs per channel
`/apps/espchat/prv/<MAC>.log` — private chat logs per contact
`/apps/badusb/scripts/*` — BadUSB DuckyScript files (`SD_DIR_BADUSB_SCRIPTS`)
`/apps/ssh/hosts.csv` — SSH saved host profiles (`SD_SSH_HOSTS`, `name,ip,port,user`); `known_hosts`/`keys/` planned

## WiFi / SD — ESP32-S3 GDMA Rule
**Never write to SD while WiFi is in APSTA or promiscuous mode** — WiFi and SPI share the GDMA controller on ESP32-S3; concurrent DMA corrupts FatFS.
- Open SD files **before** `WiFi.mode(WIFI_MODE_APSTA)` / `esp_wifi_set_promiscuous(true)`
- Do all SD writes **after** `WiFi.softAPdisconnect()` + `WiFi.mode(WIFI_STA)`
- Never use `WiFi.disconnect(true)` — it calls `esp_wifi_stop()` which corrupts GDMA state; use `WiFi.disconnect(false)` instead
- Never use `WiFi.mode(WIFI_OFF)` after attacks — use `WiFi.mode(WIFI_STA)` to leave WiFi initialized but idle
- **For mid-session SD writes while promiscuous is live, use the RAII guard** `ScopedPromiscPause` (`wifi/core/wifi_sd_guard.h`) instead of hand-rolling `esp_wifi_set_promiscuous(false)`/`(true)` pairs: `{ ScopedPromiscPause _; sd.appendLine(...); }`. It reads the *current* promiscuous state in its ctor and restores only what it found — so it's a safe no-op when promiscuous is off, and impossible to forget the resume. Reference use: `EvilTwin::flushCredsToSD()`. Existing modules (wguard `doAutoSave`, wifimon pcap flush, handshake/pmkid) still use the hand-rolled pattern and can migrate incrementally — the guard is opt-in, not a forced refactor.

**CI** (`.github/workflows/build.yml`): compile-gate — builds both `T-Deck` + `T-Deck-Plus` envs on every push/PR to `main`/`feature/pentest-enhancements` (paths-filtered to firmware/lib/platformio.ini). Catches build breaks without hardware; does not test runtime behavior.

**HandshakeCapture** (`handshake_capture.cpp/h`):
- `ws <index|bssid> [ch]` — deauth + EAPOL sniff; stores M1+M2 in RAM (`g_whs`), writes pcap only after WiFi teardown
- Promiscuous filter: `WIFI_PROMIS_FILTER_MASK_DATA` (EAPOL is a data frame)
- On-device crack: PBKDF2(SSID,pass,4096,32) → PRF-512 → KCK → HMAC-SHA1 MIC verify
- Wordlist: `/apps/wpasniff/wordlist.txt` (SD, user choice) or built-in 100 passwords
- Output: `/apps/wpasniff/<BSSID>.cap` (aircrack-ng / hashcat hcxpcapngtool compatible) + `/apps/wpasniff/cracked.csv`

**PmkidAttack** (`pmkid_attack.cpp/h`):
- `pm <index|bssid> [ch]` — passive EAPOL M1 sniff; no deauth, no client needed
- Extracts PMKID KDE from M1 Key Data: `DD 14 00:0F:AC 04 <16B PMKID>`
- On-device crack: PBKDF2(SSID,pass,4096,32) → HMAC-SHA1-128(PMK, "PMK Name"||AP||STA) vs PMKID
- Simpler than ws crack — no PRF-512, just one HMAC-SHA1 truncated to 16 bytes
- Wordlist: `/apps/pmkid/wordlist.txt` (SD, user choice) or built-in 100 passwords
- Output: `/apps/pmkid/<BSSID>.cap` + `/apps/pmkid/cracked.csv` (tagged `,PMKID`)
- Falls back gracefully: if no PMKID in M1 Key Data, shows `M1 seen — no PMKID in Key Data`

**Karma** (`wifi/attacks/karma/karma.cpp` + `rogue_handshake.cpp/.h`) — `karma`/`km`:
- `km` harvest+fingerprint · `km auto` hands-free sweep · `km hs <ssid> [ch]`/`[h]` WPA2 half-handshake · `km portal <ssid>`/`[p]` captive portal · `[s]` save harvest+devices → `/apps/karma/NNN.csv`
- **PNL fingerprinting** (`[v]` DEVS view): union-find clusters randomized MACs into physical devices by shared probed-SSID set — defeats MAC randomization when a device leaks a multi-SSID PNL
- **Rogue-AP half-handshake** (`roguehs` engine): a fully MANUAL injected AP. WE pick the ANonce + inject our own M1 (ESP32 promiscuous can't hear self-TX M1), client auto-joins and replies M2 keyed by the REAL PSK → crackable half-handshake. **Make-or-break:** `begin()` sets the STA MAC then reads it back (`esp_wifi_get_mac`) and uses THAT as the BSSID, so the hardware MAC-layer ACKs the client (a failed set_mac can't silently break it). `rnd`/`REAL` indicator. Live Prb/Ath/Asc/M1/M2 stage counters. **HW-VERIFIED end-to-end** (associate→M2→on-device crack). M1 retransmit schedule (assoc isn't ACKed). One client/session (gotM2 latches).
- `.cap` (beacon+M1+M2, libpcap lt 105) **never overwrites** — `<ssid>.cap`, `<ssid>-1.cap`… `karmaSaveCap` returns the real basename. `[c]` = on-device crack (SD `/apps/karma/wordlist.txt` or built-in 100 picker) → `cracked.csv`; or crack later with `cc`
- **Auto mode** (`km auto`): loops harvest ~30s → bait uncaptured SSIDs (least-recently-baited first → round-robins the whole field, skips captured) ~20s each; M2 → `.cap` + `/apps/karma/connects.csv` (`time,ssid,sta_mac,vendor,type`). Capture-only (crack with `cc`). `[v]` lists captured SSIDs live (paged; bait timer compensated). `[q]` stops.
- MAC randomization via shared `wifi/core/mac_util.h` (`randomLaMac`) — independent of `mc` (AP BSSID vs client identity). GDMA: all SD writes after AP teardown. Portal picker scans `/apps/karma/portal` AND `/apps/eviltwin/portal`.

**CapCrack** (`wifi/tools/capcrack/capcrack.cpp/h`) — `crack`/`cc`:
- Offline WPA/WPA2 cracker for `.cap` files: 4-way handshake (M1+M2) OR PMKID. Works on karma/ws/pm + external captures (classic libpcap, not pcapng).
- `cc [cap] [wordlist|dir]` — paths relative to `cd` cwd (pass bare filenames after `cd`). cap/wordlist each accept a file, a directory (pick a cap / run every `*.txt`), or omitted (picker). Built-in 100 list always tried last. `COMP_ANY` autocomplete (files + dirs).
- Needs an ESSID (beacon/probe-resp) in the cap to derive the PMK; errors `noESSID`/`noM1`/`noM2`. Results → `/apps/capcrack/cracked.csv`. Reuses `wpa_crack` + `dot11` + `pcap` reader (added to `pcap_writer.h`). SD-only, no WiFi → no GDMA concern.

**Wardrive** (`wifi/tools/wardrive/wardrive.cpp/h`) — `wardrive`/`wd` — **T-Deck Plus only** (`#ifdef BOARD_TDECK_PLUS`, base board prints a notice like the `gps` command):
- **synchronous** WiFi scan (`WiFi.scanNetworks(false,true)`) in STA mode + `GpsManager` fix → **WiGLE WiFi-1.4** CSV. Auto-starts the GPS task if not running; leaves it running on exit.
- **CRITICAL — Phase 1 waits for the first GPS fix with the radio IDLE (no scanning), THEN Phase 2 scans+logs.** GpsManager does a one-time NVS *flash* write on first fix (`saveGpsFixFlag`); a flash write while a WiFi scan is in flight corrupts the scan engine (every later scan returns 0 — the "90 APs, then 0 after fix" bug). Scanning before the fix is also pointless (can't geotag). `_fixSaved` is latched so losing/regaining a fix mid-drive never re-triggers the write. UI: `GPS searching` → `FIX … LOGGING`. Cold fix ~4 min outdoors.
- **sync, not async — adopted Bruce's method** (`BruceDevices/firmware` `src/modules/gps/wardriving.cpp`). A blocking `WiFi.scanNetworks()` per sweep can't get stuck in a half-started async state, so there is **no scan state machine / watchdog / `esp_wifi_scan_stop`** — the reliability is from not using async. (History: async was tried for a live UI but produced a recurring "scans APs but logs nothing" class of bug — a failed async *start* read back as `WIFI_SCAN_FAILED(-2)` and mistaken for a finished empty sweep; sync removes that entire surface.) Cost: UI frozen for the ~3-4s blocking scan — fine for wardriving; shown as "Scanning..." so it doesn't look hung, and `[q]` is honoured in the ~1s pace window between sweeps. `vTaskDelay(1)` every 32 rows (WDT yield) + `vTaskDelay(120)` after `scanDelete()` before SD I/O (both from Bruce). `n<0` (scan fail) is a self-limiting empty sweep — just retried next loop.
- **Lazy file** — `/apps/wardrive/NNN.csv` is created on the **first AP actually logged** (`ensureFile()`), so no-fix / quick-quit sessions leave **no empty CSV**. Sequential, never overwritten. Header = WiGLE pre-header + `MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type`. MAC lowercase, FirstSeen=GPS UTC, **AltitudeMeters = real `GpsManager::altitude()`** (metres MSL, `0` until module reports it), **AccuracyMeters = HDOP** (`GpsManager::hdop()`, Bruce's ×1 convention), falling back to the sats heuristic (8 m if ≥6 sats else 20 m) when HDOP is 0. **Verified against the official spec** (api.wigle.net/csvFormat-1_4.html). Validated against Bruce + dkyazzentwatwa/esp32-gps-wifi-wigle references.
- **Dedup**: one row per BSSID per session via a `WD_MAX_BSSIDS=1024` table in **PSRAM** (`ps_malloc`, freed on exit — rule 5c). SSID commas/CRLF stripped (RFC4180-safe).
- GDMA: rows staged in a RAM `std::vector<String>` during result processing, flushed to SD only **after `WiFi.scanDelete()`** (radio idle), before the next scan. `logged`/`lastWrote` count only **successful** `appendLine()` returns (a failed write no longer inflates the count). On-screen `Heap NNk min NNk` diagnostic (free-heap leak watch) + per-sweep `sweep n=.. fix=.. new=.. wr=..` gate trace (which logging gate failed). Lock-unlock redraw via `consumeJustUnlocked()`. `[q]` quits.

**MACChanger** (`mac_changer.cpp/h`):
- `applyIfEnabled()` only called in `scanWiFiNetworks()` and `connectToWiFiCommand()` — the two places where T-Rex's own MAC appears on the network
- Never call in monitor/deauth/ws/hs — injected frames use spoofed SA, passive sniff doesn't transmit
- Config: `/macchanger.conf` · Subcommands: `on|off|random|set <mac>|restore on|off|target wifi|bt|both|status`

## Coding Rules
- New commands: one-liner in `setupCommands()`, assign a category
- No `Serial.println` — especially no passwords/credentials
- No `delay()` in scan loops — use `millis()`
- All display output via `displayManager`
- Poll `inputHandler.getKeyboardInput()` for `q` in every blocking loop
- New modules: own `.cpp/.h` pair
- Command buffer 128 bytes — keep syntax compact
- SD + WiFi: follow the GDMA rule above — open files before WiFi, close after teardown

**I2cScan (`i2cscan.cpp/h`)** — `diagnostics/i2cscan/` [EXPERIMENTAL]:
- `i2cscan` / `isc` — interactive I2C bus scanner (0x08–0x77), 35-entry chip table
- T-Deck Plus built-ins: 0x18 ES8311 · 0x40 ES7210 · 0x55 keyboard · 0x34 AXP2101 · 0x5D GT911 trackpad
- Tagging: `[BUILTIN]` grey for known internal chips; `[EXT]` green for unknown (Grove/external)
- Type color-coding: input=cyan, audio=orange, power=yellow, sensor=green, disp=magenta, rfid=red, accel=sky
- Detail pane: auto-reads on row change; two-path — reg-ptr write (0x00) + `requestFrom`, fallback to raw stream (GT911 needs this — 16-bit regs return 0x00 for pointer path)
- Register browser `[r]`/CLICK: 16 pages × 16 bytes, trackpad page, `[w]` write + 10ms re-read, `[RAW]` tag when reg-ptr unsupported
- Subcommands: `isc r <addr> <reg> [len]` · `isc raw <addr> [len]` · `isc w <addr> <reg> <val>` · `isc d <addr>` (256-byte hex dump)
- SD save: `[s]` → `/apps/i2cscan/results.csv` (`FILE_APPEND` only — never truncates)
- Interactive: `[v]` verify all · `[f]` rescan · `[p]` re-probe selected · `[q]` quit

**BleAdvMonitor (`bmon.cpp/h`)**:
- `bmon` / `bm` — passive BLE advertisement sniffer
- Decodes: iBeacon (Apple MFR 0x004C + type 0x02 + len 0x15 → UUID+Major+Minor+TxPow), Eddystone-UID/URL/TLM (service UUID 0xFEAA), cleartext device names, unknown MFR (shows company ID + first 4 bytes)
- NimBLE passive scan (`setActiveScan(false)`), continuous (`start(0)`), duplicates enabled for live RSSI updates
- Ring buffer (32 entries, BT task → main task) → 64-entry device table sorted newest-first
- `[s]` toggle SD logging → `/apps/bmon/NNN.csv` (sequential, never overwrite); dedup 60s per MAC
- Log columns: `timestamp,first_seen,mac,addr_type,type,rssi,sightings,info,extended`
- `info` = truncated screen string; `extended` = full decoded data (full iBeacon UUID, full Eddystone NS/instance, TLM adv_count+uptime, full MFR hex)
- `addr_type`: `pub` (public MAC) or `rnd` (random MAC); timestamps from ClockManager (GPS/NTP), fall back to `@NNNms`
- **UI layout** (fixed pixel grid, `outputY + n × LINE_HEIGHT`): row 0 = header, row 1 = column headers (TYPE/MAC/AT/RSSI/INFO), row 2 = separator, rows 3-9 = 7 data rows, row 10 = separator, rows 11-12 = extended detail pane (2 lines) for selected device, row 13 = footer
- **Row selection**: trackpad UP/DOWN (`TBALL_UP`/`TBALL_DOWN`) moves selection within page; selected row gets dark-blue `fillRect` highlight + `>` marker + yellow info text; detail pane auto-updates
- **Column layout** (6px/char): CX_SEL=4, CX_TYPE=16, CX_MAC=52, CX_AT=160, CX_RSSI=184, CX_INFO=214
- `[a/l]` page navigation (7 rows/page, resets selection to row 0) · `[q]` quit → stops scan + closes log

**CSIDetect (`csidetect.cpp/h`)** — `wifi/sensing/`, command `csidetect`/`csi [auto]` (Diagnostics):
- WiFi **CSI motion detector** with a radar UI (first-of-kind on T-Deck). Ports the single-device CSI path of skizzophrenic/Cardputer-CSI-Human-Detector (MIT); algorithm upgrades from ESPectre (GPL, methodology only) — see `NOTICES` #12/#13/#14. Reads CSI from frames via promiscuous + `esp_wifi_set_csi`. Bails if `wg bg` owns promiscuous; no SD, no notifications.
- **Two source modes:** `csi` = connected link (needs `cw`, cleaner/faster signal, all frames on the AP channel). `csi auto` = **passive, no association** — `csiAutoScout()` does a `WiFi.scanNetworks()`, picks the strongest AP, parks on its channel (`esp_wifi_set_channel`) and **single-source MAC-locks** to it (`gLockActive`/`gLockMac`, filtered in `csiCb` on `info->mac`). The MAC lock is essential: blending CSI from multiple transmitters reads as motion even when still.
- **Algorithm** (IRAM `csiCb`): per-subcarrier amplitude `sqrt(r²+im²)` + mean sin(phase). **NBVI auto subcarrier weighting** (`gScMean/gScVar` per-subcarrier EMA → weight `var/mean²`, capped) emphasises motion-responsive subcarriers vs a plain average — warms up after `CSI_WINDOW*2` frames, falls back to uniform mean, toggle live with `[n]` (`gNbviOn`). Global windowed variance + **asymmetric-EMA** self-cal → `gMotion`; **Hampel outlier filter** (`csiHampel`, 7-sample median+MAD) on the motion stream in the main loop kills lone glitches before the threshold/hold-coast (thresh default 0.15). Also `CSI_BANDS=8` responsive-EMA bands → per-sector "contacts".
- **`wifi_csi_config_t` = IDF-4.4 fields** (lltf_en/htltf_en/…) — correct for `platform = espressif32` 6.x; renames in IDF 5.2+. (Espressif's `esp_wifi_sensing`/`esp-radar` need IDF ≥5.4 → unusable here; that's why the methods are hand-ported.)
- **Adaptive threshold** (`[t]`, `gAdaptive`): trip level = learned quiet-room noise floor (`gNoiseEMA`, updated only while not-present) + `gMargin`; auto-raises the bar when the source is noisy (helps `csi auto`). Manual `a/l` nudge `thresh` when off, `gMargin` when on. Panel shows `THR auto`.
- **SD logging** (`[s]`, GDMA-safe): presence transitions (CLEAR↔CONTACT edges only — compact) → `/apps/csidetect/NNN.csv` (sequential, never overwritten; `SD_DIR_CSIDETECT`). Columns `time,event,motion_pct,thresh_pct,zones,mode,channel,bssid,ssid` (full BSSID + SSID of the sensed AP, both modes; `time` = ClockManager timestamp or `@<ms>` fallback, bmon convention). Every SD touch (`csiOpenLog`/`csiLogEvent`) wrapped in `ScopedPromiscPause` — CSI keeps promiscuous live, so the write must pause it (GDMA rule). Panel shows `L<n>` next to `fr:`.
- **UI**: double-buffered PSRAM `LGFX_Sprite` radar (≈30 fps) — sweep cone, sector blips (gated behind real motion), pulsing CLEAR/CONTACT reticle; right panel = CONTACT/CLEAR + activity + zones(+`NBVI`) + MOTION/THRESH bars + **`AUTO/LINK c<ch> <ssid>` source line** (channel + AP name, truncated; full BSSID/SSID in CSV) + `fr:`/`L<n>`/`CSI live` diag. `[h]` help. `a/l`/trackball=sens, `c`=recal, `t`=adaptive, `n`=NBVI, `s`=SD log, `q`=quit.
- **HONESTY (load-bearing)**: single antenna = ONE motion-energy signal — **no bearing, no count, no localization**, in BOTH modes. Auto mode improves *robustness* (any AP, no join) not precision; NBVI/Hampel improve *signal quality / false-alarm rate*, still not direction. The 8 sectors are signal bands, NOT directions. Direction needs a multi-antenna array or multi-node mesh (confirmed against 2025-26 state of the art). Never reintroduce fake per-person positions.

## Pending Features
- LoRa scanner / packet logger
- wguard: Karma detection needs real-world testing (probe-response sniff for 3+ SSIDs/60s from same BSSID)
- espvoice: private/encrypted 1:1 mode (currently broadcast only); optional voice-activity TX gate
