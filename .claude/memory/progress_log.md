---
name: Progress Log
description: What is implemented vs what is not yet started
type: project
---

## Session 2026-05-10
- MAC changer bug fix #1 — removed `esp_wifi_set_mode(WIFI_MODE_NULL)` trick that was corrupting scan subsystem (scan returned 0 results after any `mc` command)
- MAC changer bug fix #2 — re-added `applyIfEnabled()` in `connectToWiFiCommand()` after `WiFi.mode(WIFI_STA)` because `WiFi.disconnect(true)` deinits driver and wipes custom MAC
- Banner grabber overhaul — protocol-aware probing, 120ms animated spinner inlined in read loop, HTTP Server header extraction (384-byte buffer, `\nServer:` line search)
- OS fingerprinting — TTL via lwip raw ICMP (`raw_pcb`, `ip4_current_header()`) + SSH/HTTP banner analysis + port-presence fallback (RDP/SMB/MSRPC → Windows)
- Power save screen-off tier — 5 min inactivity → brightness 0, keypress wakes to 128; `set screenoff <s>` + `set screenoffmode on|off`
- README + project memory updated; `feature/wpa-man-helpfix` merged into `feature/pentest-enhancements`

## Done
- Bug fixes (all 8), keyboard polling fix, command buffer 128b
- SD card manager, WiFi monitor, deauth attack, FreeRTOS task infra
- Evil twin + captive portal, power save manager, GPS manager (Plus only)
- TrackMe anti-tracking scanner, splash screen, hidden SSID reveal
- ARP scan, parallel port scan, top-31 common ports scan, ICMP ping
- BLE scanner, cyberpunk UI headers, esp info pages, LoRa test
- UI overhaul complete (LINE_HEIGHT=14, perPage=10, font 1.0× — current state is final)
- Banner grabber (b key in port scan) — protocol-aware probing (HTTP active, TLS detect, MySQL binary, Redis PING), animated spinner (120ms update), HTTP Server header extraction, 55-entry service name table
- OS fingerprinting in port scan — TTL via lwip raw ICMP (≤64=Linux/macOS, ≤128=Windows), SSH/HTTP banner analysis, port-presence fallback (RDP 3389 / SMB 445 / MSRPC 135 → Windows)
- Power save screen-off tier — 5 min inactivity → brightness 0; keypress restores to 128. New psv subcommands: `set screenoff <sec>`, `set screenoffmode on|off`
- MAC changer (`macchanger/mc`) — randomize or set STA MAC; hooks into scan/connect/deauth/wpasniff; fixed scan-empty regression (removed mode-reset trick); fixed connect not spoofing (re-apply after WiFi.disconnect deinit)
- WPA2 handshake capture + on-device crack (`wpasniff/ws`, then `c`) — EAPOL sniff → pcap, deauth every 4s, `c` cracks with SD wordlist (unlimited) or built-in top-100; mbedtls PBKDF2+PRF512+HMAC-SHA1; saves to `/logs/cracked.csv`
- MAC changer hooked into deauth + handshake capture (applyIfEnabled before softAP)
- `getNetworkSSID()` added to WiFiFunctions
- Help command sub-pagination (5 cmds/sub-page) — fixes WiFi category overflow (9 cmds → 2 sub-pages)
- Man pages (`man/mn <cmd>`) — all 29 commands; SYNTAX/ABOUT/STEPS/KEYS/NOTE/FILES/WARNING labels in grey; l/a browse adjacent pages; short name lookup works

## Session 2026-05-11
- `show/sh <wifi|ble|hosts>` — re-display last scan without rescanning; reuses static render functions (renderScanPage / renderBlePage / renderArpPage) in same .cpp; "No scan data" guard
- Trackpad cursor — GPIO edge detection on BOARD_TBOX_G02/G01/G04/G03 + BOARD_BOOT_PIN; `_cursorPos` tracking in CommandManager; insert-at-cursor with buffer shift; `redrawCommandLine()` in DisplayManager with inverted-color cursor char; `_cmdLineY` tracks scrolling prompt Y; click fires only on falling edge to prevent double-fire; `_tballLast[]` initialized from actual pin state in `begin()` to prevent false startup triggers
- README: System features table, Commands table, Roadmap, Navigation section all updated
- Memory files updated

## Session 2026-05-11 (continued)
- WPS flag in `scanwifi` — reads `wifi_ap_record_t.wps` bit via `WiFi.getScanInfoByIndex(i)` cast; shows cyan ` WPS` tag after auth in scan table; zero extra scan overhead

