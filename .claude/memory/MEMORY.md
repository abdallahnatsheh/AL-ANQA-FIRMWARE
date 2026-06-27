# Memory Index

## Rules (always apply)
- [Collaboration + Coding Rules](feedback_rules.md) — no AskUserQuestion, reuse code, verify APIs, GDMA critical rule, user profile
- [UI Rules](ui_rules.md) — `[CYAN::YELLOW] 01/02` header style + cursor corruption fix pattern
- [NimBLE v2.x Rules](nimble_v2_rules.md) — scan response name, cleanup no re-init, scan non-blocking, auto-bond-delete

## State
- [Progress Log](progress_log.md) — last session 2026-06-23 (`edit`/`ed` nano-style SD editor — per-row dirty render + undo + auto-indent + trackball accel/Top/Bottom; `rm -d` recursive dir delete + dir autocomplete — all ✅tested, committed 58d1207 + pushed); not-yet-built list
- [Next Steps](next_steps.md) — priority queue: WiFi(1-9)→BT(10-12)→GPS(13-14)→USB(15-18)→Other(19-26)

## Improvement backlog (hardening — user will consider)
- [Improvement backlog](project_improvement_backlog.md) — project-wide hardening: #1 unit tests for crypto/parsers, #2 validate compiled-but-untested features, #3 extract dot11_tx/promisc shared utils, #4 finish ScopedPromiscPause migration, #5 64-cmd cap (at 59 — freed 5 via merges, added edit+macwatch+csidetect), #6 central WiFi state. NOT new features.

## Open Issues (verify / fix later)
- [ESPVoice crash watch](project_espvoice_crash_watch.md) — `ev` sometimes crashes after a couple min; PS_NONE + draw-throttle applied (unconfirmed); read RESET REASON to classify (brownout vs panic vs WDT)

## Planned features (NOT YET BUILT — design specs captured 2026-06-26; full plans in TREX_*_PLAN.md)
- [wpa3down/w3d](project_wpa3down_plan.md) — WPA3 transition-mode downgrade: RSN-IE PMF/AKM detect, WPA2-only rogue AP, EAPOL+PMKID capture → HCCAPX/HC22000. Full: TREX_WPA3_DOWNGRADE_PLAN.md
- [netspy/ns + isoscan/is](project_netspy_isoscan_plan.md) — client-isolation bypass (AirSnitch NDSS 2026): device discovery + GTK inject/gateway bounce/port stealing. Full: TREX_NETSPY_ISOSCAN_PLAN.md
- [csidetect/csi](project_csi_camdetect_plan.md) — ✅ BUILT + HW-VERIFIED 2026-06-26; PRO **WiFi motion detector** `[EXP]` (sprite ~30fps + 8 subcarrier-band sectors). **Honestly NOT a radar** (single antenna = motion energy, no bearing). Next: SD presence log/alert. camdetect DROPPED. Full: TREX_CSI_CAMDETECT_PLAN.md

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
- [macwatch/mw](project_macwatch_idea.md) — BUILT 2026-06-25, NOT yet HW-tested; "trackme-lite" dual-radio loop, presence state machine, OUI-prefix matching, hunt meter, BLE-only bg mode, reuse map, GDMA-safe gap. Also `ble_ident.h` (SIG/Continuity/AirPods device ID, used by bmon/sbl/mw)
- [Remote CLI / screen mirror](project_remote_cli_screen_mirror.md) — NOT YET BUILT; USB-CDC text mirror of DisplayManager, Flipper-CLI style
- [Future peripherals](project_future_peripherals.md) — ES7210 mic (I2S_NUM_1, both boards), GT911 touch pins
- [espvoice/ev + mictest/mt](progress_log.md) — ESP-NOW G.722 walkie-talkie + mic test; PTT toggle, coexist-resident I2S, libg722 vendored, app-local vol/gain (see 2026-06-12 in progress log)
