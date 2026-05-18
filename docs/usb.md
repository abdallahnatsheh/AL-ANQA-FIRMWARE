# USB Gadget

T-Rex can present the T-Deck as two different USB devices to a connected PC. Both modes are enabled at boot and selected by command — no reflashing required.

---

## Commands

| Command | Short | Description |
|---------|-------|-------------|
| `usbmsc` | `um` | Expose SD card as a USB Mass Storage drive |
| `usbkbd` | `uk` | T-Deck as USB keyboard + mouse |

---

## USB Mass Storage — `usbmsc` / `um`

Mounts the SD card as a removable drive on the host PC. No drivers needed on Windows, macOS, or Linux.

**Usage:**
```
um
```

The T-Deck screen shows the card size and waits. The SD card appears on the PC as a removable disk — read, write, and delete files normally.

**To exit:**
1. Eject the drive from the PC first (safely remove hardware)
2. Press `q` on the T-Deck keyboard

**Notes:**
- WiFi is automatically disconnected before MSC starts (shared GDMA bus on ESP32-S3)
- All three SPI CS pins (display, SD, LoRa radio) are held HIGH during MSC to prevent bus corruption
- After exit, the SD card is automatically remounted for T-Rex use (up to 8 attempts)
- Do not unplug the cable mid-write — always eject from the PC side first

---

## USB Keyboard + Mouse — `usbkbd` / `uk`

Turns the T-Deck into a full USB HID input device. The physical keyboard types into the host and the trackball controls the mouse cursor.

**Usage:**
```
uk
```

### Keyboard

Every key on the T-Deck keyboard is forwarded to the host as a USB HID keystroke. The keyboard controller handles shift and modifier state internally, so capitals, symbols, and numbers all work as expected.

Special keys:

| T-Deck key | Sent to host |
|------------|-------------|
| Backspace | Backspace |
| Enter | Return |
| Tab | Tab |
| ESC | Escape |
| DEL | Delete |

**Backspace auto-repeat:** Hold Backspace — after 500 ms it starts deleting at 60 ms intervals (≈16 chars/sec) for up to 2 seconds. Press Backspace again to continue deleting.

### Mouse

Roll the trackball to move the cursor. Speed is accelerated based on how fast you spin:

| Roll speed | Cursor step |
|------------|-------------|
| Very fast | 20 px/tick |
| Fast | 13 px/tick |
| Medium | 8 px/tick |
| Slow | 5 px/tick |
| Single tick | 3 px/tick |

### Trackball center button

| Action | Result |
|--------|--------|
| Quick tap (< 300 ms) | Left click |
| Hold 300 ms – 1.5 s, release | Right click |
| Hold ≥ 1.5 s | Exit KBD mode |

### Exiting

Hold the trackball center button for 1.5 seconds. The T-Deck returns to the command screen.

---

## Technical Notes

- Both MSC and HID descriptors are registered with TinyUSB at boot (`usbManager.begin()`) — the host sees a composite device
- MSC and KBD modes are mutually exclusive at runtime but share the same USB connection
- USB keyboard+mouse is implemented in `usb_keyboard.h` / `usb_keyboard.cpp` (`UsbKeyboard` class)
- USB mass storage is implemented in `usb_manager.h` / `usb_manager.cpp` (`USBManager` class)
