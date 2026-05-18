# USB Gadget

T-Rex can present the T-Deck as two different USB devices to a connected PC. Both modes are enabled at boot and selected by command — no reflashing required.

---

## Commands

| Command | Short | Description |
|---------|-------|-------------|
| `usbmsc` | `um` | Expose SD card as a USB Mass Storage drive |
| `usbkbd` | `uk` | T-Deck as USB keyboard + mouse |
| `usbexec` | `ux` | BadUSB — execute a DuckyScript payload |

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

---

## BadUSB / DuckyScript — `usbexec` / `ux`

Executes keystroke injection scripts (DuckyScript) against the connected PC. Compatible with Flipper Zero DuckyScript v1 format.

**Usage:**
```
ux demo                      # built-in demo — opens Notepad and draws T-Rex ASCII art
ux /badusb/payload.txt       # run a script from SD card
```

Scripts live in `/badusb/` on the SD card (auto-created on first boot).

### Supported commands

| Command | Description |
|---------|-------------|
| `REM` / `//` | Comment — ignored |
| `DELAY <ms>` | Wait ms milliseconds |
| `DEFAULT_DELAY <ms>` | Delay applied after every line |
| `STRING <text>` | Type text |
| `STRINGLN <text>` | Type text + Enter |
| `REPEAT <n>` | Repeat previous line n times |
| `STRING_DELAY <ms>` | Per-character delay for next STRING only |
| `DEFAULT_STRING_DELAY <ms>` | Per-character delay for all STRINGs |
| `WAIT_FOR_BUTTON_PRESS` | Pause until trackball click |
| `ENTER` `BACKSPACE` `TAB` `ESC` `DELETE` | Special keys |
| `SPACE` `HOME` `END` `INSERT` `PAGEUP` `PAGEDOWN` | Navigation keys |
| `UP` `DOWN` `LEFT` `RIGHT` (or `UPARROW` etc.) | Arrow keys |
| `CAPSLOCK` `F1`–`F24` | Lock and function keys |
| `GUI` `CTRL` `ALT` `SHIFT` | Modifiers (space-separated) |
| `CTRL-ALT` `GUI-SHIFT` `CTRL-SHIFT` etc. | Hyphenated combos (Flipper Zero format) |

### Modifier combos

Two formats are supported — both are equivalent:

```
CTRL ALT DELETE          # space-separated
CTRL-ALT DELETE          # hyphenated (Flipper Zero format)
```

Supported hyphenated combos: `CTRL-ALT`, `CTRL-SHIFT`, `CTRL-GUI`, `CTRL-ESCAPE`, `ALT-SHIFT`, `ALT-GUI`, `GUI-SHIFT`, `GUI-SPACE`, `CTRL-ALT-SHIFT`, `CTRL-ALT-GUI`, `ALT-SHIFT-GUI`, `CTRL-SHIFT-GUI`

### Example script

```
REM Open Notepad and type a message
DEFAULT_DELAY 100
GUI r
DELAY 500
STRING notepad
ENTER
DELAY 1200
STRING Hello from T-Rex!
ENTER
REPEAT 2
```

### Abort

Press `q` on the T-Deck keyboard at any point — script stops at the next `DELAY` boundary.

### WAIT_FOR_BUTTON_PRESS

Script pauses until the trackball center button is clicked. Useful for multi-stage payloads that need manual confirmation between steps.

---

## Technical Notes

- MSC, KBD, and BadUSB HID descriptors are all registered with TinyUSB at boot — the host sees a composite device
- MSC and KBD/BadUSB modes are mutually exclusive at runtime but share the same USB connection
- USB keyboard+mouse is implemented in `usb_keyboard.h` / `usb_keyboard.cpp` (`UsbKeyboard` class)
- BadUSB is implemented in `bad_usb.h` / `bad_usb.cpp` (`BadUsb` class) with its own `USBHIDKeyboard` instance
- USB mass storage is implemented in `usb_manager.h` / `usb_manager.cpp` (`USBManager` class)
