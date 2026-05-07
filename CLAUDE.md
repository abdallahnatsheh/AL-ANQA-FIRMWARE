# T-DECK-CLI — Claude Project Context

## What This Project Is

A pentesting firmware for the LilyGo T-DECK and T-DECK Plus devices, targeting ethical hacking, security education, and network/Bluetooth/GPS/LoRa/BadUSB utilities on a portable cyber-deck form factor. The device runs on an ESP32-S3 with a physical keyboard, TFT display, BLE, WiFi, LoRa radio, SD card, I2S audio, and GPS support.

This is built with PlatformIO + Arduino framework. All source lives in `t-deck-cli/`.

---

## Hardware

| Peripheral | Pin(s) | Status |
|---|---|---|
| Display (ST7789) | CS=12, DC=11, BL=42, SPI=41/38/40 | Working |
| Keyboard (PS/2 via I2C) | SDA=18, SCL=8, slave=0x55, INT=46 | Working |
| Battery ADC | GPIO 4 | Working |
| Touch | INT=16 | Unused |
| SD Card | CS=39, SPI shared | Working — logs/, signatures.csv, evilportal/ |
| LoRa (SX1262) | CS=9, BUSY=13, RST=17, DIO1=45 | Unused |
| Audio I2S out | WS=5, BCK=7, DOUT=6 | Working — trackme alerts, spktest |
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14 | Unused |
| GPS | RX=44, TX=43, baud=9600 (T-Deck Plus only) | Working — L76K / u-blox M10Q, ~4min cold fix |
| Power control | GPIO 10 (must be HIGH) | Working |

**Board:** LilyGo T-DECK (ESP32-S3, 16MB flash, PSRAM)
**Build target:** `env:T-Deck` in platformio.ini
**Screen:** 320×240 ST7789, rotation=1 (landscape)
**Screen layout:** 30px header bar (title) + output area starts at `outputY`

---

## Code Architecture

### Entry Point
`main.ino` — creates all global objects, calls `displayManager.init()` and `commandManager.setupCommands()` in setup, polls keyboard in loop.

### Global Objects (defined in main.ino, accessed via `extern` everywhere)
```cpp
LGFX tft;
DisplayManager displayManager(tft);
CommandManager commandManager;
InputHandling inputHandler;
ESPInfoPrinter espInfoPrinter(displayManager);
WiFiFunctions wifiFunctions(displayManager);
BluetoothFunctions bluetoothFunctions;
```

### Command System (`command_manager.cpp/h`)
- `registerCommand(name, shortName, fn, description, hasArgs)` — max 64 commands
- `processInput(char)` — accumulates keystrokes into 64-byte buffer, executes on Enter
- Commands with `hasArgs=true` receive everything after the command name as `char* args`
- Short names are the canonical alternate (e.g. `sw` for `scanwifi`)

### Display (`display_manager.cpp/h`)
- Wraps LovyanGFX (`LGFX tft`)
- All output goes through `displayManager.println()` / `printText()` / etc.
- `clearScreen()` — clears output area only (below header)
- `tdeck_begin()` — full reset to CLI home screen
- `printCommandScreen()` — prints `CMD> ` prompt inline
- `launchMatrixAnimation()` — blocking loop until `q` pressed

### Input (`input_handling.cpp/h`)
- Polls PS/2 keyboard over I2C (address 0x55)
- `getKeyboardInput()` returns `char` or `0` if no key pressed
- All command loops and scan functions call this in their inner wait loops

### WiFi (`wifi_functions.cpp/h`)
- `scanWiFiNetworks()` — paginated scan, 8 per page
- `connectToWiFiCommand(args)` — connect by index from last scan, stores creds via Preferences
- `networkDiscovery()` — ping scan (currently buggy, see Known Bugs)
- `networkPortScan(args)` — TCP connect port scan, format: `ps <ip> <startPort> <endPort>`
- Credentials stored in ESP32 NVS under namespace `"wifi"`, key = SSID

### Bluetooth (`bluetooth_functions.cpp/h`)
- `scanBluetoothDevices()` — BLE scan, 5 seconds, paginated 6 per page
- Active scan enabled
- Shows: index, MAC, RSSI, name

### Battery (`battery_manager.cpp/h`)
- Pangodream_18650_CL lib, ADC pin 4, averaging 20 reads, conversion factor 1.8
- Percentage = `(voltage - 3.0) / (4.2 - 3.0) * 100` clamped to 0–100

---

## Current Commands

