---
title: BT Keyboard
parent: Bluetooth
nav_order: 7
---

# BT Keyboard

## `btkbd` / `bk` — BLE HID Keyboard + Mouse

Turns the T-Deck into a Bluetooth HID keyboard and mouse. Pairs via Just Works (no PIN). AES-128 encrypted after pairing. Bonds to Windows — reconnects automatically on next use.

```
CMD> bk
```

**Phase 1 — Pairing:** T-Rex advertises as `T-REX-KBD`. Pair from the host's Bluetooth settings. Hold trackball for 1.5s to cancel.

**Phase 2 — Active:** Every T-Deck key is forwarded as a HID keystroke. The trackball controls the mouse cursor.

### Trackball center button

| Action | Result |
|--------|--------|
| Quick tap (< 300ms) | Left click |
| Hold 300ms – 1.5s | Right click |
| Hold ≥ 1.5s | Exit |

**Backspace auto-repeat:** Hold Backspace for 1.5s → repeat at ~16 chars/sec.

> Uses a separate BLE address from `buddy` (suffix `:CB` vs `:BD`) so Windows bonds both devices independently. If Windows shows auth errors, remove the device from BT settings and re-pair.
