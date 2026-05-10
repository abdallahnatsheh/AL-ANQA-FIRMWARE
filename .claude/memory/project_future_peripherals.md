---
name: Future peripherals
description: Unused hardware — pins and implementation priority
type: project
---

| Peripheral | Pins | Notes |
|---|---|---|
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14, I2C addr=0x40 | Can't run simultaneous with speaker (same I2S). Must reconfigure between modes. |
| Trackpad (optical) | INT=16, via keyboard MCU I2C 0x55 | Delta bytes come in same `Wire.requestFrom` packet as key codes. Hook into `getKeyboardInput()` → emit virtual a/l/Enter. |
| Touchscreen GT911 | INT=16, I2C GT911_SLAVE_ADDRESS_L, RST=-1 | `setSwapXY(true)` + `setMirrorXY(false,true)` required. Use SensorLib `TouchDrvGT911`, NOT `tft.getTouch()`. |

**Priority:** trackpad first (modify `getKeyboardInput()`, all commands benefit) → mic recorder → touch.
