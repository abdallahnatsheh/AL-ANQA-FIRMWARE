---
name: project-remote-cli-screen-mirror
description: Planned feature — USB-CDC remote CLI that mirrors T-Deck screen output to a PC terminal in real time (Flipper Zero CLI style)
metadata:
  type: project
---

# Remote CLI / Screen Mirror over USB-CDC (idea, not yet built)

## Goal
Connect T-Deck to a PC via USB and open a normal terminal (`screen`/PuTTY/minicom) to:
1. Type commands from the PC that run through the existing `command_manager` (same as on-device keyboard input)
2. See the device screen's text content mirrored live in that same terminal, in real time, alongside the device's own ST7789 display

Modeled directly on Flipper Zero's CLI — it's plain serial-over-USB, not pixel/graphics streaming.

## Chosen approach: text mirroring, not framebuffer streaming
- `DisplayManager` is already the single chokepoint for all on-screen text/table/status output across every command.
- Add a "mirror" hook there: when remote-CLI mode is active, every print/clear/update call also emits the equivalent text (+ANSI for color/highlight if useful) to USB-Serial.
- Input side: bytes from USB-Serial feed into the same `executeCommand()` path used by `getKeyboardInput()` — no separate parser needed.
- Tradeoff accepted: graphics/icons/sprites (buddy pet, lock-screen ASCII art, highlight colors) won't be pixel-accurate over serial — fine, since most commands are text/table based anyway.

## Transport: native ESP32-S3 USB CDC
- ESP32-S3 has native USB (TinyUSB) — CDC-ACM serial works without external USB-UART bridge, full-speed USB bandwidth (not baud-limited).
- Espressif's documented pattern: UART for debug logs, native USB CDC for app I/O — matches this firmware's "no Serial.println for secrets" rule (CDC channel would be the *intentional* output channel, separate from debug UART).

## Known integration wrinkle
- T-Deck already uses USB for BadUSB/MSC gadget modes (HID/MSC) — see [[project_usb_gadget_plan]].
- TinyUSB supports **composite devices** (CDC + HID/MSC together), so this is solvable, but USB descriptor setup must be coordinated so "remote CLI" mode doesn't collide with BadUSB/usbmsc when those are active. Likely need a single composite descriptor built at boot, with CDC always present and HID/MSC enabled only when those commands run.

## Proposed command
- `remotecli` / `rcli` (System category) — toggles mirror mode on/off, one-liner registration like other commands per `setupCommands()` convention.
- Background-friendly: similar to `ec bg` pattern — hook into the main loop's existing keyboard-poll cycle to also poll USB-Serial RX.

## Reference material
- Flipper Zero CLI docs: https://docs.flipper.net/zero/development/cli (plain serial CLI, `screen`/PuTTY at fixed baud)
- Espressif USB OTG Console guide (ESP32-S3 native CDC pattern): https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-otg-console.html
- arduino-esp32-tft-terminal (closest hobby precedent for PC<->TFT sync over USB): https://github.com/pbauermeister/arduino-esp32-tft-terminal

## Status
Idea only — not started. Next step when picked up: confirm USB composite descriptor approach with BadUSB/usbmsc code, then prototype the DisplayManager mirror hook.
