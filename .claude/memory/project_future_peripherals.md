---
name: Peripherals reference
description: Pins + non-obvious gotchas for the T-Deck peripherals — all now DONE
type: project
---

All three are now BUILT & HW-verified — trackpad ✅, ES7210 mic ✅, GT911 touch ✅. Kept
as a pin + gotcha reference (the framing as "future/unused" was stale).

| Peripheral | Pins | Reality |
|---|---|---|
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14, I2C 0x40 | **Present on BOTH boards** (only GPS is Plus-only). Mic = **I2S_NUM_1 (RX)**, speaker = **I2S_NUM_0 (TX)** — SEPARATE peripherals that **coexist resident** (espvoice installs both once per session, gates by PTT; the old "same I2S / can't run simultaneous" note was WRONG — cycling drivers per PTT crashed). ALL_LEFT delivers 2 int16/sample (L/R dup) → de-dup `raw[2*i]`. Consumers: `mictest/mt`, `espvoice/ev`. |
| GT911 touch | INT=16 (`BOARD_TOUCH_INT`), addr 0x5D (`GT911_SLAVE_ADDRESS_L`) → 0x14 auto-probe, RST=-1 | SensorLib `TouchDrvGT911` via **umbrella `#include "TouchDrv.hpp"`** (0.4.x deprecated the per-driver `TouchDrvGT911.hpp`). Read with **`getTouchPoints()`** (the `getPoint(x,y,n)` overload is deprecated in 0.4.x). Config `setMaxCoordinates(320,240)`+`setSwapXY(true)`+`setMirrorXY(false,true)` (LilyGo vendor values, verbatim). Reuses shared `Wire` (SDA=18/SCL=8). NOT `tft.getTouch()`. Wrapped by `TouchManager` singleton → `test touch`, notes/undercover. See [[PLAN-undercover-touch]]. |

Trackpad: cursor + insert-at-pos, done 2026-05-11.
