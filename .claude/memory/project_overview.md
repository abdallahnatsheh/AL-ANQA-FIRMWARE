---
name: T-DECK-CLI Project Overview
description: Core purpose, hardware target, and current state of the T-DECK pentesting firmware project
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
Portable pentesting firmware for LilyGo T-DECK and T-DECK Plus (ESP32-S3). Ethical hacking cyber-deck with physical keyboard, TFT display, WiFi, BLE, LoRa, GPS, SD card, I2S audio, and native USB HID.

**Why:** Educational/ethical hacking tool for learning networking, Bluetooth, GPS, BadUSB, and LoRa on a handheld device.

**Current state (as of 2026-05-02):**
- Functional CLI shell with command registration system
- WiFi scan, connect, credential persistence, ping-based host discovery, TCP port scan
- BLE device scan with pagination
- Battery monitoring, device info, matrix animation
- Many hardware peripherals unused: LoRa, SD, GPS, BadUSB/HID, audio, touch

**Active branch:** `feature/pentest-enhancements` — all new work goes here.

**How to apply:** Every suggestion should account for the ESP32-S3 constraints (no OS, single-threaded Arduino loop, ~320KB SRAM usable, shared SPI bus for display/SD/LoRa).