## Session 2026-05-12
- `wifipass/wp` command — paginated saved credentials viewer; SD (wpa_supplicant.conf) with NVS fallback; shows SSID, plain password, `[open]`, `[hex-psk]`, hidden `~`, BSSID/priority sub-line; `l/a/q` navigation
- Full Linux wpa_supplicant.conf bidirectional compatibility — new `wifi_creds` module (wifi_creds.h/cpp): `WifiNetwork` struct, `parseWpaSupplicant()`, `appendWpaNetwork()`, `getWifiNetwork()`
- Parser handles all Linux formats: `psk="plain"`, `#psk="plain"` (wpa_passphrase recovery), `psk=hexhash` (marked unusable), `key_mgmt=NONE`, `scan_ssid=1`, `priority=`, global fields silently skipped
- Auto-save credentials to SD on successful connect (after `WL_CONNECTED` only — wrong password never saved); backup-before-modify (`wpa_supplicant.bak`)
- `connectwifi/cw` extended to accept SSID name in addition to scan index — enables hidden + offline network connections
- Self-healing upgrade: when Linux `update_config=1` strips `#psk=` comment leaving hex-only entry, T-Rex prompts user once → appends plain entry → parser prefers plain over hashed on next read
- `SD_LAYOUT.md` created — documents all SD files, auto-created vs manual, Linux sync workflow, limitations (hex SSID unsupported, EAP unsupported, update_config=1 caveat), desktop Linux migration steps (nmcli + wpa_passphrase)
- Bugs fixed: NVS v5 iterator leak, empty SSID silent timeout on hidden index connect, duplicate entries from Linux-imported files (BSSID-only dedup → SSID-primary dedup), SD_LAYOUT.md false claim about update_config=1 preserving comments

## Session 2026-05-18
- `cd` command + CWD in SDCardManager — `resolvePath()` normalizes `..`, absolute/relative
- `ls` rewritten — non-recursive, shows CWD at top, dirs in cyan with `/`, files with size
- `sdr`, `srm`, `ux` resolve relative paths via `resolvePath()` before calling their functions
- Command history — 16-entry ring buffer in CommandManager; trackpad UP = older, DOWN = newer; saves current input before navigating, restores on DOWN past newest

## Session 2026-05-25
- Lock screen write-lock — `DisplayManager::_blocked` flag; all draw methods guard `if (_blocked) return`; `setBlocked(true)` after `lock()`; lock screen draw functions temporarily lift block to draw; status bar also refreshed inside `refreshDuration()` while block is lifted
- Auto-redraw on unlock — `LockScreenManager::_justUnlocked` + `consumeJustUnlocked()`; all interactive apps (sw, sbl, nd, ps, ts, man, wguard, cat, ls, bf, tm, hs) check flag each loop iteration and re-render; `clearRedrawCallback()` approach replaced by `_justUnlocked` pattern
- Backspace hold-repeat in CLI — 700ms delay, 80ms rate, backspace only; any other key cancels; fields `_repeatKey`, `_repeatStart`, `_repeatLast` in `InputHandling`; `kReleaseTimeoutMs` removed (was causing repeat to never fire)
- WiFi wrong password fix — `storeWiFiCredentials()` moved from before-connect to success path only; wrong password is never saved to NVS
- Plain text password entry — `readPassword()` now echoes `c` directly instead of `*`; space restriction removed
- Clock sync after WiFi connect — NTP start/stop check moved ABOVE 10s throttle in `ClockManager::update()`; fires immediately on first poll after `WL_CONNECTED`
- Status bar 3s live refresh — `_lastBarMs` timer in `ClockManager::update()`; calls `updateStatusBar()` every 3s when not blocked; keeps clock/WiFi/battery/GPS live during idle
- Status bar while locked — `refreshDuration()` (1 Hz) calls `updateStatusBar()` with block temporarily lifted; keeps clock in status bar fresh while screen is locked
- WGuard WiFi isolation — `WiFi.disconnect(false)` before `WiFi.mode(WIFI_STA)` in both `run()` and `beginBackground()`; ensures promiscuous-only mode, no lingering STA association
- ClockManager — GPS-backed UTC wall clock with NTP priority; TZ from `/clock.conf`; `getShortTime/getTimeStr/getDateStr/getTimestamp`; status bar HH:MM at x=175; lock screen UTC line in `refreshDuration()`; timestamps in trackme/eviltwin/handshake/wguard logs
- BeaconFlood (`bf/beaconflood`) — raw 802.11 beacon injection; list/seq/file modes; random LA-MAC; channel hop 1→6→11…
- WGuard enrichments — DFIR log enrichment, threat counter split, OUI vendor lookup
- `ls/cd` CWD, command history ring buffer, autocomplete arg hints
- `sdrm` renamed → `rm/rm`

## Not Yet Built
- macwatch / mw — MAC proximity watchlist
- LoRa scanner (`lorascan/ls`)
- Touchscreen, microphone
---
name: buddy — implemented summary
---
`bd [name]` — NimBLE NUS GATT server, Claude Desktop BLE remote. y=approve once, n=deny, q=quit.
Security: `setSecurityAuth/IOCap` must be called BEFORE `pSvc->start()` — crashes NimBLE 1.4.x if after.
deinit(true) on quit + SD.begin(39) to restore SPI. NOTIF_PING fires on new popup arrival.
