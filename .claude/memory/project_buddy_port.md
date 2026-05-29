---
name: project-buddy-port
description: buddy/bd — WORKING. Key NimBLE quirks only.
type: project
---

`buddy/bd [name]` — NimBLE NUS GATT server. Status: **working** (2026-05-29).

**Key quirks to avoid breakage:**
- `bonding=false` on buddy — NUS doesn't need bonding; prevents NVS writes that would conflict with btkbd bonds
- `onConnect` sets `s_connected=true` immediately (not gated on encryption)
- `setStaticAddress` top 2 bits of MSByte must be `11` (≥ 0xC0) for random static BLE addr
- `setSecurityPasskey` (static passkey) crashes NimBLE v1.4.x — use callback approach only
- `enableScanResponse(true)` + `setName()` BEFORE `startAdvertising()` — required in v2.x for name visibility
- Cleanup: `deinit(true)` only, never re-init (see nimble_v2_rules.md rule 2)
