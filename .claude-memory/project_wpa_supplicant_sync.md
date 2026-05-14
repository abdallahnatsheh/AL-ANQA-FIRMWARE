---
name: project-wpa-supplicant-sync
description: wpa_supplicant.conf SD sync fix — appendWpaNetwork now returns bool with user feedback
metadata:
  type: project
---

`appendWpaNetwork()` in `wifi_creds.cpp` now returns `bool` (was `void`).

**What changed:**
- Added `#include "sdcard_manager.h"` and `extern SDCardManager sdCardManager` to `wifi_creds.cpp`
- Added `if (!sdCardManager.isReady()) return false;` as third guard in `appendWpaNetwork()`
- Dedup early-returns now return `true` (credentials ARE on SD, not an error)
- File write returns `true` on success, `false` on failure
- `wifi_creds.h` updated: `void appendWpaNetwork()` → `bool appendWpaNetwork()`
- `wifi_functions.cpp` `connectToWiFiCommand()`: checks return value, shows green "Saved to wpa_supplicant.conf" or grey "NVS only (no SD card)"

**Why:** Function was silently failing with no user feedback. User connected to new networks, saw NVS save but never knew if SD write succeeded or not.

**How to apply:** Any call site of `appendWpaNetwork()` can now check the bool return for user feedback. Dedup returning `true` means "already in file" — treat as success (network IS on SD).

Related file: [[project-sdcard-apps-review]]
