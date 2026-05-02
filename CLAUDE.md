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
| SD Card | CS=39, SPI shared | Unused |
| LoRa (SX1262) | CS=9, BUSY=13, RST=17, DIO1=45 | Unused |
| Audio I2S out | WS=5, BCK=7, DOUT=6 | Unused |
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14 | Unused |
| GPS | (UART — module availability TBD) | Unused |
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
| `info` | `inf` | — | Device info (IP, MAC, battery) |
| `clear` | `clr` | — | Clear screen |
| `scanwifi` | `sw` | — | Scan WiFi networks |
| `connectwifi` | `cw` | `<index>` | Connect to scanned network |
| `clearwifi` | `clrw` | — | Clear saved credentials |
| `netdiscover` | `nd` | — | Ping scan local subnet |
| `portscan` | `ps` | `<ip> <start> <end>` | TCP connect port scan |
| `scanblue` | `sbl` | — | BLE device scan |
| `MATRIX` | `matrix` | — | Matrix animation |
| `help` | `hlp` | `[command]` | Help |

---

## Known Bugs (must fix before any new features)

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

7. **Command buffer too small** (`command_manager.h`)
   - 64-byte buffer is too short for `portscan 192.168.100.200 1 65535`
   - Increase to 128 bytes

---

## Active Branch

`feature/pentest-enhancements` — all new work goes here, PRs back to `main`.

---

## Planned Pentesting Features (Prioritized)

### Tier 1 — WiFi Attack Surface
- **Monitor mode / packet sniffer** (`wifimon/wm`) — `esp_wifi_set_promiscuous()` + frame callback
- **Deauth attack** (`deauth/da <bssid>`) — raw 802.11 deauth via `esp_wifi_80211_tx()`
- **Evil Twin AP** (`eviltwin/et <ssid>`) — rogue AP + captive portal logging credentials
- **WPS detection** (`wpscheck/wc`) — flag WPS-enabled networks in scan output
- **WPA2 handshake capture** — EAPOL frame filter in promiscuous mode, save `.cap` to SD

### Tier 2 — Network Recon
- **ARP scan** (replace/augment ping scan) — faster, firewall-resistant
- **Banner grabbing** (`banner/bn <ip> <port>`) — read first 256 bytes from open port
- **Subnet-aware discovery** — use actual subnet mask for host range calculation

### Tier 3 — Bluetooth
- **BLE GATT enumeration** (`bleinfo/bi <mac>`) — connect, list services + characteristics
- **BLE device tracker** (`btrack/bt`) — continuous scan, log new/disappearing MACs
- **Bluetooth Classic scan** — `esp_bt_gap_start_discovery()` for non-BLE devices

### Tier 4 — BadUSB / HID
- **HID keystroke injection** (`badusb/bu <file>`) — ESP32-S3 native USB, DuckyScript from SD
- Requires: TinyUSB HID + SD card integration

### Tier 5 — LoRa (RadioLib already in lib/)
- **LoRa scanner** (`lorascan/ls`) — scan frequencies, display packets (freq, SF, RSSI, SNR, hex payload)

### Tier 6 — Infrastructure
- **SD card integration** — log all scan results, store BadUSB scripts
- **GPS wardriving** — correlate WiFi/BT scans with GPS coordinates, save to SD

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
