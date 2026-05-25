---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

All implemented features are documented in CLAUDE.md (commands, architecture sections).

## Session 2026-05-25
- Lock screen write-block — `DisplayManager::_blocked`; all draw methods guard on it; `setBlocked(true)` after `lock()`; lock screen draw functions lift block temporarily; status bar refreshed in `refreshDuration()` while lifted
- Unlock auto-redraw — `_justUnlocked` + `consumeJustUnlocked()`; all interactive apps re-render on flag
- Backspace hold-repeat in CLI — 700ms delay, 80ms rate, backspace only; other keys cancel; `_repeatKey/_repeatStart/_repeatLast` in `InputHandling`
- WiFi wrong-password fix — `storeWiFiCredentials()` moved to success path only
- Plain-text password entry — echoes `c` directly, space restriction removed
- NTP sync fix — NTP start/stop check moved ABOVE 10s throttle in `ClockManager::update()`
- Status bar 3s live refresh — `_lastBarMs` timer in `ClockManager::update()`
- Status bar while locked — `refreshDuration()` calls `updateStatusBar()` with block lifted
- WGuard WiFi isolation — `WiFi.disconnect(false)` before `WiFi.mode(WIFI_STA)` in `run()` + `beginBackground()`
- `sdrm` renamed → `rm/rm`; autocomplete arg hints + shortName completion fixed

## Not Yet Built
- macwatch/mw — MAC proximity watchlist (spec: project_macwatch_idea.md)
- LoRa scanner (`lorascan/ls`)
- NotificationManager — standalone I2S alert module (spec: project_notification_manager.md)
- Touchscreen GT911, ES7210 mic (pins: project_future_peripherals.md)
