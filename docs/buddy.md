---
title: Claude Desktop Buddy
nav_order: 6
---

# Claude Desktop Buddy

**Control Claude Desktop from your T-Deck — approve permission prompts, watch live session stats, and raise an ASCII pet.**

---

## `buddy` / `bd` — Claude Desktop Remote

```
CMD> buddy
CMD> bd
CMD> bd MyT-Rex    # custom BLE advertising name
```

Connects the T-Deck to [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) over BLE Nordic UART Service (NUS). Once linked, Claude Desktop streams live session data and pending permission prompts to the device. You approve or deny from the keyboard — no phone, no desktop interaction needed.

**Requires:** Claude Desktop with Developer mode → Hardware Buddy enabled.

---

## Screen Layout

The display splits into two panels:

```
┌─────────────────────┬────────────────┐
│  Left panel (160px) │  Right panel   │
│  Stats + controls   │  ASCII pet     │
│                     │  animation     │
└─────────────────────┴────────────────┘
```

**Left panel — top to bottom:**

| Section | Content |
|---------|---------|
| Header | Species name (left) · BLE status (right) |
| mood | 4 tiny hearts — how fast you approve prompts |
| fed | 10 circles — token progress toward next level |
| energy | 5 bars — drains over time while `bd` is running |
| Level badge | `Lv N` in species body color |
| approved | Lifetime count of approved permission requests |
| denied | Lifetime count of denied requests |
| napped | Total time spent inside `bd` sessions |
| tokens | Cumulative lifetime output tokens |
| today | Tokens used in the current Claude Desktop session |
| HUD | Approval prompt **or** idle status message |

**Right panel:** One of 18 ASCII animal species, animating at 5 fps. Species changes based on what Claude is doing (sleeping, busy, attention, celebrating, etc.).

---

## Keys

| Key | Action |
|-----|--------|
| `y` | Approve the pending permission prompt |
| `n` | Deny the pending permission prompt |
| `space` | Cycle to the next animal species |
| `q` | Quit and return to the CLI |

---

## Approval Flow

When Claude Desktop needs permission (file write, bash command, etc.) the HUD area shows:

```
approve? 8s          ← timer; turns red after 10s
read_file            ← tool name (large if short)
/path/to/file        ← hint
[y] approve   [n] deny
```

The timer counts up from when the prompt arrived. If you take longer than 10 seconds it turns red — same as the original M5StickCPlus behavior. Pressing `y` or `n` sends the decision to Claude Desktop instantly over BLE.

---

## Stats Explained

### mood — 4 hearts

Tracks how quickly you respond to permission prompts. Every `y` press records how many seconds elapsed since the prompt appeared. The median of your last 8 responses sets the base tier:

| Response time | Hearts |
|---------------|--------|
| Under 15s | 4 (max) |
| Under 30s | 3 |
| Under 60s | 2 |
| Under 2 min | 1 |
| Over 2 min | 0 |

If your denial rate exceeds 33%, mood loses 1 tier. If you deny more than you approve, it loses 2 tiers.

### fed — 10 circles

Token-driven. Claude Desktop sends its cumulative output token count each heartbeat. The fed bar shows progress toward the next level — every 5,000 tokens fills one pip, 50,000 tokens = full bar = **level up**. On level-up the pet plays its celebrate animation and the bar resets.

### energy — 5 bars

Starts at 5/5 (full) every time you open `bd`. Drains 1 bar every 2 hours while the command is running. Closing and reopening `bd` restores it to full — each session start counts as a "wake".

### Lv N — level

`lifetime_tokens ÷ 50,000`. Increases forever. Level 1 = 50K tokens, Level 2 = 100K, and so on.

### approved / denied

Simple lifetime counts of every `y` and `n` you've pressed. Both affect mood calculation.

### napped

Total wall-clock time you've spent inside the `bd` command across all sessions. Accumulates on exit and saves to NVS.

### tokens / today

**tokens** — lifetime cumulative output tokens since you first ran `bd`. Drives level and fed.

**today** — live count from the current Claude Desktop session. Resets when Claude Desktop's bridge restarts. Not persisted.

---

## Persistence

Stats are stored in NVS (survives power cycles) under the `buddy` namespace:

| Stat | Persisted |
|------|-----------|
| tokens (lifetime) | Yes |
| level | Yes (derived) |
| approvals | Yes |
| denials | Yes |
| mood history (velocity ring) | Yes |
| napped | Yes |
| energy | No — resets to 5/5 on each `bd` open |
| today tokens | No — live from bridge |

Stats save immediately on significant events (approval, denial, level-up) and on exit. NVS wear is minimal — no timer-based saves.

---

## Animal Species

18 species port from the official [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) project. Each has 7 animated states:

| State | Trigger |
|-------|---------|
| sleep | Not connected to Claude Desktop |
| idle | Connected, nothing active |
| busy | 3+ sessions running simultaneously |
| attention | Permission prompt waiting |
| celebrate | Recently completed task · level-up |
| dizzy | (reserved) |
| heart | Just approved a prompt within 5s |

Press `space` to cycle through all 18 species. The starting species is random on each `bd` launch.

**Species list:** axolotl · blob · cactus · capybara · cat · chonk · dragon · duck · ghost · goose · mushroom · octopus · owl · penguin · rabbit · robot · snail · turtle

---

## Setup

1. Install [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) on your desktop.
2. In Claude Desktop → **Developer** → **Hardware Buddy** → enable.
3. On T-Deck, run `bd` — it advertises as `Claude-XXXX` (last 2 bytes of BT MAC).
4. Claude Desktop auto-connects via BLE scan.

The T-Deck advertises continuously. Claude Desktop reconnects automatically after a disconnect.

---

## Technical Details

| Property | Value |
|----------|-------|
| Transport | BLE Nordic UART Service (NUS) |
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Protocol | Line-delimited JSON |
| BLE stack | NimBLE 1.4.x |
| NVS namespace | `buddy` |
| Security | Plain NUS (no bonding/passkey) |

**Known quirk:** On a cold boot (BLE never initialized), run `sbl` once before `bd` to warm up the BLE controller — or just wait for the 200ms init delay added to `bd` which handles this automatically.

---

## Source Files

| File | Purpose |
|------|---------|
| `t-deck-cli/buddy.cpp` | Command entry point, NimBLE NUS server, stats engine, display |
| `t-deck-cli/buddy.h` | Command declaration |
| `t-deck-cli/buddy_common.h` | Shared geometry, colors, Species struct for species files |
| `t-deck-cli/M5StickCPlus.h` | Compatibility shim (`typedef LGFX_Sprite TFT_eSprite`) |
| `t-deck-cli/buddies/*.cpp` | 18 species animation files (ported unchanged from official repo) |
