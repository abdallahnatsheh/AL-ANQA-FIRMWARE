---
name: Next Steps
description: Ordered feature queue for upcoming sessions
type: project
---

1. **WPS flag in scanwifi** — detect WPS IE (tag 0xDD OUI 00:50:F2:04) in beacon frames during scan, show cyan `[WPS]` tag in scan table.

3. **macwatch** — see `project_macwatch_idea.md` for full spec. Command: `macwatch/mw`.

4. **BLE GATT enumeration** — `NimBLEClient` connect to MAC from last `scanblue`, walk `getServices()→getCharacteristics()`, print UUID + properties + readable values. Command: `bleinfo/bi <index>`.

5. **LoRa scanner** — SX1262 (CS=9, BUSY=13, RST=17, DIO1=45, shared SPI), receive mode, hop 868/915 MHz, log to `/logs/lora.csv`. Command: `lorascan/ls`.

6. **BadUSB** — TinyUSB HID, parse DuckyScript from SD `/scripts/*.duck`. Command: `badusb/bu <script>`.

