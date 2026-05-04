---
name: T-DECK Hardware Pin Map and Peripheral Status
description: Full pin mapping, which peripherals are working vs unused, and key hardware constraints
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
**Board:** LilyGo T-DECK, ESP32-S3, 16MB flash, PSRAM enabled (`-DBOARD_HAS_PSRAM`).
**Build:** PlatformIO, `env:T-Deck`, board=esp32s3box, Arduino framework.
**Power:** GPIO 10 must be set HIGH to enable all peripherals — done in `DisplayManager::init()`.

| Peripheral | Pins | Status |
|---|---|---|
| Display ST7789 (320×240) | CS=12, DC=11, BL=42, MOSI=41, MISO=38, SCK=40 | Working, LovyanGFX |
| PS/2 Keyboard via I2C | SDA=18, SCL=8, slave=0x55, INT=46 | Working |
| Battery ADC | GPIO 4 | Working |
| LoRa SX1262 | CS=9, BUSY=13, RST=17, DIO1=45, shared SPI | Unused — RadioLib ready |
| SD Card | CS=39, shared SPI | Unused |
| Touch | INT=16 | Unused |
| Audio I2S out | WS=5, BCK=7, DOUT=6 | Unused |
| ES7210 mic | MCLK=48, LRCK=21, SCK=47, DIN=14 | Unused |
| GPS | UART (module availability TBD) | Unused |
| USB HID | Native ESP32-S3 USB | Unused — BadUSB potential |

**Screen layout:** rotation=1 (landscape). Header bar 30px. Output area starts at `outputY` constant.
**SPI bus:** display, SD card, and LoRa all share the same SPI pins — CS pins differentiate them.

**How to apply:** When implementing LoRa or SD card, remember shared SPI — initialize with correct CS. When implementing BadUSB, use TinyUSB (ESP32-S3 native USB, not UART).
