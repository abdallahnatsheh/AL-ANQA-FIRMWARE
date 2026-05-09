# T-DECK-CLI — Claude Project Context

## Project
Pentesting firmware for LilyGo T-DECK / T-DECK Plus (ESP32-S3). PlatformIO + Arduino. All source in `t-deck-cli/`. Build: `env:T-Deck` / `env:T-Deck-Plus` (GPS + speaker). Screen: 320×240, status bar 30px, `outputY=38`.

## Hardware (T-Deck Plus extras in parentheses)
- Display ST7789 · Keyboard PS/2 I2C 0x55 · Battery ADC GPIO4 · SD CS=39
- Audio I2S: BCK=7, WS=5, DOUT=6 — **must use i2s_driver_install(); tone() fails**
- GPS (Plus): RX=44, TX=43, 9600 baud — L76K or u-blox M10Q, ~4 min cold fix
- Power: GPIO10 must be HIGH

## Architecture

**Command system** (`command_manager.cpp/h`): `registerCommand(name, shortName, fn, desc, hasArgs, category)` — max 64, all one-liners in `setupCommands()`. Categories: System · WiFi · Network · Bluetooth · SD Card · Diagnostics.

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

## Commands
System: `help/hlp` `info/inf` `clear/clr` `MATRIX/matrix` `pwrsave/psv`
WiFi: `scanwifi/sw` `connectwifi/cw` `clearwifi/clrw` `wifimon/wm` `deauth/da` `eviltwin/et` `hiddenssid/hs`
Network: `netdiscover/nd` `portscan/ps` `topscan/ts` `ping/pg`
Bluetooth: `scanblue/sbl` `trackme/tm [silent]`
SD: `sdinfo/sdi` `sdls/ls` `sdread/sdr` `sdrm/srm`
Diagnostics: `gpson/gon` `gpsoff/gof` `gpstest/gt` `spktest/st` `loratest/lt`

## SD Layout
`/logs/` — eviltwin.csv · trackme.csv · trackme_known.csv · hidden_ssids.csv
`/evilportal/` — custom HTML portal pages
`/signatures.csv` — custom BLE tracker signatures
`/pwrsave.json` — power save config

## Coding Rules
- New commands: one-liner in `setupCommands()`, assign a category
- No `Serial.println` — especially no passwords/credentials
- No `delay()` in scan loops — use `millis()`
- All display output via `displayManager`
- Poll `inputHandler.getKeyboardInput()` for `q` in every blocking loop
- New modules: own `.cpp/.h` pair
- Command buffer 128 bytes — keep syntax compact

## Pending Features
- WPA handshake capture (EAPOL → `.cap` on SD)
- BadUSB / DuckyScript (TinyUSB)
- BLE GATT enumeration (`bleinfo/bi <mac>`)
- LoRa scanner / packet logger
- macwatch — MAC watchlist with proximity alert
