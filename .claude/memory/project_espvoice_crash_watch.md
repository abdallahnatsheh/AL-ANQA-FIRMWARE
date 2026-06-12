---
name: ESPVoice intermittent crash — watch / test later
description: espvoice/ev sometimes crashes after a couple minutes of sustained use; two mitigations applied, root cause not yet confirmed
type: project
---

## Status (2026-06-12)
`espvoice/ev` works great in manual testing **but sometimes crashes after a
couple of minutes** of sustained use. Core feature committed; this is an open
reliability issue to confirm/fix later.

## Mitigations already applied (in espvoice.cpp at commit time)
Two best-guess fixes for "runs fine then dies after minutes" — NOT yet confirmed
to resolve it:
1. **`esp_wifi_set_ps(WIFI_PS_NONE)`** after `WiFi.mode(WIFI_STA)` — keeps the
   radio awake for sustained 50 pkt/s ESP-NOW (modem sleep makes continuous
   ESP-NOW flaky / can wedge). Restored to `WIFI_PS_MIN_MODEM` on quit.
2. **Mic-bar redraw throttled to ~15 Hz** (was ~50 Hz, every TX frame) with peak
   accumulation — 50 Hz of `fillRect` SPI/GDMA fights WiFi + I2S DMA over a long
   session.

## How to diagnose when it next crashes — READ THE RESET REASON
Open serial monitor (115200) and reproduce. The FIRST line(s) after the reset
tell you the class of failure — each needs a different fix:

| Reset line | Cause | Direction |
|---|---|---|
| `Brownout detector was triggered` | Power — battery rail sags under audio+WiFi load | not a code bug; lower vol/gain, test on USB power vs battery |
| `Guru Meditation Error: Core N panic'ed (...)` + `Backtrace: 0x.. 0x..` | Real code crash | decode the backtrace (`addr2line` / PlatformIO monitor filter `esp32_exception_decoder`) → exact line |
| `Task watchdog got triggered ... loopTask` | A blocking call wedged | I2S `i2s_read`/`i2s_write` or ESP-NOW DMA stall — add finite timeouts |

Enable `monitor_filters = esp32_exception_decoder` in platformio.ini to get
symbolicated backtraces automatically.

## Other candidates if mitigations don't fix it
- I2S RX (mic) runs unread for minutes while in LISTEN mode — DMA keeps
  overwriting; normally harmless (no event queue registered) but worth ruling out
  by draining periodically in RX.
- `esp_now_send` at 50/s for minutes — check return for `ESP_ERR_ESPNOW_NO_MEM`
  buildup; currently fire-and-forget.
- Free-heap drift — log `ESP.getFreeHeap()` to the stats line during a long
  session to spot a slow leak (WiFi/ESP-NOW internal).

Related: [[progress_log]] (2026-06-12 session), espvoice section in CLAUDE.md.
