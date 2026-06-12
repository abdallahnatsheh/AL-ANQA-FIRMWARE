---
name: project-usb-gadget-plan
description: USB MSC+HID composite — working on feature/usb-msc-hid, NOT merged to main
---

**Branch:** `feature/usb-msc-hid` — NOT yet merged into `feature/pentest-enhancements`.

**Working:** composite USB MSC + HID device; write 2MB via USB MSC, unplug, `ls` — all pass.

**Key fixes that made it work:**
1. `RADIO_CS_PIN` (GPIO9) must be held HIGH before MSC — LoRa SPI was corrupting MISO
2. Queue-based SD I/O: TinyUSB callbacks post to `s_ioQ`, main task services via `serviceMscIO()` — prevents LovyanGFX/Arduino SD SPI conflict
3. SD retries: reads 10×20ms, writes 15×100ms
4. `sdSpiReset()`: 80 SPI clocks with CS HIGH, then 8×500ms remount attempts

**SPI CS pins:** display=GPIO12, SD=GPIO39, LoRa=GPIO9.

**Still pending in this branch:** BadUSB/DuckyScript (`usbexec/ux`).
