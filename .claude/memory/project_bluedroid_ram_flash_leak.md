---
name: project_bluedroid_ram_flash_leak
description: mac_changer.cpp's BLE MAC randomizer called legacy Bluedroid APIs, dragging in the entire Bluedroid host stack + BLE Mesh. FIXED + HW-VERIFIED 2026-07-02 — swapped to NimBLE-native, saved ~19.7KB RAM + ~428KB flash, `mc random`/`mc status` confirmed changing the BLE MAC on real hardware.
metadata:
  type: project
---

# Bluedroid/BLE-Mesh accidentally linked in via mac_changer.cpp — FIXED + HW-VERIFIED 2026-07-02

## Status: DONE. Code fixed, both envs build clean, confirmed working on real hardware.

## The finding (as discovered, kept for context)
Build stats prompted a RAM/flash audit (`RAM 65.4% (214344/327680)`, `Flash 40.1%
(2625037/6553600)` on `T-Deck-Plus`, before the fix). Investigated the real linker map
rather than guessing. Root cause, traced via the map's "Archive member included" log:
```
libbt.a(esp_bt_main.c.obj)     <=  mac_changer.cpp.o (esp_bluedroid_get_status)
libbt.a(esp_gap_ble_api.c.obj) <=  mac_changer.cpp.o (esp_ble_gap_set_rand_addr)
```
`MacChanger::applyBleMac()` (used by `mc target bt`/`mc target both` to randomize the BT
MAC) called the legacy **Bluedroid** API directly — the ONLY place in the codebase that
touched Bluedroid; every other BLE feature (bmon, bleinfo, trackme, macwatch, espchat) is
NimBLE (see [[nimble_v2_rules]]). That pulled 150 extra objects out of the precompiled
`libbt.a` — Bluedroid's device manager, GATT client/server, SMP, L2CAP, and the **entire
BLE Mesh stack**.

**Bonus discovery while designing the fix:** Bluedroid is never `esp_bluedroid_init/enable`d
anywhere else in this codebase, so `esp_bluedroid_get_status() != ENABLED` was **always**
true in real usage → `applyBleMac()` silently returned early every single time. `mc target
bt`/`mc target both` has been a no-op the whole time this code existed — the old
implementation didn't just waste RAM/flash, it never actually spoofed the BLE MAC. This
made the fix a strict improvement with no working behavior to preserve/regress.

## The fix
`al-anqa-firmware/wifi/attacks/mac_changer/mac_changer.cpp`:
- Swapped `#include "esp_gap_ble_api.h"` + `"esp_bt_main.h"` for `#include <NimBLEDevice.h>`.
- `applyBleMac()`: `esp_bluedroid_get_status()`/`esp_ble_gap_set_rand_addr()` →
  `NimBLEDevice::isInitialized()` guard + `NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)`
  + `NimBLEDevice::setOwnAddr(mac)` (wraps `ble_hs_id_set_rnd()` — pure NimBLE host API, see
  `NimBLEDevice.cpp` ~line 1159). Same "no-op if the stack isn't up" contract as before,
  just checked against the stack this project actually uses — so now it's a no-op **only**
  when no BT tool currently has `NimBLEDevice::init()` active, instead of always.
