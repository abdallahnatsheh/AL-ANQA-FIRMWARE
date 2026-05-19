# Memory Index

- [Reuse existing code](feedback_reuse_existing_code.md) — Call existing methods, never reimplement
- [Cyberpunk header style](feedback_ui_header_style.md) — `[CYAN::YELLOW]  01/02` on every command screen
- [ESP32-S3 WiFi/SD GDMA](feedback_esp32s3_wifi_sd_gdma.md) — CRITICAL: disconnect(false), WIFI_STA not WIFI_OFF, SD.begin(39) after BLE deinit
- [trackme algorithm](project_trackme_feature.md) — Gates, scoring, GPS movement, WiFi-only cap, Gate3 required for WARNING/ALERT
- [eviltwin](project_eviltwin_feature.md) — MAC strategy, deauth, captive portal routes, credential table keys
- [macwatch idea](project_macwatch_idea.md) — Future: WiFi+BLE MAC watchlist, proximity alert
- [GPS Manager](project_gps_manager.md) — FreeRTOS task, init order, M10Q detection, status bar icon
- [PowerSave Manager](project_powersave_manager.md) — Inactivity+battery dim, hooked into getKeyboardInput(), /pwrsave.conf
- [Future peripherals](project_future_peripherals.md) — ES7210 mic, trackpad I2C 0x55, GT911 touch (swapXY+mirrorY)
- [hiddenssid](project_hiddenssid.md) — IRAM_ATTR chain, subtype 0/5 filter, SD dedup, ~name in cyan
- [WPA2 handshake capture](project_handshake_capture.md) — GDMA-safe pcap, buffer in RAM, write after teardown, on-device crack
- [BLE spam](project_ble_spam.md) — bs: Apple/Android/MS/Samsung; bsReinit/bsRestoreStack; min interval 32; Android 16 no ad-only popup
- [Fast Pair attack](project_fastpair_module.md) — fp: scan (TaskManager sub-task), spam (MAC churn), GATT WhisperPair (CVE-2025-36911)
- [Buddy module](project_buddy_module.md) — NimBLE init pattern, popup flow, 19 species, NVS stats, hint truncation is desktop-side
- [Shell UX](project_shell_ux.md) — ls/cd/CWD, history ring buffer, Sym+K autocomplete, CompType per command, matchesCmd dispatch fix
- [bmon idea](project_bmon_feature.md) — Future: passive BLE ad sniffer, cleartext detector, iBeacon/Eddystone parser, PCAP to SD (linktype 251); cannot sniff connected sessions (needs Ubertooth)
- [wguard idea](project_wguard_feature.md) — Future: WiFi IDS — deauth flood, evil twin, handshake harvest, PMKID grab, auth flood, beacon flood detection; NOT new-client (MAC randomization = all false positives)
- [NotificationManager](project_notification_manager.md) — Future: standalone reusable module, I2S tones, volume, /notif.conf, wake callback; integrate into buddy (popups), trackme (gate alerts), wguard (threats), hiddenssid, wpasniff; add forceWake() to PowerSaveManager
