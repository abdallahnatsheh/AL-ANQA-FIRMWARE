---
name: T-DECK-CLI Project Overview
description: Core facts about the T-DECK-CLI / T-Rex project — hardware, platform, libraries, source layout, features, branch structure
type: project
originSessionId: c4148f1c-fc4f-436a-8b45-e1f432add3ec
---
T-DECK-CLI (being renamed to **T-Rex**) is an offensive security CLI firmware for the LilyGo T-DECK and T-DECK Plus (ESP32-S3 device with physical keyboard and TFT display).

**Why:** Portable pentesting tool — hacker CLI in your pocket. Ethical hacking / security education.

**Stack:**
- PlatformIO + Arduino framework, board: esp32s3box, 16MB flash, 8MB PSRAM
- Display: LovyanGFX (320×240 ST7789)
- Source dir: `t-deck-cli/`
- Default build env: `T-Deck-Plus` (has GPS + speaker); `T-Deck` for base model
- License: AGPL-3.0

**Branch structure:**
- `main` — stable releases
- `feature/pentest-enhancements` — active dev branch, all new work goes here
- `test/build-verify` — created from feature branch to test before merging back

**Key source files (all in t-deck-cli/):**
- `main.ino` — entry point, global objects
- `display_manager.cpp/.h` — screen rendering (LovyanGFX wrapper)
- `command_manager.cpp/.h` — CLI command dispatch (64 commands max, 128-byte buffer)
- `wifi_functions.cpp/.h` — WiFi scan, connect, monitor mode, ARP scan, port scan, ping
- `bluetooth_functions.cpp/.h` — BLE scanner
- `trackme.cpp/.h` — anti-tracking scanner (BLE + WiFi probes, 3-gate pipeline, Kalman filter)
- `eviltwin.cpp/.h` — Evil Twin AP + captive portal (Bruce-derived, AGPL-3.0)
- `deauth_functions.cpp/.h` — raw 802.11 deauth (Bruce-derived, AGPL-3.0)
- `battery_manager.cpp/.h` — battery level (18650CL lib)
- `input_handling.cpp/.h` — PS/2 keyboard input over I2C
- `test_gps.cpp` — GPS test command (working L76K + u-blox init, use as reference)

**Features implemented:** WiFi scan/connect/monitor/deauth, Evil Twin AP, ARP scan, port scan, top-26 scan, ICMP ping, BLE scan, anti-tracking (trackme), SD card manager, Matrix animation, GPS test, speaker test.

**Compliance:**
- LICENSE: AGPL-3.0 at repo root
- NOTICES: third-party attribution file at repo root
- Bruce Firmware (https://github.com/pr3y/Bruce) is the only derived code source — AGPL-3.0, credited in headers of eviltwin.cpp and deauth_functions.cpp
- Marauder was never used — remove any reference to it if found

**How to apply:** Keep modular file structure. Build via `pio run -e T-Deck-Plus`. Push requires user to run `git push` from their own terminal (HTTPS credential prompt blocked in non-interactive env).