| Command | Short | Args | Description |
|---|---|---|---|
| `help` | `hlp` | `[cmd]` | Help |
| `info` | `inf` | — | Device info (IP, MAC, battery) |
| `clear` | `clr` | — | Clear screen |
| `MATRIX` | `matrix` | — | Matrix rain animation |
| **WiFi** | | | |
| `scanwifi` | `sw` | — | Scan WiFi networks (paginated) |
| `connectwifi` | `cw` | `<index>` | Connect to scanned network |
| `clearwifi` | `clrw` | — | Clear saved credentials |
| `wifimon` | `wm` | `[ch]` | Monitor mode, ch 1-13, 0=hop |
| `deauth` | `da` | `<bssid\|#> [ch] [client]` | Raw 802.11 deauth attack |
| `eviltwin` | `et` | — | Evil Twin AP + captive portal |
| **Network** | | | |
| `netdiscover` | `nd` | — | ARP scan local /24 |
| `portscan` | `ps` | `<ip\|#> <start> <end>` | TCP port scan, 4× parallel |
| `topscan` | `ts` | `<ip\|#>` | Scan top 26 common ports |
| `ping` | `pg` | `<ip\|hostname>` | ICMP ping with RTT + loss |
| **Bluetooth** | | | |
| `scanblue` | `sbl` | — | BLE device scan (paginated) |
| `trackme` | `tm` | — | Anti-tracking scanner |
| **SD Card** | | | |
| `sdinfo` | `sdi` | — | SD card type and size |
| `sdls` | `ls` | `[path]` | List SD directory |
| `sdread` | `sdr` | `<path>` | Read file from SD |
| `sdrm` | `srm` | `<path>` | Delete file from SD |
| **Diagnostics** | | | |
| `gpstest` | `gt` | — | GPS test (T-Deck Plus only) |
| `spktest` | `st` | — | I2S speaker tone test |

---

## Known Bugs (open)

1. **`pingScan` only scans 1 host** (`wifi_functions.cpp:315`)
   - `for (int i = 1; i <= gatewayLastPart; ++i)` should be `i <= 254`

2. **WiFi passwords reject special characters** (`wifi_functions.cpp:213`)
   - `isAlphaNumeric(input)` → change to `isPrintable(input)`

3. **Passwords printed to Serial in plaintext** (`wifi_functions.cpp:39,45`)
   - Remove all `Serial.println("Password: " + password)` lines

4. **Duplicate `sscanf` condition** (`wifi_functions.cpp:23`)
   - `sscanf(...) != 1 && sscanf(...) != 1` — same call twice, should be `||`

5. **BLE callback memory leak** (`bluetooth_functions.cpp:28`)
   - `new MyAdvertisedDeviceCallbacks()` never deleted — leaks on every scan

6. **Port scan re-runs full scan on every page render** (`wifi_functions.cpp:418`)
   - The outer `while(true)` re-scans `startPort..endPort` on each page flip
   - Fix: scan once into a `std::vector<int> openPorts`, paginate the results

---

## Active Branch

`feature/pentest-enhancements` — all new work goes here, PRs back to `main`.

---

## Architecture — Key Modules

### TrackMe (`trackme.cpp/h`)
- `start(bool silent)` — entry point; runs BLE+WiFi scan loop until `q`
- **Baseline (60s)**: all devices seen at startup → `isCompanion=true`, never scored
- **Whitelist**: loads `/logs/trackme_known.csv` at start; `w` key appends to it
- **Tiers**: Tier1 (RSSI > −70, max 20, full analysis) · Tier2 (distant/Apple, max 100)
- **Kalman filter**: 1-D per device, Q=1 R=4, smooths RSSI before all decisions
- **Gate 1**: BLE signature match (company_id + payloadByte + minMfrLen)
- **Gate 2**: behaviour score — time(+25), sightings(+20), variance(+35), gap-return(+25)
- **Gate 3**: GPS path (followDistM≥200m + distinctWindows≥1) OR time path (5min + 3 windows)
- **Gap minimum**: 30s absent before `distinctWindows` increments — misses a single advert
- **WiFi-only cap**: probe sniff only → max NOTICE, requires GPS movement ≥100m
- **WARNING/ALERT only after Gate 3** — score alone can never beep
- **GPS tracking** (`#ifdef BOARD_TDECK_PLUS`): updates `_totalDistM` + per-device `followDistM` every 10m step; shows `GPS:none/srch/Nm` in status
- **I2S audio**: inits `I2S_NUM_0` on start, deinits on exit; 1-beep WARNING, 3-beep ALERT every 30s

