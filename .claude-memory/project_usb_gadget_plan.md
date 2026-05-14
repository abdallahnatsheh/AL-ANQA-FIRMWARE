---
name: project-usb-gadget-plan
description: USB gadget (MSC + HID + BadUSB) — corrected plan ready for implementation, not yet implemented
metadata:
  type: project
---

The corrected USB plan is in `planUsbModes.corrected.md` (old bad plans deleted).

**NOT YET IMPLEMENTED.** Waiting for user to say go.

**Key architecture decisions already settled:**
- ESP32-S3: composite device (CDC + MSC + HID) registered ONCE at boot via global constructors — no hot-switching descriptors
- Runtime control: `msc.mediaPresent(true/false)` to expose/hide SD, not USB.begin()/end()
- SD during MSC: `f_unmount("0:")` detaches FAT VFS without killing SPI driver — `sdmmc_read/write_sectors()` used for block access
- `SD.end()` is NOT called before MSC (kills card handle needed for sector reads)
- New module: `usb_gadget.cpp` / `usb_gadget.h` — singleton `UsbGadget`
- BadUSB: DuckyScript subset, step-by-step from SD via `update()` in loop(), requires explicit 'y' confirmation

**Commands to register (in "SD Card" category):**
`usbmsc/um`, `usbhid/uh`, `usbcomp/uc`, `usboff/uo`, `usbstatus/ust`, `usbexec/ux`

**Files to create:**
1. `t-deck-cli/usb_gadget.h`
2. `t-deck-cli/usb_gadget.cpp`
3. Modify: `sdcard_manager.cpp` — add `UsbGadget::getInstance().isSDAllowed()` checks
4. Modify: `command_manager.cpp` — register 6 new commands
5. Modify: `main.ino` — global UsbGadget, `init()` in setup, `update()` in loop

**Open questions before impl:**
- Does `f_unmount("0:")` keep the diskio/SPI layer alive on this ESP-IDF version?
- Does `SD.card()` pointer survive after `f_unmount()`? (It should — card struct lives in SPI host, not VFS)
- Composite USB descriptor ordering (CDC + MSC + HID global objects must be declared before setup())
