---
title: Power Save
lang: en
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

**Battery-aware alert + auto-sleep (T-Pager)** — using the BQ27220 fuel gauge + BQ25896 charger for an accurate state-of-charge and charge state, while running on battery:

- At **`batteryWarnPercent`** (default 15%) it plays one **alert tone** (re-arms after you charge or recover).
- At **`batteryCriticalPercent`** (default 5%) it **auto deep-sleeps** to protect the cell — press to wake after charging. Disable with `batteryAutoSleep=false`.
- Both are suppressed whenever USB is plugged in (charging or not), so a USB/MSC session never sleeps on you. Alerts are silenced under undercover mode.

These three keys live in `/config/pwrsave.conf`; `psv` status shows the warn/sleep thresholds. (The alert/auto-sleep is T-Pager-only — the T-Deck's ADC-derived battery % is too noisy to trust for an auto-sleep.)

Config is saved to `/config/pwrsave.conf` on the SD card and restored on boot.
