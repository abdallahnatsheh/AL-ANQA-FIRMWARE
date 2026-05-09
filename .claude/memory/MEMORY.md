# Memory Index

- [Reuse existing code before creating new implementations](feedback_reuse_existing_code.md) — Never duplicate logic; if a method already exists in the class, call it instead of reimplementing the formula
- [trackme feature + hardware discoveries](project_trackme_feature.md) — Full algorithm: baseline, whitelist, GPS movement, Gate3 GPS path, WiFi-only cap, 30s gap min, WARNING only after Gate3
- [eviltwin feature + pending roadmap](project_eviltwin_feature.md) — Evil Twin AP: clone+MAC spoof+deauth, credential table (portal stays running), captive portal cross-platform fix, Windows behaviour
- [macwatch feature idea](project_macwatch_idea.md) — WiFi probe + BLE MAC watchlist: scan MACs, label them, alert when they enter range (proximity awareness, boss detector)
- [GPS Manager module](project_gps_manager.md) — Background FreeRTOS GPS task: init order, M10Q detection fix, status bar icon, trackme integration
- [PowerSave Manager module](project_powersave_manager.md) — Inactivity dim + battery-aware dim, hooked into getKeyboardInput() globally, SD config /pwrsave.json
- [UI overhaul plan — font 1.3×, full screen](project_ui_overhaul.md) — 7-file change: LINE_HEIGHT 12→14, font 1.0→1.3×, perPage 6→10, BLE name truncation, help layout, pwrsave desc, eviltwin nav a/l
- [Future peripherals — mic, trackpad, touch](project_future_peripherals.md) — ES7210 mic (I2S MCLK=48), trackpad (I2C 0x55 + INT=16), GT911 touchscreen (SensorLib TouchDrvGT911, I2C GT911_SLAVE_ADDRESS_L, swapXY+mirrorY required)
