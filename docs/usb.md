---
title: USB Gadget
nav_order: 9
has_children: true
lang: en
---

# USB Gadget

Al-Anqa can present the T-Deck as USB devices to a connected PC. All modes are enabled at boot — no reflashing required. Each tool has its own page (left) with full detail.

> MSC and KBD/BadUSB modes are mutually exclusive at runtime, but share the same USB connection.

---

| Tool | Command | What it does |
|------|---------|--------------|
| [USB Mass Storage](usbmsc) | `usbmsc` · `um` | Expose the SD card as a removable USB drive |
| [USB Keyboard](usbkbd) | `usbkbd` · `uk` | T-Deck as a USB keyboard + mouse |
| [BadUSB](usbexec) | `usbexec` · `ux` | DuckyScript keystroke injection — over **USB** or **BLE HID** |
| [Mouse Jiggler](jiggle) | `jiggle` · `jg` | Keep the host awake / prevent screen lock |
