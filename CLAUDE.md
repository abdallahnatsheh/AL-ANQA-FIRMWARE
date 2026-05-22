# T-DECK-CLI — Claude Project Context

## Project
Pentesting firmware for LilyGo T-DECK / T-DECK Plus (ESP32-S3). PlatformIO + Arduino. All source in `t-deck-cli/`. Build: `env:T-Deck` / `env:T-Deck-Plus` (GPS + speaker). Screen: 320×240, status bar 30px, `outputY=38`.

## Hardware (T-Deck Plus extras in parentheses)
- Display ST7789 · Keyboard PS/2 I2C 0x55 · Battery ADC GPIO4 · SD CS=39
- Audio I2S: BCK=7, WS=5, DOUT=6 — **must use i2s_driver_install(); tone() fails**
- GPS (Plus): RX=44, TX=43, 9600 baud — L76K or u-blox M10Q, ~4 min cold fix
- Power: GPIO10 must be HIGH

## Architecture

**Command system** (`command_manager.cpp/h`): `registerCommand(name, shortName, fn, desc, hasArgs, category, compType=COMP_NONE)` — max 64, all one-liners in `setupCommands()`. Categories: System · WiFi · Network · Bluetooth · SD Card · Diagnostics.
- Dispatch uses `Utils::matchesCmd(cmd, prefix)` — requires space or NUL after prefix (not bare `startsWith`). Critical: prevents `sdrm` matching `sdr`.
- `CompType`: `COMP_NONE` (no file args) · `COMP_ANY` (ls) · `COMP_DIR` (cd) · `COMP_FILE` (sdr/srm/ux)
- History: 16-entry ring buffer in `_hist`; trackpad UP/DOWN navigates; `_histSaved` preserves in-progress line
- Autocomplete: `'` key (Sym+K = 0x27, defined `KEY_AUTOCOMPLETE` in `input_handling.h`); fills common prefix, single match adds space, multiple lists up to 8

**Display** (`display_manager.cpp/h`): all output via `displayManager` — never `tft` directly. `clearScreen()` = below header only. `tdeck_begin()` = full reset.

**Input** (`input_handling.cpp/h`): `getKeyboardInput()` → `char` or `0`. Every blocking loop must poll for `q`.

**GpsManager** (`gps_manager.cpp/h`) — T-Deck Plus only, singleton:
- FreeRTOS task core 0, 30 ms poll, volatile primitives (no mutex needed on ARM32)
- Init order: `begin(9600)` → `initL76K()` ×3 (≈4.5s M10Q boot window) → `begin(38400)` + `recoverUblox()` → `updateBaudRate(9600)` + `recoverUblox()` — must mirror test_gps.cpp exactly
- Status bar: grey=off, yellow=searching, green=fixed

**TrackMe** (`trackme.cpp/h`):
- Gate1=signature · Gate2=score(max 100) · Gate3=GPS≥200m OR time≥5min — WARNING/ALERT need Gate3
- BLE scan: always clears callback before each scan (`setAdvertisedDeviceCallbacks(nullptr)`) — prevents dangling-ptr crash from `scanblue` reusing the singleton
- WiFi probe sniff: Plus only (`#ifdef BOARD_TDECK_PLUS`)
- Whitelist: `/logs/trackme_known.csv` · Signatures: `/signatures.csv`

**PowerSaveManager** (`powersave_manager.cpp/h`) — singleton:
- Hooked into `getKeyboardInput()` — `update()` every poll, `updateActivity()` on keypress — works globally, no per-command changes needed
- Inactivity dim + battery-aware dim (force dim below threshold)
- `init()` calls `tft.setBrightness()` directly · SD config: `/pwrsave.json` (key=value)

**EvilTwin** (`eviltwin.cpp/h`):
- OPEN → clone exact MAC + channel; WPA2 → random LA-MAC (`(x & 0xFE) | 0x02`)
- Deauth pauses automatically when portal clients connected
- Templates: built-in or `/evilportal/*.html` · Logs: `/logs/eviltwin.csv`
- `_creds[20]` in-memory · `[c]` shows table (portal keeps running) · `[s]` saves to SD
- `handleRedirect()`: 302 + `Captive-Portal-URL` header + HTML meta-refresh body — empty body was breaking iOS/Windows

