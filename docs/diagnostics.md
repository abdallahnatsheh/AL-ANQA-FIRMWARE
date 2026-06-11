---
title: Diagnostics
parent: System
nav_order: 8
---

# Diagnostics

## GPS (T-Deck Plus only)

```
CMD> gps on    # start GPS background task with live status display
CMD> gps off   # stop GPS background task
CMD> gps test  # one-shot GPS coordinate read + display
```

GPS status bar icon: grey = off, yellow = searching, green = fixed.

Cold start takes ~4 minutes outdoors. Run `gps on` before `trackme` to pre-warm the fix.

---

## `spktest` — Speaker Test

```
CMD> spktest
```

I2S speaker hardware verification and notification level test.

| Key | Action |
|-----|--------|
| `1`–`6` | Raw tones (220–4000 Hz) — bypass NotificationManager |
| `s` | Play C major scale |
| `a` / `w` / `c` / `i` / `p` | Trigger ALERT / WARNING / SUCCESS / INFO / PING notification levels |
| `q` | Quit |

Keys `a/w/c/i/p` go through NotificationManager and play the configured WAV if one is set (respecting each level's on/off toggle). Keys `1–6` bypass volume settings and play raw tones directly — use for hardware verification. To force-play a notification sound regardless of toggle, use `nf test`.

---

## `loratest` — LoRa Test

```
CMD> loratest
```

Initializes the LoRa SX1262, runs a TX test, then enters RX monitor mode. Press `q` to stop.
