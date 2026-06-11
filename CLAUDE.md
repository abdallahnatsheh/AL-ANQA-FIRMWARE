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
- Dispatch uses `Utils::matchesCmd(cmd, prefix)` — requires space or NUL after prefix (not bare `startsWith`). Critical: prevents `sdrm` matching `sdr`.
- `CompType`: `COMP_NONE` (no file args) · `COMP_ANY` (ls) · `COMP_DIR` (cd) · `COMP_FILE` (cat/srm/ux)
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
- Lock triggers: `lock` command (immediate) · hold trackpad center 3 s (GPIO0 LOW; `clearPendingClicks()` called before `lock()` to suppress stale TBALL_CLICK)
- Idle auto-lock: `_timeout` seconds of no `getKeyboardInput()` activity; 0 = disabled
- No-password mode: Space ×3 to unlock (shows `(1/3)` / `(2/3)` progress on instruction line); all trackball events are swallowed while locked
- Recovery: remove SD card + reboot → `loadConfig()` returns false → `_hasPassword = false` → no PIN → Space ×3 unlocks. Hot-pulling SD while already locked does NOT bypass the PIN (hash stays in RAM until reboot) — intentional: prevents attacker from yanking SD to bypass lock
- PIN mode: type PIN → Enter to confirm; wrong-PIN → 1.5 s red flash cooldown; Esc → back to dormant; up to 16 chars, any printable keyboard character
- PIN stored as SHA-256(saltHex + pin) using mbedTLS context API; 8-byte random salt via `esp_random()`
- Config: `/lockscreen.conf` (key=value: `timeout`, `hash`, `salt`). `saveConfig()` returns `bool` — all cmd functions check it and print yellow "No SD — active this session only" warning on false
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
System: `help/hlp` `info/inf` `clear/clr` `MATRIX/matrix` `pwrsave/psv` `lock/lk`
WiFi: `scanwifi/sw` `connectwifi/cw` `wifipass/wp` `wifiexport/wex` `clearwifi/clrw` `wifimon/wm` `deauth/da` `eviltwin/et` `hiddenssid/hs` `macchanger/mc` `wpasniff/ws` `pmkid/pm` `wguard/wg` `beaconflood/bf` `espsniff/es` `esptest/est` `espchat/ec`
Network: `netdiscover/nd` `portscan/ps` `topscan/ts` `ping/pg`
Bluetooth: `scanblue/sbl` `bleinfo/bi` `trackme/tm [silent]`
SD: `sdinfo/sdi` `sdls/ls` `cd/cd` `cat/cat` `sdrm/srm` `sdf/sdf`
Diagnostics: `gps/gps` `spktest/st` `loratest/lt` `i2cscan/isc [EXP]`

**ESPChat** (`espchat/ec`, `espsniff/es`, `esptest/est`) — `radio/espnow/espchat/`, `espsniff/`, `esptest/`:
- Wire format: `EcMsg{type(1)+seq(1)+name[12]+text[100]}` = 114 bytes, type=0x01; broadcast ch compatible with any ESP32/ESP8266
- Public chat: `WIFI_STA` + broadcast peer (FF:FF:FF:FF:FF:FF) unencrypted
- Private chat: ESP-NOW CCMP AES-128, LMK = SHA-256(PIN + sorted(mac_A, mac_B))[:16]
- Pairing: initiator derives LMK immediately, adds encrypted peer; receiver sends "* pair ok" encrypted; initiator replies "* pin ack" encrypted — wrong PIN = frame dropped; 3 attempts, fail = `ecRemoveContact()`
- Background (`ec bg`): `pollEspchatBg()` hooked in `getKeyboardInput()`; `EC` badge in status bar; public=PING, private=INFO notification
- Contacts: SD → `/apps/espchat/contacts.csv`; no SD → `g_ecContacts[]` RAM only, cleared on reboot
- UI layout: PAIR_Y(y=66) · MSG_Y0(y=80) · EC_VIS=7 rows · SEP2_Y(y=178) · FOOT_Y(y=192) · INPUT_Y(y=206); 4px scroll slider at x=316
- All WiFi commands call `stopEspchatBg()` before starting to avoid ESP-NOW/WiFi mode conflicts

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
`/apps/wifimon/NNN.cap` — raw 802.11 PCAP files from `wm` (`SD_DIR_WIFIMON`, linktype 105, Wireshark-compatible)
`/apps/wifimon/probes.csv` — directed-probe log from `wm [p]` (`SD_LOG_PROBES`)
`/apps/wguard/NNN.csv` — `001.csv`, `002.csv` … session files (never overwritten; new number on each boot/start)
`/apps/beaconflood/wordlist.txt` — custom SSID list for `bf file` (`SD_CFG_WORDLIST_BCN`)
`/apps/bmon/NNN.csv` — `001.csv`, `002.csv` … BLE advertisement logs (never overwritten; sequential on each start)
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

## Pending Features
- LoRa scanner / packet logger
- macwatch — MAC watchlist with proximity alert
- espvoice — ESP-NOW voice over I2S (requires ES7210 mic on T-Deck Plus)
- wguard: Karma detection needs real-world testing (probe-response sniff for 3+ SSIDs/60s from same BSSID)
