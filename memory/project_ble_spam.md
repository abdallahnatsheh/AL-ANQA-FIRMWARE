---
name: ble-spam-module
description: "BLE notification spam suite (bs command) — Apple/Android/Microsoft/Samsung advertisement flood, NimBLE stack, SD-safe teardown pattern"
metadata: 
  node_type: memory
  type: project
  originSessionId: f2a28e73-2aae-4b57-a11c-57573886b78c
---

`blespam/bs` — `ble_spam.cpp/.h`, registered in `command_manager.cpp`, man page in `man_pages.cpp`.

**Subcommands:** `apple` (AppleJuice + SourApple), `android` (Google Fast Pair flood), `ms` (Microsoft Swift Pair), `samsung` (Galaxy accessories), `all` (cycle all four). Keys: `[l/a]` cycle type, `[q]` stop.

**Advertisement formats** (byte arrays from BruceDevices/firmware, AGPL-3.0):
- Apple: Proximity Pairing type `0x07` (17 model bytes) + Nearby Info type `0x0F` (11 action bytes, random bytes via `esp_fill_random`)
- Android: UUID list `0xFE2C` + Service Data `0xFE2C` + 3-byte model ID — no TX power, flags `0x06`
- Microsoft Swift Pair: manufacturer data company `0x0006` + `{0x03,0x00,0x80}` + device name — length byte = `6 + nameLen`
- Samsung: manufacturer data company `0x0075` + 11-byte fixed payload + model byte — length byte = `0x0e` (14), NOT `0x0f`

**NimBLE stack pattern:**
- `bsReinit()` — just `NimBLEDevice::init("")` (idempotent, safe on cold boot, no deinit)
- `bsRestoreStack()` — `deinit(true)` → `vTaskDelay(50ms)` → `init("T-REX")` → `SD.begin(39)`. Deinit BEFORE SD.begin is required — BLE deinit disturbs SPI bus on ESP32-S3
- Android spam: per-cycle `deinit(true)` + `init("")` for MAC randomization (Android deduplicates Fast Pair by MAC+modelId pair). Requires `init("")` BEFORE the loop's first `deinit`
- All other vendors: single `bsReinit()` before loop, no per-cycle deinit — `pAdv->start()` auto-stops previous advertisement

**Advertising parameters:** `setMinInterval(32)` / `setMaxInterval(48)` — minimum valid is 32 units (20ms); values below crash BLE controller. Never call `setPower(ESP_PWR_LVL_P9)` on ESP32-S3 — unsupported, causes panic.

**Android 16 note:** Fast Pair popups require anti-spoofing GATT verification on Android 16 — advertisement-only spoofing may not trigger popup. Microsoft Swift Pair still works (no anti-spoofing). Use `fp h <idx>` (GATT attack) for Android.