**HiddenSSID** (`hidden_ssid.cpp/h`):
- Deauth burst every 3s + promiscuous sniff for subtype 5 (probe response) or 0 (assoc request) matching target BSSID
- `snifferCb` + `WiFiMonitor::extractSSID()` both `IRAM_ATTR` (callback chain cache safety)
- On found: stop sniff immediately, I2S two-tone beep (unless `silent`), save `BSSID,SSID,ch` → `/logs/hidden_ssids.csv`
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
- Rate limits: BCAST DEAUTH / DEAUTH storm / HANDSHAKE harvest all throttled to once per 30s per source MAC via `lastFired` field in `WgCounter`. Notification sound throttle: WARNING ≤1/10s, ALERT ≤1/5s via `notifyThrottled()`.
- Session files: `/logs/wguard/NNN.csv` — scans SD on init to find next free number (never overwrites). Columns: `time,severity,rssi_dbm,message`. Timestamps are session-relative (`e.ts - _sessionStartMs`). Each save block writes only new events via `_savedEvCount` tracker — no duplicates. Save types: AUTO-SAVE (ring full, clears ring + resets `_savedEvCount`) · CHECKPOINT (every 2 min, skipped if nothing new) · MANUAL ([s] key, footer shows `Saved N events` / `Nothing new to save` for 2.5s) · FINAL (session end).
- GDMA: all SD writes pause promiscuous (`s_active=false`), write, resume.
- Background: `pollBackground()` drains ring, triggers saves, shows popup bar + shield icon in status bar.

## Commands
System: `help/hlp` `info/inf` `clear/clr` `MATRIX/matrix` `pwrsave/psv`
WiFi: `scanwifi/sw` `connectwifi/cw` `wifipass/wp` `wifiexport/wex` `clearwifi/clrw` `wifimon/wm` `deauth/da` `eviltwin/et` `hiddenssid/hs` `macchanger/mc` `wpasniff/ws` `wguard/wg` `beaconflood/bf`
Network: `netdiscover/nd` `portscan/ps` `topscan/ts` `ping/pg`
Bluetooth: `scanblue/sbl` `bleinfo/bi` `trackme/tm [silent]`
SD: `sdinfo/sdi` `sdls/ls` `cd/cd` `sdread/sdr` `sdrm/srm` `sdf/sdf`
Diagnostics: `gpson/gon` `gpsoff/gof` `gpstest/gt` `spktest/st` `loratest/lt`

## SD Layout
`/wpa_supplicant.conf` — saved WiFi credentials (Linux-compatible key=value)
`/wpa_supplicant.bak` — auto-backup before first T-Rex modification
`/wordlist.txt` — custom WPA crack wordlist (one password per line, ≥8 chars)
`/pwrsave.conf` — power save config (key=value, NOT JSON)
`/macchanger.conf` — MAC changer config (key=value)
`/logs/` — eviltwin.csv · trackme.csv · trackme_known.csv · hidden_ssids.csv · cracked.csv
`/logs/wguard/` — `001.csv`, `002.csv` … session files (never overwritten; new number on each boot/start)
`/logs/hs/` — WPA handshake pcap files (`<BSSID>.cap`, libpcap format, linktype 105)
`/evilportal/` — custom HTML portal pages
`/signatures.csv` — custom BLE tracker signatures

## WiFi / SD — ESP32-S3 GDMA Rule
**Never write to SD while WiFi is in APSTA or promiscuous mode** — WiFi and SPI share the GDMA controller on ESP32-S3; concurrent DMA corrupts FatFS.
- Open SD files **before** `WiFi.mode(WIFI_MODE_APSTA)` / `esp_wifi_set_promiscuous(true)`
- Do all SD writes **after** `WiFi.softAPdisconnect()` + `WiFi.mode(WIFI_STA)`
- Never use `WiFi.disconnect(true)` — it calls `esp_wifi_stop()` which corrupts GDMA state; use `WiFi.disconnect(false)` instead
- Never use `WiFi.mode(WIFI_OFF)` after attacks — use `WiFi.mode(WIFI_STA)` to leave WiFi initialized but idle

**HandshakeCapture** (`handshake_capture.cpp/h`):
- `ws <index|bssid> [ch]` — deauth + EAPOL sniff; stores M1+M2 in RAM (`g_whs`), writes pcap only after WiFi teardown
- Promiscuous filter: `WIFI_PROMIS_FILTER_MASK_DATA` (EAPOL is a data frame)
- On-device crack: PBKDF2(SSID,pass,4096,32) → PRF-512 → KCK → HMAC-SHA1 MIC verify
- Wordlist: `/wordlist.txt` (SD, user choice) or built-in 100 passwords
- Output: `/logs/hs/<BSSID>.cap` (aircrack-ng / hashcat hcxpcapngtool compatible) + `/logs/cracked.csv`

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

## Pending Features
- LoRa scanner / packet logger
- bmon — passive BLE advertisement sniffer (iBeacon/Eddystone/cleartext detector, PCAP linktype 251)
- macwatch — MAC watchlist with proximity alert
- wguard: Karma detection needs real-world testing (probe-response sniff for 3+ SSIDs/60s from same BSSID)
