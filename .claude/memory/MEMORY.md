# Memory Index

## Rules (always apply)
- [Collaboration + Coding Rules](feedback_rules.md) — no AskUserQuestion, reuse code, verify APIs, GDMA critical rule, user profile
- [UI Rules](ui_rules.md) — `[CYAN::YELLOW] 01/02` header style + cursor corruption fix pattern
- [NimBLE v2.x Rules](nimble_v2_rules.md) — scan response name, cleanup no re-init, scan non-blocking, auto-bond-delete

## State
- [Progress Log](progress_log.md) — last session 2026-06-12 (espvoice/ev + mictest/mt); not-yet-built list
- [Next Steps](next_steps.md) — priority queue: WiFi(1-9)→BT(10-12)→GPS(13-14)→USB(15-18)→Other(19-26)

## Open Issues (verify / fix later)
- [ESPVoice crash watch](project_espvoice_crash_watch.md) — `ev` sometimes crashes after a couple min; PS_NONE + draw-throttle applied (unconfirmed); read RESET REASON to classify (brownout vs panic vs WDT)

## Feature References (look up when touching that feature)
- [Karma/km](project_karma_plan.md) — NOT YET BUILT; design+build plan: Auto/Interactive modes, portal+WPA2-handshake bait, PNL fingerprinting (defeats MAC rand), intel cards, reactive karma
- [SSH client/ssh](project_ssh_client.md) — LibSSH-ESP32, 50KB task, colour terminal+scrollback; HW-SHA concurrency caveat
- [buddy/bd](project_buddy_port.md) — working; key NimBLE quirks
- [BLE Info/bi](project_bleinfo.md) — working; critical compile quirks
- [WGuard/wg](project_wguard_feature.md) — full IDS threat table, detection logic, save types
- [Beacon Flood/bf](project_beacon_flood.md) — 5 modes, GDMA-safe, dynamic frame builder
- [Lock screen blocking](project_lock_display_blocking.md) — setBlocked/consumeJustUnlocked per-command table
- [GPS warm start](project_gps_warmstart.md) — L76K/M10Q NVS cache, init commands
- [NotificationManager](project_notification_manager.md) — NOT YET BUILT; I2S WAV spec
- [USB Gadget](project_usb_gadget_plan.md) — MSC+HID; key SPI fixes
- [macwatch](project_macwatch_idea.md) — NOT YET BUILT; spec
- [Remote CLI / screen mirror](project_remote_cli_screen_mirror.md) — NOT YET BUILT; USB-CDC text mirror of DisplayManager, Flipper-CLI style
- [Future peripherals](project_future_peripherals.md) — ES7210 mic (I2S_NUM_1, both boards), GT911 touch pins
- [espvoice/ev + mictest/mt](progress_log.md) — ESP-NOW G.722 walkie-talkie + mic test; PTT toggle, coexist-resident I2S, libg722 vendored, app-local vol/gain (see 2026-06-12 in progress log)
