---
name: esp32s3-wifi-sd-gdma-constraint
description: ESP32-S3 critical rule — WiFi and SPI-SD share GDMA; wrong teardown corrupts SD permanently until reboot
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 375d410f-2458-450f-8ce0-724f0e36b6fe
---

Never write to SD while WiFi is in APSTA or promiscuous mode. Never call `WiFi.disconnect(true)`, `WiFi.mode(WIFI_OFF)`, or anything that calls `esp_wifi_stop()` / `esp_wifi_deinit()` — these corrupt the GDMA controller that SPI-SD also uses, causing all subsequent `SD.open()` calls to fail until reboot.

**Why:** WiFi and the SD SPI bus share the ESP32-S3 GDMA controller. `esp_wifi_stop()` resets GDMA state mid-flight. `esp_wifi_deinit()` corrupts SPI bus state entirely. Both manifest as `SD.open()` returning null with no other error signal. Discovered during `ws`/`wm`/`da` debugging — every command that called `WiFi.mode(WIFI_OFF)` on exit left the SD permanently unmounted.

**How to apply:**
- WiFi teardown: always use `WiFi.mode(WIFI_STA)` (leaves stack alive, safe for SD) — never `WIFI_OFF`
- AP association drop: always use `WiFi.disconnect(false)` — never `disconnect(true)`
- SD writes during capture: buffer in RAM, write only AFTER WiFi is fully torn down
- SD file open: always open files BEFORE calling `WiFi.mode(APSTA)` or `esp_wifi_set_promiscuous(true)`
- Promiscuous filter: always reset explicitly (`esp_wifi_set_promiscuous_filter`) before enabling — prior sessions leave DATA-only filter that hides management frames
- BLE deinit+init cycles (`NimBLEDevice::deinit(true)`) also disturb the SD SPI bus on ESP32-S3 — always call `SD.begin(39)` after BLE teardown in restore functions (e.g. `bsRestoreStack`)
- `disconnect(true)` violations found and fixed in: `wifi_functions.cpp` (connectwifi), `eviltwin.cpp` (AP start + stop); `WIFI_OFF` violations fixed in `eviltwin.cpp` and `hidden_ssid.cpp`
- BLE active + WiFi active simultaneously amplifies GDMA contention — after BLE commands, BLE radio stays on (NimBLE initialized); `disconnect(true)` while BLE is active causes guaranteed SD corruption
