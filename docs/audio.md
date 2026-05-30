---
title: Audio & Notifications
parent: System
nav_order: 5
---

# Audio & Notifications

T-REX uses I2S (not `tone()`) for all audio on the T-Deck Plus. Two independent volume controls exist: general volume (`vol`) and notification volume (`nf vol`).

---

## `volume` / `vol` — General Volume

```
CMD> vol          # show current level
CMD> vol 80       # set to 80%
CMD> vol up       # +10%
CMD> vol down     # -10%
CMD> vol off      # mute (0%)
```

Reserved for future features (MP3 music player, voice recorder). Does **not** affect notification sounds.

---

## `notif` / `nf` — Notification Manager

### Enable / disable

```
CMD> nf                  # status screen
CMD> nf on               # enable all levels
CMD> nf off              # disable all levels
CMD> nf alert on         # enable a single level
CMD> nf warning off      # disable a single level
```

### Volume

```
CMD> nf vol              # show current notif volume
CMD> nf vol 70           # set notif volume to 70% (0-100)
```

Notification volume is independent of `vol`. You can have notifications loud and music quiet, or vice versa.

### Notification levels

| Level | Trigger | Default tone |
|-------|---------|--------------|
| `alert` | Deauth storm, Evil Twin, PMKID, cracked WPA2, handshake harvest | 3 × 1000 Hz beeps |
| `warning` | Auth flood, probe storm, beacon flood | 2 × 700 Hz beeps |
| `success` | WPA handshake captured, hidden SSID found | 500→650→800 Hz ascending |
| `info` | General one-shot beep | 500 Hz, 200 ms |
| `ping` | BLE buddy prompt arrived | 600 Hz, 100 ms |

### Custom MP3 per level

Place MP3 files in `/notification/` on the SD card. Then point a level to the file:

```
CMD> nf alert file alert.mp3          # resolves to /notification/alert.mp3
CMD> nf success file /sounds/ok.mp3   # absolute path
CMD> nf alert file                    # clear — back to built-in tone
```

On notification, T-REX checks if an MP3 is configured for that level. If the SD card is available and the file exists, it plays the MP3. Otherwise it falls back to the built-in I2S tone.

### Config file — `/notif.conf`

Auto-created by `sdf init`. Key=value format:

```
notif_volume=70
alert=on
warning=on
success=on
info=on
ping=on
alert_file=/notification/alert.mp3
success_file=/notification/success.mp3
```

Changes made with `nf` commands are saved automatically. You can also edit the file directly on the SD card.

### SD folder structure

```
/notification/
  alert.mp3
  warning.mp3
  success.mp3
  info.mp3
  ping.mp3
```

Any MP3 format supported by ESP8266Audio is accepted (MPEG Layer III, up to 320 kbps).

---

## `spktest` / `st` — Speaker Test

Interactive hardware test. Useful for verifying the I2S speaker and your notification config.

```
CMD> spktest
```

| Key | Action |
|-----|--------|
| `1` | 220 Hz — low bass |
| `2` | 440 Hz — A4 concert pitch |
| `3` | 880 Hz — high |
| `4` | 1000 Hz — beep |
| `5` | 2000 Hz — alert range |
| `6` | 4000 Hz — sharp |
| `s` | C major scale |
| `a` | Trigger NOTIF_ALERT (uses your `nf` config) |
| `w` | Trigger NOTIF_WARNING |
| `c` | Trigger NOTIF_SUCCESS |
| `i` | Trigger NOTIF_INFO |
| `p` | Trigger NOTIF_PING |
| `q` | Quit |

Keys `1`–`6` and `s` play raw I2S tones at full amplitude — they bypass `nf vol` and are useful for verifying the speaker hardware is working at all. Keys `a`/`w`/`c`/`i`/`p` go through `NotificationManager` and will play your configured MP3 if one is set, at your configured `nf vol`.

> **Note:** All audio on T-Deck Plus uses `i2s_driver_install()`. The `tone()` Arduino function does not work on this hardware.
