# Memory Index

## Rules (always apply)
- [Collaboration + Coding Rules](feedback_rules.md) — no AskUserQuestion, reuse code, verify APIs, GDMA critical rule, user profile
- [User compiles manually](feedback_user_compiles_manually.md) — do NOT run pio builds; static-review then hand off; CI is the gate
- [UI Rules](ui_rules.md) — `[CYAN::YELLOW] 01/02` header style + cursor corruption fix pattern
- [NimBLE v2.x Rules](nimble_v2_rules.md) — scan response name, cleanup no re-init, scan non-blocking, auto-bond-delete

## State
- [Progress Log](progress_log.md) — **2026-07-05: isoscan/is Stage 2** — targeting + confirm-before-fire + **CCMP crypto ✅** + **GTK inject primitive ✅ HW-PROVEN end-to-end** (victim decrypts+replies via tcpdump). Gateway ARP-poison lands but a **mobile hotspot L3-routes past it** (0% ping loss w/ poison held) → traffic INTERCEPTION needs a REAL router w/ client isolation (hotspot is wrong test bed); RA DNS poison deferred (IPv6/UDP lift + needs proper isolated router). Delivery ~1/20s (DTIM-capped broadcast). Prior **2026-07-04: undercover PANIC BUTTON ✅** (`uc panic set|off`, `panic_key` default `@`, instant-hide from anywhere incl. mid-command; re-entrant hook in getKeyboardInput; cover-exit `signalRedraw()` fixes mid-command repaint). Undercover now feature-complete (Phase 2 + boot-cover 044d860 + panic). Decoy/duress + freeze-TX ops-policy = user DROPPED. Prior: mc/NimBLE spoof fix (89765e0/6a50e50)

## Done
- [Undercover mode + touchscreen](PLAN-undercover-touch.md) — Phase 0 ✅ Phase 1a ✅ Phase 2 ✅ boot-cover ✅ **panic-button ✅ (2026-07-04)** — ALL HW-verified. Touch, Notes cover UI, secret-passphrase exit, SD-backed notes + cursor editor, boot-cover, and instant-hide `panic_key` (default `@`, re-entrant hook in getKeyboardInput, cover-exit `signalRedraw()` repaints mid-command apps). **Decoy/duress passphrase + freeze-transmitters ops-policy = user DROPPED as not useful.** Feature-complete for the user. Optional polish only (ux/portal-picker repaint). Full detail: progress_log 2026-07-04.
- [Next Steps](next_steps.md) — priority queue: WiFi(1-9)→BT(10-12)→GPS(13-14)→USB(15-18)→Other(19-26)

## Improvement backlog (hardening — user will consider)
- [Improvement backlog](project_improvement_backlog.md) — project-wide hardening: #1 unit tests for crypto/parsers, #2 validate compiled-but-untested features, #3 extract dot11_tx/promisc shared utils, #4 finish ScopedPromiscPause migration, #5 64-cmd cap (at 59 — freed 5 via merges, added edit+macwatch+csidetect), #6 central WiFi state. NOT new features.

## Open Issues (verify / fix later)
- [ESPVoice crash watch](project_espvoice_crash_watch.md) — `ev` sometimes crashes after a couple min; PS_NONE + draw-throttle applied (unconfirmed); read RESET REASON to classify (brownout vs panic vs WDT)
- [Bluedroid/BLE-Mesh RAM+flash leak](project_bluedroid_ram_flash_leak.md) — **FIXED + HW-VERIFIED 2026-07-02**: `mac_changer.cpp` swapped legacy Bluedroid GAP calls for NimBLE-native (`setOwnAddrType`/`setOwnAddr`); saved 19.7KB RAM + 428KB flash on T-Deck-Plus. User confirmed `mc random`/`mc status` now actually changes the BLE MAC on hardware (old code was dead — Bluedroid never enabled elsewhere, so `mc target bt` silently no-op'd before). Note: `bk`/`bd` hardcode their own stable BLE address and can't be used to test this over the air (see file for why).

## Planned features (NOT YET BUILT — design specs captured 2026-06-26; full plans in TREX_*_PLAN.md)
- [wpa3down/w3d](project_wpa3down_plan.md) — WPA3 transition-mode downgrade: RSN-IE PMF/AKM detect, WPA2-only rogue AP, EAPOL+PMKID capture → HCCAPX/HC22000. Full: TREX_WPA3_DOWNGRADE_PLAN.md
- [netspy/ns + isoscan/is](project_netspy_isoscan_plan.md) — Stage 0+1+**1b ✅DONE & HW-verified** (committed, unpushed). `ns` = PASSIVE client-isolation recon (AirSnitch): sniffs AP-relayed group frames (assoc'd ESP32 HW decrypts them → no CCMP needed), parses ARP/IPv4/DHCP/mDNS/SSDP → IP+name+services; Enter/`[i]` detail; `[p]`/`[o]` + `ps ns#`/`pg ns#` probe discovered devices; GTK at `gWpaSm+0x174` (platform pinned `espressif32@7.0.1`). **NEXT = Stage 2: active `isoscan`/`is` GTK-inject** (separate opt-in command, transmits). **Victim-targeting design captured 2026-07-05** (reuse netspy `#` index/`ns#` prefix + `s_dev[]`; export MAC+name from netspy.h; CLI `is ns#` + in-app picker + confirm-before-fire; resolve gateway MAC for `bounce`). See this file's BUILD STATUS + "Stage 2 VICTIM TARGETING design".
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
