---
name: project-buddy-port
description: buddy/bd — Claude Desktop BLE remote, WORKING (commit 9b48de8)
---

`buddy/bd [name]` — NimBLE NUS GATT server. Claude Desktop pushes session stats + permission prompts over BLE; `y`=approve, `n`=deny, `q`=quit.

**Status:** Implemented and working on `feature/pentest-enhancements`.

**NUS UUIDs:**
- Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX (write): `6e400002-...`  TX (notify): `6e400003-...`

**Key quirks:**
- `setSecurityAuth/IOCap` MUST be called BEFORE `pSvc->start()` — crashes NimBLE 1.4.x if after. Currently NOT using security (plain NUS, no passkey) — fine for Claude Desktop.
- Call `NimBLEDevice::deinit(true)` on `q` so scanblue/trackme can reinit BLE normally.
- Call `SD.begin(39)` after deinit to restore SPI (BLE deinit can disturb SPI bus).
- GPIO10 = power enable pin — never use as LED.
- Ignore `{"time":[...]}` messages from desktop (no RTC on T-DECK).
- ArduinoJson already in lib_deps — no new dependencies.
