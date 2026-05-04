---
name: T-DECK-CLI Progress Log
description: What has been implemented, committed, and pushed — full record of completed work
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
All work is on branch `feature/pentest-enhancements`. Every item below is committed and pushed.

## Completed (in order)

### Bug Fixes (commit affe974)
1. pingScan only scanned 1 host → fixed to scan 1-254
2. WiFi passwords rejected special chars → `isPrintable` instead of `isAlphaNumeric`
3. Passwords printed to Serial in plaintext → all removed
4. Duplicate sscanf condition → single call
5. BLE callback memory leak → `pScanCallbacks` tracked and deleted
6. Port scan re-ran full scan on every page flip → scan once into `std::vector<int>`, paginate stored results
7. Command buffer 64 bytes → 128 bytes

### Keyboard Fix (commit c4d2072)
- Reverted interrupt-driven keyboard to `digitalRead` polling
  - GPIO 46 is an ESP32-S3 strapping pin — `attachInterrupt` on it is unreliable, caused keyboard to produce no input
  - `getKeyboardInput()` now checks `digitalRead(BOARD_KEYBOARD_INT) == LOW` then does `Wire.requestFrom`
  - Removed `volatile bool keyAvailable`, ISR, and `attachInterrupt` entirely

### UI Improvements (commit affe974)
- Status bar: live WiFi IP (green) or OFFLINE (grey) + version in 30px header
- Status bar: live WiFi IP (green) or OFFLINE (grey) + version in 30px header
  - `updateStatusBar()` called in `tdeck_begin()` and `printCommandScreen()`
  - Dark navy background with divider line
- `scrollIfNeeded()` prevents text running off bottom of screen
  - Called at start of every `println()` variant
  - Clears output area and resets cursor to outputY when Y > SCREEN_HEIGHT - LINE_HEIGHT*2
- `backspaceChar()` uses computed char width (6 * 1.2) instead of hardcoded 7px
- `clearInputText()` consolidated into `tdeck_begin()` (no more duplication)
- `printCommandScreen()` refreshes status bar on every prompt render

### SD Card (commit da703a2)
- `SDCardManager` class — `sdcard_manager.h/cpp`
- Shares SPI2_HOST with LovyanGFX (bus_shared=true in LGFX config, CS=39)
- Auto-creates `/logs`, `/scripts`, `/captures` on mount
- `appendLine(path, line)` used by all logging modules
- Log path constants: SD_LOG_WIFI, SD_LOG_BT, SD_LOG_PORTS, SD_LOG_PACKETS
- Commands: `sdinfo/sdi`, `sdls/ls [path]`, `sdread/sdr <file>`, `sdrm/srm <file>`
- Graceful boot without SD card

### WiFi Monitor Mode (commit 4f99423)
- `WiFiMonitor` class — `wifimon_functions.h/cpp`
- `esp_wifi_set_promiscuous(true)` + IRAM_ATTR `rxCallback`
- Lock-free ring buffer (32 slots, volatile) between WiFi driver task and main loop
- 802.11 frame parsing: type/subtype, src MAC, BSSID, SSID from beacon/probe IEs
- Channel hopping ch1-13 at 200ms dwell, or lock to specific channel (press digit 1-9)
- Tracks up to 24 unique networks (BSSID, SSID, RSSI, channel, beacon count)
- Display refreshes every 300ms: packet counters + paginated network table (size-1 font)
- SD logging to `/logs/packets.txt`: ch,type,subtype,rssi,src,bssid[,ssid]
- Controls: h=hop, 1-9=lock ch, s=toggle log, a/l=page, q=quit
- Command: `wifimon/wm [channel]`

### Deauth Attack (commits e9ff7d2, b630cc5)
- `DeauthAttack` class — `deauth_functions.h/cpp`
- Raw 802.11 deauth frames via `esp_wifi_80211_tx(WIFI_IF_STA, ...)`
- Frame: FC=0xC0/0x00 (mgmt subtype=12), reason code 7
- Broadcast mode: AP→all clients
- Directed mode: AP→client + client→AP (bidirectional)
- **Index mode**: `deauth <index>` uses scan result from last `scanwifi` run
  - `WiFiFunctions::getNetworkInfo(idx, bssid, channel)` auto-fills BSSID + channel
  - `isScanDone()` and `getNetworkCount()` accessors added to WiFiFunctions
- Manual mode: `deauth <bssid> [channel] [client_mac]` still works
- Command: `deauth/da <index|bssid> [ch] [client]`

### FreeRTOS Task Infrastructure (commit aaec9d1)
- `TaskManager` class — `task_manager.h/cpp` (all static, no global object needed)
- `xTaskCreatePinnedToCore` on Core 0, Arduino loop stays on Core 1
- `volatile bool stopRequested` for clean task shutdown
- FreeRTOS queue (64 slots, `TaskResult` struct) for results Core0→Core1
- `TaskResult::Type`: PORT_OPEN, HOST_FOUND, INFO, DONE
- `cleanup()` resets all state after task exits

**Modules moved to Core 0:**
- Deauth: frame sending loop (`deauthTaskFn`) → Core 0, frame counter shared via volatile uint32_t
- Port scan: TCP connects (`portScanTaskFn`) with 500ms timeout → Core 0, open ports streamed live
- Ping scan: `Ping.ping()` calls (`pingScanTaskFn`) → Core 0, hosts streamed live
- All display/keyboard stays Core 1 → device always responsive, q=stop works instantly

**Port scan also got**: results shown live as `[+] Port N OPEN` instead of waiting for full scan

**Why:** fix to 500ms timeout alone was huge — scanning was previously default TCP timeout (~3s per port)
