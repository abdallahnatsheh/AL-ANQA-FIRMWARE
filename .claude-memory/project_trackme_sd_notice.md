---
name: project-trackme-sd-notice
description: TrackMe app SD save visual feedback — what was added and how it works
metadata:
  type: project
---

TrackMe (`trackme.cpp/.h`) now shows a 3-second on-screen notice when saving to SD.

**What was added:**
- `_sdNoticeMs` (uint32_t) timestamp in `TrackMeScanner` — set only on actual successful `sd.appendLine()` return
- `drawSdNotice(const char* msg)` helper — draws green text at row 10 of the output area
- `drawScreen()` THREAT_NONE branch: if `_sdNoticeMs` is set and < 3s ago, shows "Saved to SD: /logs/trackme.txt" in green instead of "No threats detected"
- `'s'` key: compares `_sdNoticeMs` before/after `saveLog()` — shows device count or "No SD card - nothing saved"
- `'w'` key (whitelist): same pattern — shows "Device trusted + saved to whitelist" or "Device trusted (no SD - session only)"

**Why:** `_sdNoticeMs` change-detection pattern is safe with no SD card — if `sd.appendLine()` returns false, timestamp is never updated, so no false "saved" message is shown.

**How to apply:** The `_sdNoticeMs != before` pattern (capture timestamp before, compare after) is the correct pattern for any SD write feedback in apps that use `appendLine()`. Do not call `drawSdNotice()` unconditionally.
