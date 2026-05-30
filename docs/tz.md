---
title: Timezone
parent: System
nav_order: 6
---

# Timezone

## `tz` — Set Device Timezone

Sets the timezone used by the status bar clock and wguard session log timestamps.

```
CMD> tz status               # show current timezone
CMD> tz +3                   # UTC+3
CMD> tz -5:30                # UTC-5:30
CMD> tz <posix>              # full POSIX TZ string
```

### Examples

```
tz +3                                   # Riyadh, Moscow (UTC+3)
tz +5:30                                # IST (India)
tz -5                                   # EST, no DST
tz EST5EDT,M3.2.0,M11.1.0              # US Eastern with DST rules
```

The timezone is saved to NVS and restored on boot. Simple `+HH` / `-HH:MM` offsets are converted to a valid POSIX string automatically.
