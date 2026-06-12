---
name: project-bleinfo
description: "bleinfo/bi — compiled, NOT YET FIELD TESTED"
---

`bleinfo/bi <index|mac|all>` — full GATT enum, read, write, fuzz, sniff, pair, auth-risk audit, write-cap replay. Implemented in `ble_info.cpp`.

**Status:** compiles, core enum working. ⚠️ `[b]` audit + `[r]` write-cap NOT field tested.

**Critical quirks:**
- `s_loadedPkts[]` MUST be declared after `BiNotif` struct — causes compile error if placed in SD section at top
- Off-by-one fix: store `bs.nChars - 1` (not `bs.nChars`) when saving to `s_writable[]`
- `onPassKeyRequest()` returns `uint32_t` (the passkey) — not void; no `injectPassKey` needed
- `NimBLE setSecurityAuth/IOCap` MUST be called BEFORE `pSvc->start()`

**SD safety:** uses `canAccessSD()` (not `isReady()`). BLE + SD safe on ESP32-S3 (no GDMA conflict).

**Man page limit:** `lines[12]` = max 11 content lines + nullptr.