### EvilTwin (`eviltwin.cpp/h`)
- `start(ssid)` — interactive: mode menu → scan/pick or custom SSID → AP + portal loop
- **Smart MAC**: open network → clone target BSSID; WPA2 → random locally-administered MAC
- **Adaptive deauth**: 15× deauth + 15× disassoc burst every 8s, pauses while portal clients present
- **Client tracking**: WiFi event-based (`ARDUINO_EVENT_WIFI_AP_STACONNECTED/DISCONNECTED`)
- **Templates**: built-in Google/Router login; `s` key → pick from `/evilportal/*.html` on SD
- Saves credentials to `/logs/eviltwin.csv`

### WiFiMonitor (`wifimon_functions.cpp/h`)
- Promiscuous mode, optional channel hop or fixed channel
- Displays frame type, BSSID, RSSI, channel

### DeauthAttack (`deauth_functions.cpp/h`)
- Raw 802.11 deauth/disassoc via `esp_wifi_80211_tx(WIFI_IF_AP, ...)`
- `ieee80211_raw_frame_sanity_check` override included (required by ESP-IDF)

### NetworkScanner (`network_scanner.cpp/h`)
- ARP scan via raw Ethernet frames
- Parallel TCP port scan (4× FreeRTOS tasks, 150ms timeout)

### GpsManager (`gps_manager.cpp/h`) — T-Deck Plus only
- Singleton: `GpsManager::instance()`
- `start()` / `stop()` — create/destroy FreeRTOS task pinned to core 0
- `gpsTask()` drains serial every 30ms, updates volatile state (no mutex — ARM32 4-byte aligned reads are atomic)
- **Module detection order** (must mirror `test_gps.cpp` exactly):
  1. `_serial->begin(9600, ...)` — inside `initModule()`, before any probe
  2. `initL76K()` — 3 attempts × delay(500) ≈ 4.5s boot window for M10Q
  3. If L76K fails: `_serial->begin(38400, ...)` → `recoverUblox()` (UBX CFG-CFG reset + RATE poll)
  4. If 38400 fails: `updateBaudRate(9600)` → `recoverUblox()`
- **Critical**: single L76K attempt (~1s) is not enough — M10Q needs ~4.5s to boot before UBX ACK arrives
- Status bar icon: grey=off, yellow=searching, green=fixed (`updateStatusBar()` queries instance directly)
- `runGpsOn()` — live display (module, chars/sats, lat/lon/Maps/UTC); `q` exits but task stays running
- Trackme integration: if GpsManager is already running, trackme skips own GPS init and 90s warm-up

---

## Pending Features

- WPA handshake capture (EAPOL filter in promiscuous mode, save `.cap` to SD)
- BadUSB / HID keystroke injection (DuckyScript from SD, TinyUSB)
- BLE GATT enumeration (`bleinfo/bi <mac>`)
- LoRa frequency scanner (RadioLib already in `lib/`)
- Banner grabber (`banner/bn <ip> <port>`)
- macwatch — WiFi probe + BLE MAC watchlist, alert when a known MAC enters range

---

## Libraries

| Library | Location | Used |
|---|---|---|
| LovyanGFX | `lib/LovyanGFX-master/` | Yes |
| DigitalRainAnimation | `lib/TP_Arduino_DigitalRain_Anim/` | Yes |
| ESP32Ping | `lib/ESP32Ping/` | Yes |
| Pangodream_18650_CL | `lib/18650CL-master/` | Yes |
| RadioLib | `lib/RadioLib/` | No — LoRa pending |
| TinyGPSPlus | `lib/TinyGPSPlus/` | No — GPS pending |
| ESP32-audioI2S | `lib/ES32-audioI2S/` | No |
| AceButton | `lib/AceButton/` | No |
| TouchLib | `lib/TouchLib/` | No |
| LVGL | `lib/lvgl/` | No |
| TFT_eSPI | `lib/TFT_eSPI/` | No (replaced by LovyanGFX) |

---

## Coding Rules

- All new commands registered in `CommandManager::setupCommands()` in `command_manager.cpp`
- All display output goes through `displayManager`, never directly to `tft`
- Blocking scan loops must poll `inputHandler.getKeyboardInput()` for `q` to allow exit
- Use `String` (Arduino) for display/WiFi work; avoid mixing with `std::string`
- No `delay()` inside scan loops — use `millis()` for timeouts
- Remove all debug `Serial.println` before merging to main
- Passwords and sensitive data must never be printed to Serial
- Each new feature module gets its own `.cpp/.h` pair
- Command buffer is 128 bytes — keep command syntax compact
