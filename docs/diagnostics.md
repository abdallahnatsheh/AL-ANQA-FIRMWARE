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

## `test` / `tst` — Hardware Self-Tests

The three hardware tests are sub-commands of one `test` command (formerly the standalone `spktest` / `mictest` / `loratest`):

```
CMD> test spk     # I2S speaker
CMD> test mic     # ES7210 microphone
CMD> test lora    # LoRa SX1262
```

---

## `test spk` — Speaker Test

```
CMD> test spk
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

## `test mic` — Microphone Test

```
CMD> test mic
```

ES7210 microphone hardware verification (the mic is on **both** T-Deck and
T-Deck Plus — only the GPS is Plus-exclusive).

- **Live level meter** — peak-hold bar + scrolling bar-graph that react to sound.
- **Voice-activity detection** — `*** VOICE DETECTED ***` flashes when speech is
  present (debounced).
- **Record + replay** — capture 3 s to PSRAM and play it back through the speaker
  to confirm the full capture→playback path.

The capture de-duplicates the ES7210's `ALL_LEFT` stream (which delivers two
identical samples per audio sample) so playback is true 16 kHz mono — correct
pitch and duration.

| Key | Action |
|-----|--------|
| `r` | Record 3 s |
| `p` | Replay the recording |
| `+` / `-` | Mic gain |
| `q` | Quit |

---

## `test lora` — LoRa Test

```
CMD> test lora
```

Initializes the LoRa SX1262, runs a TX test, then enters RX monitor mode. Press `q` to stop.

---

## `csidetect` / `csi` — WiFi CSI Motion Detector  `[EXPERIMENTAL]`

> **Not an actual radar.** The circular sweep display is a *style* choice only —
> the device cannot locate, count, or aim at people. It detects **motion energy**
> from WiFi disturbance, nothing spatial. See "Honest limits" below.

```
CMD> cw <ssid> <password>   # must be connected first
CMD> csi                    # opens the motion detector
```

A **single-chip WiFi motion detector** — believed to be the first WiFi-CSI
sensing feature on a T-Deck. Every WiFi frame the radio receives carries
**Channel State Information** (amplitude/phase of the OFDM subcarriers). A moving
body changes the room's multipath, so the CSI variance rises. `csi` tracks that,
self-calibrates a floor/ceiling, and turns it into a live presence + motion
readout on a double-buffered sweep-style display. No camera, no PIR, no extra
hardware — it rides the WiFi your environment is already transmitting.

### Reading the screen

| Element | Meaning |
|---------|---------|
| **CLEAR** (green centre) | No movement detected |
| **CONTACT** (red centre, pulses) | Movement near the device |
| **MOTION %** | How much movement right now |
| **Activity** (`STILL`/`FIDGET`/`WALK`/`RUN`) | Rough magnitude class |
| **Blips / sectors** | Which subcarrier bands reacted most — a *rough, repeatable* hint of activity, gated so a still room stays empty |
| **Sweep cone** | Cosmetic sweep animation (decorative only) |
| `fr:<n>` + `CSI live` | Bring-up health: CSI frames received + status |

### Keys

| Key | Action |
|-----|--------|
| `a` / `l` / trackball | Sensitivity (− / +) |
| `c` | Recalibrate — re-baseline the room |
| `h` | In-app help overlay |
| `q` | Quit |

### How to use it

1. Connect to any 2.4 GHz network with `cw` (CSI is read from frames on that
   channel — a busier network gives more frames and snappier detection).
2. Run `csi` and leave it still for ~10–15 s so it learns the empty room → **CLEAR**.
3. Move or walk near it → **CONTACT** + the MOTION bar rises.
4. Best confidence check: **leave the room → CLEAR; walk back → CONTACT**, repeated.

### Honest limits

- **Single antenna = one motion-energy signal.** There is **no true direction,
  no person count, and no localization.** The radar sectors are 8 independent
  signal measurements spread around the dial for readability — *not* a compass
  bearing. (The reference projects either randomize the angle or use a multi-node
  mesh + ML to get real position; one T-Deck can't.)
- **Motion only** — a perfectly still person can read CLEAR.
- **Environment-dependent** — needs WiFi traffic, benefits from `c` recalibration
  when you change rooms. Best treated as a covert *motion/occupancy* indicator,
  not a precision sensor.

Adapted from the single-device CSI path of
[skizzophrenic/Cardputer-CSI-Human-Detector](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(MIT); subcarrier-band idea from [ruvnet/ruview](https://github.com/ruvnet/ruview).
See `NOTICES`.
