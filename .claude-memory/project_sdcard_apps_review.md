---
name: project-sdcard-apps-review
description: SD card usage audit across all apps — which are safe and which were fixed
metadata:
  type: project
---

Full review completed of all apps that use SD card. Status:

**SAFE (no changes needed):**
- `eviltwin.cpp` — all SD access uses `sd.isReady()` checks in `handleRoot()`, `handlePost()`, `saveCredsToSD()`, `pickSdTemplate()`
- `handshake_capture.cpp` — all SD access uses `sdCardManager.isReady()` checks before opening pcap, cracked password log, and wordlist
- `wifimon_functions.cpp` — confirmed safe (previous session)
- `hidden_ssid.cpp` — confirmed safe (previous session)

**FIXED:**
- `wifi_creds.cpp` — `appendWpaNetwork()` had no isReady check and returned void → fixed (see [[project-wpa-supplicant-sync]])
- `sdcard_manager.cpp` — 3 bugs fixed (see [[project-sdcard-fixes]])

**How to apply:** When adding new SD-using features, always gate on `sdCardManager.isReady()` at the top of any function that touches the card. Use `sd.appendLine()` or `sdCardManager.appendLine()` rather than `SD.open()` directly when possible (those already check isReady internally).
