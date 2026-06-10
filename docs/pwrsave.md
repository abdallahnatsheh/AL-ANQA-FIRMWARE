---
title: Power Save
parent: System
nav_order: 3
---

# Power Save

## `pwrsave` / `psv`

Two-tier inactivity system that dims and then turns off the screen.

```
CMD> psv status
CMD> psv on / psv off
CMD> psv set dim <seconds>           # inactivity dim timeout (default: 120s)
CMD> psv set screenoff <seconds>     # screen-off timeout (default: 300s)
CMD> psv set screenoffmode on|off
```

| Tier | Default | Behaviour |
|------|---------|-----------|
| Dim | 2 min | Reduces brightness |
| Screen off | 5 min | Brightness = 0, any key restores |

**Battery-aware dim** — automatically dims when battery drops below threshold regardless of the inactivity timer.

Config is saved to `/config/pwrsave.conf` on the SD card and restored on boot.
