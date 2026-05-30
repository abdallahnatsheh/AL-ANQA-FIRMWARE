---
title: USB Keyboard
parent: USB Gadget
nav_order: 2
---

# USB Keyboard

## `usbkbd` / `uk` — USB HID Keyboard + Mouse

Turns the T-Deck into a USB HID keyboard and mouse. Every key is forwarded to the connected PC; the trackball controls the cursor.

```
CMD> uk
```

**Backspace auto-repeat:** Hold Backspace for 1.5s → repeat at ~16 chars/sec for up to 2 seconds, then stops. A second tap while repeat is active cancels it.

### Mouse speed

| Roll speed | Step |
|------------|------|
| Very fast | 20 px |
| Fast | 13 px |
| Medium | 8 px |
| Slow | 5 px |
| Single tick | 3 px |

### Trackball center button

| Action | Result |
|--------|--------|
| Tap < 300ms | Left click |
| Hold 300ms – 1.5s | Right click |
| Hold ≥ 1.5s | Exit |
