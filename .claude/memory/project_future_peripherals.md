---
name: Future peripherals — microphone, trackpad, touchscreen
description: Hardware context and feature ideas for ES7210 mic, optical trackpad, and touch input — not yet implemented
type: project
---

Three unused peripherals on T-Deck hardware with no commands built yet.

**Why:** User wants to use them in the future but hasn't decided on specific features yet. Save hardware context now so implementation can start without re-researching pins/drivers.

---

## 1. Microphone — ES7210

**Hardware:**
- 4-channel I2S microphone codec (ES7210)
- Pins (from `utilities.h`): MCLK=48, LRCK=21, SCK=47, DIN=14
- Uses `i2s_driver_install()` — same I2S bus as speaker (BCK=7, WS=5, DOUT=6)
- Cannot use mic and speaker simultaneously on the same I2S peripheral without reconfiguring

**Feature ideas:**
- `audiocap` — record audio to SD card as raw PCM or WAV, `q` to stop
- Audio level meter — VU bar on screen, real-time amplitude from mic buffer
- Sound trigger — wake from power-save or start a capture when volume exceeds threshold
- Replay attack helper — record a signal (e.g. garage door tone), replay via speaker
- DTMF decoder — detect touch-tone digits in audio (useful for phone phreaking demos)

**Implementation notes:**
- Must reconfigure I2S between speaker-out and mic-in modes (different `i2s_config_t`)
- `spktest` already initialises I2S output; mic driver would need its own `init()` that tears down speaker config first
- ES7210 also needs I2C init for its registers (address 0x40) — use existing Wire on SDA=18, SCL=8
- Each new module gets its own `.cpp/.h` pair — call it `mic_manager.cpp/h`

---

## 2. Trackpad (optical)

**Hardware:**
- Small optical trackpad, interrupt on GPIO16 (`BOARD_TOUCH_INT = 16`)
- On T-Deck the keyboard MCU (I2C 0x55) also reports trackpad delta (X/Y movement) alongside key codes
- Reading pattern: same `Wire.requestFrom(0x55, n)` used by `input_handling.cpp`; trackpad bytes come in the same packet as key data

**Feature ideas:**
- Cursor navigation in tables — swipe left/right = `a`/`l` (prev/next page), tap = select
- Scroll long output — swipe up/down scrolls the text buffer
- Speed dial — fast horizontal swipe jumps multiple pages at once
- Map gesture (in `trackme`) — swipe to pan a simple 2D position history plot
- Mouse emulation — if BadUSB (DuckyScript) ever gets added, trackpad could control remote cursor via USB HID

**Implementation notes:**
- Read trackpad delta in `InputHandling::getKeyboardInput()` alongside keyboard; synthesise virtual key presses (`a`/`l`/Enter) so all existing commands benefit with zero per-command changes
- Tap detection: if delta is near zero and INT fires → treat as Enter/select
- Threshold filtering needed: small jitter deltas should be ignored (dead zone ±2)

---

## 3. Touchscreen

**Hardware confirmed:** Both T-Deck and T-Deck Plus have a **Goodix GT911** capacitive touch controller.
- Library: `TouchDrvGT911.hpp` from **SensorLib** (Lewis He / LilyGo)
- INT pin: GPIO16 (`BOARD_TOUCH_INT`), RST: not wired (-1)
- I2C: `Wire` (SDA=18, SCL=8), address: `GT911_SLAVE_ADDRESS_L`
- Up to 5 touch points supported

**Critical init sequence (from LilyGo official example):**
```cpp
#include "TouchDrvGT911.hpp"
TouchDrvGT911 touch;

// in setup(), after Wire.begin():
pinMode(BOARD_TOUCH_INT, INPUT);
delay(20);
touch.setPins(-1, BOARD_TOUCH_INT);           // RST=-1, INT=GPIO16
touch.begin(Wire, GT911_SLAVE_ADDRESS_L);
touch.setMaxCoordinates(320, 240);
touch.setSwapXY(true);                        // REQUIRED — axes are swapped
touch.setMirrorXY(false, true);               // REQUIRED — Y axis mirrored
```

**Reading touch:**
```cpp
if (touch.isPressed()) {
    int16_t x[5], y[5];
    uint8_t count = touch.getPoint(x, y, touch.getSupportTouchPoint());
    // x[0], y[0] = first touch point in screen coordinates (0..319, 0..239)
}
```

**Do NOT use LGFX's built-in touch API** (`tft.getTouch()`) — touch is read independently via SensorLib. No changes needed to `LGFX_T-Deck.h`.

**Feature ideas:**
- Tap on WiFi network in scan table → connect
- Tap on command in help list → execute it directly
- Swipe left/right → page navigation (same as a/l keys)
- Long-press → context menu (connect / deauth / eviltwin clone)
- Tap status bar icons → quick actions (toggle WiFi, show battery)
- On-screen keyboard overlay — fallback if physical keyboard fails

**Implementation plan:**
- New module `touch_manager.cpp/h` — singleton, `init()` runs the GT911 setup, `poll()` returns a `TouchEvent` (TAP / SWIPE_LEFT / SWIPE_RIGHT / SWIPE_UP / SWIPE_DOWN / NONE)
- Hook `poll()` into `InputHandling::getKeyboardInput()` — map swipes to virtual `a`/`l` key presses so all existing commands get swipe navigation with zero per-command changes
- Tap detection: single point, delta near zero within ~150ms
- Swipe detection: delta X or Y > threshold (e.g. 30px) within ~300ms

---

## Priority suggestion (when ready to build)

1. **Trackpad gestures first** — lowest effort, biggest UX win. Modify `getKeyboardInput()` to emit virtual a/l/Enter from trackpad. Every existing command benefits immediately.
2. **Microphone recorder** — self-contained new module, `audiocap` command, useful for demos.
3. **Touch** — only if hardware confirms a digitizer is present.
