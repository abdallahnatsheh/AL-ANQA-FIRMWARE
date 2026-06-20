---
name: project-lock-display-blocking
description: Lock screen display blocking and app restore on unlock
type: project
---

Lock screen blocks all DisplayManager output while locked, and restores the open app automatically on unlock.

**How to apply:** Any new interactive command with a static UI needs `consumeJustUnlocked()` in its inner wait loop. Any command with a timed redraw needs `isBlocked()` guard before drawing.

## DisplayManager blocking
- `setBlocked(true/false)` gates all output methods (`printText`, `println`, `fillRect`, `clearScreen`, etc.)
- Lock screen draw functions temporarily unblock: `setBlocked(false)` → draw → `setBlocked(true)`
- Status bar (`updateStatusBar`) is NOT blocked — shield icon stays live

## LockScreenManager unlock signal
- `consumeJustUnlocked()` — returns `true` once after unlock, then `false`

## Per-command restore strategy

| Command | Strategy |
|---------|----------|
| wguard view | `consumeJustUnlocked()` → redraw full header; timed draw → `continue` if blocked |
| cat viewer | `consumeJustUnlocked()` → `needsRedraw = true`; skip draw if `isBlocked()` |
| sw, sbl, nd, ps, ts, man | `consumeJustUnlocked()` in inner wait-loop → `break` → outer re-renders page |
| ls | `consumeJustUnlocked()` → redraw "any key" prompt |
| beacon flood | `drawStats()` early-return if `isBlocked()` |
| trackme | `drawScreen()` early-return if `isBlocked()` |
| hiddenssid | `_dm.xxx()` calls no-op while blocked (no explicit guard needed) |
| ws/pm/cc/karma crack | crack-progress **sub-loops** need their own `consumeJustUnlocked()` → repaint header+status; the main capture loop's handler does NOT cover them (was the `ws` lock bug, 2026-06-19) |
| ssh | terminal draws direct to `tft` (bypasses DisplayManager) → guard `termRender()`+`drawHeader()` with `isBlocked()`; task loop `consumeJustUnlocked()` → `s_allDirty=true`+`redrawHeader()` |

## Gotcha — secondary/sub loops (the `ws` lock bug, 2026-06-19)
An app's MAIN loop handling unlock does NOT cover its sub-screens. Crack screens
(`ws` `crack()`, `pm` `crack()`, `cc` `runCrack()`, karma `karmaCrack()`) each run
their own long blocking loop with a one-time header + a throttled `redraw()`/`status()`
that only repaints the "Trying" region. On lock→unlock these stay blank. Fix pattern:
wrap the static header in a `drawHeader()`/`drawStatic()` lambda, and in EACH crack loop
+ result/`[q] back` wait loop add `if (consumeJustUnlocked()) { drawHeader(); redraw(cand); }`.
Also: pickers/`while(ch!='1'&&ch!='2')` source prompts and `while(!getKeyboardInput())`
"any key" result waits are sub-loops too — `cc pickList` inner wait now breaks to repaint.
Remaining minor gap: karma's post-result `[any key] back to list` waits (spans two funcs).
