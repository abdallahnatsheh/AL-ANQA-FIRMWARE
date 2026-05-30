---
title: USB Mass Storage
parent: USB Gadget
nav_order: 1
---

# USB Mass Storage

## `usbmsc` / `um`

Mounts the SD card as a removable USB drive on the connected host PC. No drivers needed on Windows, macOS, or Linux.

```
CMD> um
```

The T-Deck screen shows the card size and waits. Use the SD card normally from the PC — read, write, and delete files.

### To exit

1. Eject the drive from the PC (safely remove hardware)
2. Press `q` on the T-Deck keyboard

### Notes

- WiFi is automatically disconnected before MSC starts (shared GDMA bus on ESP32-S3)
- All SPI CS pins are held HIGH during MSC to prevent bus corruption
- SD card is auto-remounted for T-Rex use on exit (up to 8 attempts)
- Always eject from the PC before pressing `q` — do not unplug mid-write
