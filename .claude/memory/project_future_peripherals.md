---
name: Future peripherals
description: Unused hardware — pins and implementation priority
type: project
---

Trackpad: ✅ DONE (cursor + insert-at-pos, sessions 2026-05-11).

| Peripheral | Pins | Notes |
|---|---|---|
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14, I2C 0x40 | Can't run simultaneous with speaker (same I2S). Must reconfigure between modes. |
| Touchscreen GT911 | INT=16, I2C GT911_SLAVE_ADDRESS_L, RST=-1 | `setSwapXY(true)` + `setMirrorXY(false,true)`. Use SensorLib `TouchDrvGT911`, NOT `tft.getTouch()`. |

**Priority:** mic recorder → touch.
