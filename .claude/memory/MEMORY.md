# Memory Index

- [Collaboration Rules](feedback_rules.md) — no AskUserQuestion, no git push, no license text, reuse code, no redundant verification
- [User Profile](user_profile.md) — Abdallah, embedded/ESP32 dev, concise communication, file:line refs
- [Progress Log](progress_log.md) — done vs not-yet-built feature checklist; last session 2026-05-25: btkbd/bk BLE keyboard+mouse (⚠️ not field tested); buddy security fix; lock-screen write-lock + auto-redraw; backspace repeat; wifi/clock/statusbar fixes
- [Next Steps](next_steps.md) — priority ordered: Input(0a,0b)→WiFi(1-13)→BT→GPS→Other→LowPri — 31 items; new: LAN MITM(#5)+AP Bridge(#6); T-Deck=433MHz/Plus=915MHz LoRa; BT relay=inter-device channel
- [macwatch spec](project_macwatch_idea.md) — WiFi probe + BLE MAC watchlist, proximity alert
- [Future peripherals](project_future_peripherals.md) — ES7210 mic, trackpad, GT911 touch — pins + priority
- [Buddy port plan](project_buddy_port.md) — full self-contained spec: NimBLE GATT server port, wire protocol, TamaState struct, exact API mapping, T-DECK UI, impl order
- [BLE Info](project_bleinfo.md) — bleinfo/bi: compiled NOT YET FIELD TESTED; critical quirks: s_loadedPkts order, off-by-one in s_writable, pairing callback return type
- [GPS Warm Start](project_gps_warmstart.md) — NVS lat/lon cache; L76K BeiDou/SBAS; M10Q MGA-INI inject; NVS namespace "gps"
- [USB MSC+HID](project_usb_gadget_plan.md) — working on feature/usb-msc-hid (NOT merged); queue-based SD I/O; RADIO_CS_PIN fix; BadUSB pending
