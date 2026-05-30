---
title: Mouse Jiggler
parent: USB Gadget
nav_order: 4
---

# Mouse Jiggler

## `jiggle` / `jg` — Prevent Host Screen Lock

```
CMD> jg
```

Press `q` to stop.

---

## How It Works

`jiggle` sends tiny **USB HID mouse movement** packets to the connected PC on a timed interval. The cursor moves 1 pixel right, then 1 pixel left — staying in effectively the same position but continuously resetting the host OS's idle timer.

Most operating systems treat any mouse movement as "user activity" and reset their screen lock/sleep timer. This keeps the host awake and unlocked indefinitely without touching the actual keyboard or mouse.

The movement is imperceptible in normal use — the cursor returns to its original position after each tick.

---

## Use Cases

- Keep a session alive while working on the T-DECK without touching the host
- Prevent a locked workstation during a pentest assessment
- Keep a remote desktop session from timing out

---

## Requirements

- T-DECK must be connected via **USB data cable** (not charge-only)
- The host must recognise T-DECK as a HID device (same as `usbkbd`) — no additional drivers needed on Windows, macOS, or Linux
- If the host prompts for driver installation, try a different USB port

---

## Notes

- The jiggler runs independently — you can use the T-DECK keyboard and trackball normally while `jg` is active
- Movement interval is fixed — there is no configurable timing
- Some enterprise endpoint-security tools detect jiggling patterns; if the host has strict HID monitoring this may be logged
