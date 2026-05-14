---
name: project-sdcard-fixes
description: SD card bugs that were found and fixed — what changed and why
metadata:
  type: project
---

Three SD card bugs were fixed in `t-deck-cli/sdcard_manager.cpp`:

1. **`ready` flag set too late** — `begin()` called `ensureDir()` before setting `ready = true`, and `ensureDir()` gates on `!ready`. Fix: move `ready = true` to immediately after `SD.begin()` succeeds.

2. **Format with no SD card** — `formatSDCard()` never checked `if (!ready)` at the top, so it ran the format UI even with no card inserted. Fix: guard added.

3. **ls says "no SD" after format** — `formatSDCard()` had `ready = false` in the SUCCESS branch (ChatGPT bug), undoing what `performFormat()` had just set. Fix: removed that line.

**Why:** `ensureDir()` uses `if (!ready) return false` internally. So setting `ready` after calling `ensureDir()` meant all directory creation was silently skipped, and all subsequent SD commands reported "no SD card."

**How to apply:** If adding new SD operations, always check `sdCardManager.isReady()` before touching the card. Never set `ready = false` after a successful operation.

Also removed bad platformio.ini flags: `-DFF_FS_FATFS=1 -DFF_FS_EXFAT=0 -DFF_USE_MKFS=1` — these don't configure the pre-compiled ESP-IDF FatFs and do nothing.

`performFormat()` uses `f_unmount("0:")` + `f_mkfs("0:", FM_FAT32, 0, work, 4096)` — the correct approach for ESP-IDF 4.x. `SD.card()` and `esp_vfs_fat_sdcard_format` are NOT available in ESP-IDF 4.x.