- `off`/restore path: `esp_ble_gap_clear_rand_addr()` → `NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC)`.
- `mac_changer.h` needed no changes (NimBLE type doesn't leak into the header).

## Measured result (both `pio run -e T-Deck-Plus` and `-e T-Deck`, SUCCESS)
| | Before | After | Saved |
|---|---|---|---|
| RAM (T-Deck-Plus) | 214344 B (65.4%) | 194644 B (59.4%) | **19700 B (~19.2KB)** |
| Flash (T-Deck-Plus) | 2625037 B (40.1%) | 2196709 B (33.5%) | **428328 B (~418KB)** |

Beat the original estimate (~8.9KB RAM/~593KB flash from summing the 150 raw `.obj` sizes
pre-link) — actual linked savings were larger on RAM, smaller on flash (link-time
`--gc-sections` dropping unused code inside those objects explains the flash gap; the RAM
delta being *bigger* than the raw sum suggests some of those 150 objects also disabled
inlining/padding that the linker otherwise had to keep). Confirmed via `.map` re-scan:
`libbt.a(` member count went from 151 → **1** (`bt.c.obj` only, the VHCI controller glue
NimBLE genuinely needs — 37 bytes `.data`, 0 `.bss`, negligible).

## Hardware verification — DONE 2026-07-02
Flashed both envs. Confirmed on real hardware:
- Baseline (BT never touched): `mc target bt` + `mc random` correctly no-op'd — no `BLE:`
  line in `mc status` — proves the `NimBLEDevice::isInitialized()` guard is safe.
- After `sbl` (brings NimBLE up) → `mc target bt` → `mc random` → `mc status`: a real
  non-zero BLE MAC appeared, and **changed on repeated `mc random` calls** — proves
  `NimBLEDevice::setOwnAddr()`/`ble_hs_id_set_rnd()` is genuinely succeeding now (the old
  Bluedroid call never did). User confirmed: "yes mac changed".

**Important side-discovery — `bk`/`btkbd` and `bd`/`buddy` are NOT valid ways to test this
over the air.** First attempt used `bk` to see the spoofed address on an external scanner;
the laptop auto-reconnected because it was previously paired, which looked like a failure.
Root cause (`ble_keyboard.cpp:466-487`, mirrored in `buddy.cpp:808-827`): both commands do
a full `NimBLEDevice::deinit(true)` → `init()` cold-reset in their own `beginHid()`/start
path and then **hardcode their own address** derived from the real HW BT MAC (`C2:xx:xx:xx:
xx:CB` for btkbd, `xx:xx:xx:xx:xx:BD`-style for buddy) — by design, so their HID/GATT
bonds survive across reboots ("Windows stores two separate bonds and never confuses
keyboard with NUS client"). Both **intentionally override/ignore whatever `mc` set**. This
is correct/intended behavior for those two tools, not a bug — but it means there is
currently no Al-Anqa command that advertises using `mc`'s raw spoofed address, so true
over-the-air confirmation (a phone scanner literally seeing the spoofed MAC) isn't possible
with existing tooling. The on-device `mc status` signal (address populates + changes only
on API success) is accepted as sufficient verification for this fix. A future throwaway
raw-advertising test path could close that gap if ever wanted — not currently planned.

## Follow-up UX fixes (found during the same HW test session, all FIXED + committed)
- **Target-aware confirmation print.** `mc random`/`mc set` always printed `_currentMac`
  (the WiFi field) regardless of `_target`, so `target=bt` showed a misleading all-zero
  WiFi line. Fixed with `printAppliedMac()` — prints `WiFi:`/`BLE :` only for the field(s)
  `_target` actually applies.
- **`applyBleMac()`/`applyAll()` now return success**, not `void` — needed so the UI can
  tell "spoof applied" from "BLE requested but stack not up" instead of just showing zeros
  both times.
- **Two-state-aware status message.** First pass just said "BLE not active" whenever
  `_currentBleMac` was zero, conflating two different real states: (a) BT stack never
  initialized this session vs. (b) BT stack IS up (e.g. after running `sbl`) but `mc
  random`/`mc set` was never actually run yet. Caught via user HW test: ran `sbl`, quit,
  checked `mc status`, saw "not active" even though NimBLE was genuinely still initialized
  (confirmed `sbl`'s exit path never deinits — `bluetooth_functions.cpp:215-217` — by
  design, so btkbd can find the stack still warm). Fixed by checking
  `NimBLEDevice::isInitialized()` directly instead of inferring from the zeroed MAC:
  `"not applied yet — run: mc random"` vs `"BT stack not active — run a BT tool first"`.
- **Autocomplete**: `mc`'s subcommand hints already existed in `kArgHints`
  (`on off random set restore target`, `target→wifi/bt/both`, `restore→on/off`) — only
  `status` was missing (deliberately, since it's not a distinct code path — bare `mc` and
  `mc status` both fall through to the same `printStatus()`). Added it anyway since it
  works and users expect it, matching `pwrsave`/`lock`'s pattern of listing `status`.
- **Unrelated bug caught by the build**: an earlier man_pages.cpp edit (undercover mode
  docs, different session topic) had overflowed the fixed `const char* lines[32]` array —
  trimmed it back under budget.

User confirmed final state working: "now all good commit it". Committed as `89965e0` on
`feature/pentest-enhancements`.

Related: [[nimble_v2_rules]], [[project_improvement_backlog]] (Tier 2b — mark done),
[[macwatch]] (reference NimBLE usage pattern this fix followed).
