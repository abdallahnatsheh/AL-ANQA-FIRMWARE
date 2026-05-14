# Memory Index

- [SD Card Bug Fixes](project_sdcard_fixes.md) — 3 bugs fixed in sdcard_manager.cpp: ready flag order, format-without-card, ready=false after format success
- [wpa_supplicant Sync Fix](project_wpa_supplicant_sync.md) — appendWpaNetwork() now returns bool, has isReady guard, and shows user feedback
- [SD Card App Audit](project_sdcard_apps_review.md) — full review of all SD-using apps; eviltwin/handshake/wifimon/hidden_ssid are safe
- [TrackMe SD Notice](project_trackme_sd_notice.md) — 3s on-screen save feedback for all 3 TrackMe SD write paths; safe with no SD card
- [USB Gadget Plan](project_usb_gadget_plan.md) — corrected TinyUSB composite plan (MSC+HID+BadUSB), NOT YET IMPLEMENTED
- [Branch State](project_branch_state.md) — current branch feature/wpa-supplicant-sync, what's done and what's pending
