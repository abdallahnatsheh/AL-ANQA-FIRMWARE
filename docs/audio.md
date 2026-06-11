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

### Test sounds

```
CMD> nf test            # interactive picker — press 1-5 to play, [a] all, [q] quit
CMD> nf test alert      # play one level directly
```

`nf test` force-plays the sound even if that level is toggled off, so it's the quickest way to audition your custom WAVs. (The `spktest` keys `a`/`w`/`c`/`i`/`p` also play them, but respect the on/off toggle.)

### Notification levels

| Level | Trigger | Default tone |
|-------|---------|--------------|
| `alert` | Deauth storm, Evil Twin, PMKID, cracked WPA2, handshake harvest | 3 × 1000 Hz beeps |
| `warning` | Auth flood, probe storm, beacon flood | 2 × 700 Hz beeps |
| `success` | WPA handshake captured, hidden SSID found | 500→650→800 Hz ascending |
| `info` | General one-shot beep | 500 Hz, 200 ms |
| `ping` | BLE buddy prompt arrived | 600 Hz, 100 ms |

### Custom WAV per level

The player is **raw WAV PCM** (not MP3). Convert any sound to **16-bit PCM, 22050 Hz, mono**:

```
ffmpeg -i input.mp3 -ar 22050 -ac 1 -acodec pcm_s16le alert.wav
```

Place the `.wav` files in `/config/notification/` on the SD card, then point a level to the file:

```
CMD> nf alert file alert.wav          # resolves to /config/notification/alert.wav
CMD> nf success file /sounds/ok.wav   # absolute path
CMD> nf alert file                    # clear — back to built-in tone
```

On notification, T-REX checks if a WAV is configured for that level. If the SD card is available and the file exists, it plays the WAV. Otherwise it falls back to the built-in I2S tone.

### Config file — `/config/notif.conf`

Auto-created by `sdf init`. Key=value format:

```
notif_volume=70
alert=on
warning=on
success=on
info=on
ping=on
alert_file=alert.wav
success_file=success.wav
```

Bare filenames resolve under `/config/notification/`. Changes made with `nf` commands are saved automatically. You can also edit the file directly on the SD card.

### SD folder structure

```
/config/notification/
  alert.wav
  warning.wav
  success.wav
  info.wav
  ping.wav
```

All files must be **16-bit PCM WAV, 22050 Hz, mono**. Keep them short (<1 s) so alerts don't lag the UI.

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

Keys `1`–`6` and `s` play raw I2S tones at full amplitude — they bypass `nf vol` and are useful for verifying the speaker hardware is working at all. Keys `a`/`w`/`c`/`i`/`p` go through `NotificationManager` and will play your configured WAV if one is set, at your configured `nf vol` (respecting each level's on/off toggle). To force-play regardless of toggle, use `nf test` instead.

> **Note:** All audio on T-Deck Plus uses `i2s_driver_install()`. The `tone()` Arduino function does not work on this hardware.
