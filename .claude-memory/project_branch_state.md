---
name: project-branch-state
description: Current branch state and what work is done vs pending
metadata:
  type: project
---

**Current branch:** `feature/wpa-supplicant-sync`

**Completed this session (committed):**
- SD card 3-bug fix: ready flag order, format-without-card guard, ready=false after format success
- Removed bad platformio.ini FatFs flags
- `performFormat()` full rewrite for ESP-IDF 4.x compatibility
- TrackMe SD save visual notice (3s on-screen feedback for all 3 save paths)
- Corrected USB gadget plan in `planUsbModes.corrected.md`
- Deleted old bad USB plans
- `appendWpaNetwork()` returns bool + user feedback in connect command
- SD card app audit (eviltwin, handshake, wifimon, hidden_ssid all confirmed safe)

**Pending / not started:**
- USB gadget implementation (plan ready in `planUsbModes.corrected.md`)
- BLE GATT enumeration (`bleinfo/bi <mac>`) — mentioned in CLAUDE.md pending features
- WPA handshake capture to SD (EAPOL → `.cap`) — `handshake_capture.cpp` exists but may need review
- LoRa scanner / packet logger
- macwatch — MAC watchlist with proximity alert

**How to apply:** Resume on `feature/wpa-supplicant-sync`. If USB gadget is next, read `planUsbModes.corrected.md` fully before implementing.
