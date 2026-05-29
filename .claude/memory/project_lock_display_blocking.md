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
