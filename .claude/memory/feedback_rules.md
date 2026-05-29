---
name: Collaboration + Coding Rules
description: All rules for how to work with this user and this codebase
type: feedback
---

## Collaboration
1. No `AskUserQuestion` tool — pick best approach, propose in text, let user redirect.
2. No redundant verification — don't grep/diff/status after edits just to confirm. Trust the edit.
3. No full license text via Write — AGPL-3.0 text triggers content filter. User pastes manually.
4. Concise — Abdallah wants direct communication, file:line refs, no padding.

## Code Quality
5. Reuse existing code — read the class before writing new functions. If a method exists, call it.
6. Verify APIs before using — check actual header files (e.g. `getInitialized()` not `isInitialized()`).
7. Test logic mentally — trace crash scenarios before submitting. Don't iterate fixes in chat.

## Hardware Rules (ESP32-S3)
**GDMA — CRITICAL:** WiFi and SPI-SD share the GDMA controller. Wrong teardown = SD permanently broken until reboot.
- WiFi teardown: always `WiFi.mode(WIFI_STA)` — never `WIFI_OFF`
- AP drop: always `WiFi.disconnect(false)` — never `disconnect(true)`
- SD writes during capture: buffer in RAM, write only AFTER WiFi fully torn down
- SD file open: always BEFORE `WiFi.mode(APSTA)` or `esp_wifi_set_promiscuous(true)`
- After `NimBLEDevice::deinit(true)`: always call `SD.begin(39)` — BLE deinit disturbs SPI bus

## User Background
Abdallah — embedded/ESP32 dev, PlatformIO + Arduino, C++, WiFi/BLE APIs. Personal pentesting firmware project for ethical hacking/education.
