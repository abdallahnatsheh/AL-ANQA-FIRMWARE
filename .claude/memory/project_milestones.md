---
name: T-Rex Project Milestones
description: Completed work and pending tasks across sessions
type: project
originSessionId: c4148f1c-fc4f-436a-8b45-e1f432add3ec
---
## Completed

### Trackme improvements (session 2025-05)
- Fixed Apple false positives: added `minMfrLen=27` for AirTag, 8 Apple THREAT_NONE entries (iPhones/Macs/AirPods never flagged)
- Fixed BLE double-init crash: `s_bleInited` guard, removed `BLEDevice::deinit()`
- Replaced broken GPS implementation with TinyGPS++ + proper L76K/u-blox init (mirrors test_gps.cpp)
- Fixed `isKnown` bug: only true for tracker-level signatures (level != THREAT_NONE)
- Reduced I2S timeout: `portMAX_DELAY` → `pdMS_TO_TICKS(500)`
- Improved Gate 2 scoring: +15 for 2+ distinct gap-and-return windows
- Added silent mode: `tm s` / `tm silent` — suppresses speaker, shows [MUTE] in header

### AGPL-3.0 compliance (session 2025-05)
- `LICENSE` — AGPL-3.0 (user pasted manually; Write tool blocked by content filter)
- `NOTICES` — third-party attribution for Bruce, LovyanGFX, AirGuard, and 7 others
- `deauth_functions.cpp` — SPDX header + Bruce AGPL-3.0 attribution
- `eviltwin.cpp` — upgraded to full SPDX header + Bruce AGPL-3.0 attribution
- README badge: GPL-3.0 → AGPL-3.0
- README credits: Bruce correctly attributed as AGPL-3.0, "with permission" removed

### README cleanup (session 2025-05)
- Removed ESP32 Marauder from inspiration and credits (was never used — Bruce → T-Rex only)
- Added AI tools credit: ChatGPT · GitHub Copilot · Claude Code (under Credits)
- Expanded trackme documentation (3-gate pipeline, Kalman, tiers, alerts, limitations)
- Commands table verified against command_manager.cpp

### Splash screen (session 2025-05)
- `convert_splash.py` — PlatformIO extra_script that converts `images/T-REX-LOADING SCREEN.png` to `t-deck-cli/splash_image.h` at build time (runs immediately on script load, not as pre-action)
- `t-deck-cli/splash_screen.h/.cpp` — shows splash for 3s, any key skips, then clears screen
- `platformio.ini` — added `extra_scripts = pre:convert_splash.py` in shared [env]
- `main.ino` — `showSplashScreen()` called after `displayManager.init()` + `inputHandler.begin()`
- `splash_image.h` is git-ignored (auto-generated); `splash_image.h` generated successfully on first build

### Branch structure (session 2025-05)
- `test/build-verify` created from `feature/pentest-enhancements` for build testing
- All changes committed and pushed to `test/build-verify`

## Pending

- Build test: verify full clean build on `test/build-verify` (splash_image.h now generates correctly)
- Merge `test/build-verify` → `feature/pentest-enhancements` after successful build
- Merge `feature/pentest-enhancements` → `main` for release
- Rename GitHub repo from `T-DECK-CLI` to `T-Rex`
- TinyGPS++ not in `platformio.ini` lib_deps — may need to add for T-Deck-Plus build

## Roadmap features (not started)
- WPA handshake capture + PCAP export to SD
- BadUSB / HID keystroke injection (DuckyScript)
- BLE GATT enumeration
- LoRa frequency scanner + packet logger
- Banner grabber
- DNS enumeration
