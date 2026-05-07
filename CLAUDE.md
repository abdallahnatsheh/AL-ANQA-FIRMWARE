# T-DECK-CLI — Claude Project Context

## Project
Pentesting firmware for LilyGo T-DECK / T-DECK Plus (ESP32-S3). PlatformIO + Arduino. All source in `t-deck-cli/`. Build target: `env:T-Deck` (standard) or `env:T-Deck-Plus` (GPS + speaker). Screen: 320×240, 30px status bar, output starts at `outputY=38`.

## Key Hardware (T-Deck Plus additions in parentheses)
- Display ST7789 · Keyboard PS/2 I2C 0x55 · Battery ADC GPIO4 · SD CS=39
- Audio I2S: BCK=7, WS=5, DOUT=6 — **must use i2s_driver_install(), tone() does NOT work**
- GPS (Plus only): RX=44, TX=43, 9600 baud — L76K or u-blox M10Q, ~4min cold fix
- Power: GPIO10 must be HIGH

## Architecture

**Entry:** `main.ino` — global objects, `displayManager.init()`, `commandManager.setupCommands()`, keyboard poll loop.

**Command system** (`command_manager.cpp/h`):
- `registerCommand(name, shortName, fn, description, hasArgs, category)` — max 64 commands
- All registrations are one-liners in `setupCommands()` — keep them that way
- Categories: System · WiFi · Network · Bluetooth · SD Card · Diagnostics
- `help` paginates by category (a/l to navigate)

**Display** (`display_manager.cpp/h`): all output via `displayManager` — never write to `tft` directly. `clearScreen()` clears below header only. `tdeck_begin()` full reset.

**Input** (`input_handling.cpp/h`): `getKeyboardInput()` returns `char` or `0`. All blocking loops must poll this for `q`.

**GpsManager** (`gps_manager.cpp/h`) — T-Deck Plus only, singleton:
- FreeRTOS task on core 0, 30ms poll, volatile primitives (no mutex needed on ARM32)
- Init order in `initModule()`: `begin(9600)` → `initL76K()` 3 attempts (≈4.5s M10Q boot window) → `begin(38400)` + `recoverUblox()` → `updateBaudRate(9600)` + `recoverUblox()`
- **Must mirror test_gps.cpp exactly** — single L76K attempt is not enough for M10Q
- Status bar icon: grey=off, yellow=searching, green=fixed
- `runGpsOn()`: live display, `q` exits but task stays running

**TrackMe** (`trackme.cpp/h`):
- `start(bool silent)` — shows experimental disclaimer, then GPS readiness, then 60s baseline
- If `GpsManager` already running: skips own GPS init and warm-up
- WiFi probe sniff: T-Deck Plus only (`#ifdef BOARD_TDECK_PLUS`)
- Gate1=signature · Gate2=score(max 100) · Gate3=GPS≥200m OR time≥5min — WARNING/ALERT need Gate3
- Whitelist: `/logs/trackme_known.csv` · Signatures: `/signatures.csv`

**EvilTwin** (`eviltwin.cpp/h`):
- Clone MAC for open networks, random LA-MAC for WPA2
- Adaptive deauth pauses when portal clients connected
- Templates: built-in or `/evilportal/*.html` from SD · Logs to `/logs/eviltwin.csv`

**NetworkScanner** (`network_scanner.cpp/h`):
- ARP scan full /24 (batchStart 1→254)
- Port scan: results collected into `std::vector<int> openPorts` once, then paginated

## Commands (current)
System: `help/hlp` `info/inf` `clear/clr` `MATRIX/matrix`
WiFi: `scanwifi/sw` `connectwifi/cw` `clearwifi/clrw` `wifimon/wm` `deauth/da` `eviltwin/et`
Network: `netdiscover/nd` `portscan/ps` `topscan/ts` `ping/pg`
Bluetooth: `scanblue/sbl` `trackme/tm [silent]`
SD: `sdinfo/sdi` `sdls/ls` `sdread/sdr` `sdrm/srm`
Diagnostics: `gpson/gon` `gpsoff/gof` `gpstest/gt` `spktest/st`

## SD Layout
`/logs/` — eviltwin.csv, trackme.csv, trackme_known.csv
`/evilportal/` — custom HTML portal pages
`/signatures.csv` — custom BLE tracker signatures

## Coding Rules
- New commands: one-liner in `setupCommands()`, assign a category
- No `Serial.println` — especially never print passwords or credentials
- No `delay()` in scan loops — use `millis()`
- All display output through `displayManager`
- Poll `inputHandler.getKeyboardInput()` for `q` in every blocking loop
- Each new module gets its own `.cpp/.h` pair
- Command buffer 128 bytes — keep syntax compact

## Pending Features
- WPA handshake capture (EAPOL → `.cap` on SD)
- BadUSB / DuckyScript (TinyUSB)
- BLE GATT enumeration (`bleinfo/bi <mac>`)
- LoRa scanner (RadioLib in `lib/`)
- macwatch — MAC watchlist with proximity alert
