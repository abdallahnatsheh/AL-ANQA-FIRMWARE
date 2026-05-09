---
name: UI overhaul — font 1.3×, full screen utilization
description: Plan to use full 320×240 screen with bigger font and more table rows per page
type: project
---

Scale default font from 1.0× to 1.3×, increase LINE_HEIGHT 12→14, and fix all perPage values so tables fill the whole screen instead of ~40% of it.

**Why:** At font 1.0× (6×8px) with LINE_HEIGHT=12, tables show only 6 entries per page even though the content area (202px) fits 14 rows. Looks empty and font is hard to read on a real device.

**How to apply:** When starting this work, implement all 7 changes below in one pass.

---

## Key numbers

- Screen: 320×240, content area y=38 → y=240 = 202px usable
- Font 1.3× → char size ≈ 8×10px
- LINE_HEIGHT 12 → **14** (10px char + 4px gap)
- Rows available: 202/14 = 14
- Fixed rows (title + sep + sep + nav): 4
- **Content rows per page: 10** (was 6–8)

---

## Changes (7 files)

### 1. `display_manager.h`
```
#define LINE_HEIGHT   12  →  14
```

### 2. `display_manager.cpp`
- `setDefaultTextSize()`: `tft.setTextSize(1.0, 1.0)` → `tft.setTextSize(1.3, 1.3)`
- `backspaceChar()`: `charW = (int16_t)(6 * 1.2)` → `charW = (int16_t)(6 * 1.3 + 0.5)` (= 8)

### 3. `wifi_functions.cpp`
```cpp
const int perPage = 6;  →  10
```
SSID column (14 chars) is fine at 8px/char — no width change needed.

### 4. `bluetooth_functions.cpp`
```cpp
const int perPage = 6;  →  10
" %.18s"  →  " %.9s"   // name truncation: saves 9 chars so MAC+RSSI+name fits in 310px at 1.3×
```
Full BLE line at 1.3×: [XX](4) + MAC(18) + RSSI(5) + name(10) = 37 chars × 8 = 296px ✓

### 5. `network_scanner.cpp`
```cpp
const int perPage = 8;  →  10   // line 185
```
ARP entry: [X](4) + IP(16) + MAC(18) = 38 chars × 8 = 304px — fits in 310px ✓

### 6. `utils.cpp` (help command)
```cpp
const int LINE_H = 10;  →  14
```
Remove the `displayManager.setTextSize(1.0f)` call inside the category loop — let it use default 1.3×.
At 1.3× every category (max 6 commands × 2 lines = 12) + header + nav = 14 rows → still fits one page per category.

### 7. `command_manager.cpp`
Shorten pwrsave description — only one that overflows 36 chars at 1.3×:
```
"Power save settings: pwrsave [status|on|off|set ...]"  (52 chars — TOO LONG)
→ "Power save: on/off/set/status"                        (29 chars ✓)
```

---

## Bonus: eviltwin creds nav consistency
In `eviltwin.cpp` `showCredsTable()`:
- Change `[n]next [p]prev [q]back` hint → `[a]prev [l]next [q]back`
- Change key checks `n/N` → `l/L` (next) and `p/P` → `a/A` (prev)
- `ET_CREDS_PER_PAGE = 5` stays — at 1.3× it still fits (216px < 240px)

---

## What does NOT need changing
- `esp_info.cpp` iRow labels (8 chars × 8px = 64px + values fit ✓)
- Status bar icons (pixel-drawn, not text-scaled)
- `ET_CREDS_PER_PAGE = 5` in `eviltwin.h`
- `eviltwin.cpp` cred truncation (%.38s user, %.44s pass) — typical creds are short, no real overflow risk
- All commands that just use `dm.println()` — scroll/wrap handles them

---

## Verification after flash
1. `scanwifi` — should show 10 entries/page, bigger text
2. `scanblue` — 10 entries/page, BLE name ≤9 chars visible
3. `netdiscover` — 10 entries/page, IP+MAC readable
4. `help` — all categories on one page each, text clearly bigger
5. `info` — 3 pages, labels + values not cut off
6. `et` → `[c]` creds table — 5 creds/page, a/l navigation works
