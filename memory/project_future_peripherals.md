---
name: Future peripherals — mic, trackpad, touch
description: Hardware pins and critical init for ES7210 mic, optical trackpad, GT911 touchscreen
type: project
---

## Microphone — ES7210
- I2S pins: MCLK=48, LRCK=21, SCK=47, DIN=14
- **Conflicts with speaker** (BCK=7, WS=5, DOUT=6) — both use I2S_NUM_0; must reconfigure between modes
- I2C init required: address 0x40, Wire (SDA=18, SCL=8)

## Trackpad (optical)
- Reported via keyboard MCU I2C 0x55 — same `Wire.requestFrom(0x55, n)` as `input_handling.cpp`
- INT: GPIO16. Delta X/Y in same packet as key codes. Dead zone ±2 needed.

## Touchscreen — Goodix GT911
- Library: `TouchDrvGT911.hpp` (SensorLib) · INT=GPIO16 · RST=-1 · I2C: `GT911_SLAVE_ADDRESS_L`
- Critical init:
```cpp
touch.setPins(-1, BOARD_TOUCH_INT);
touch.begin(Wire, GT911_SLAVE_ADDRESS_L);
touch.setMaxCoordinates(320, 240);
touch.setSwapXY(true);           // REQUIRED
touch.setMirrorXY(false, true);  // REQUIRED — Y mirrored
```
- Read: `touch.getPoint(x, y, touch.getSupportTouchPoint())`
- Do NOT use `tft.getTouch()` — use SensorLib only.
