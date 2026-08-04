---
title: Diagnostics
lang: en
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
CMD> cw <ssid> <password>   # connected mode: join a network first
CMD> csi                    # ...then open the detector (cleaner signal)

CMD> csi auto               # passive mode: NO router join needed
```

**Two modes:**
- **`csi`** — uses the network you're connected to (run `cw` first). A busy link = more frames = snappier, cleaner detection.
- **`csi auto`** — scans, locks onto the **strongest nearby AP's beacons**, and senses **without associating to anything**. Works against any router. Trade-off: beacon-rate is lower than a connected link, so it's a touch laggier. It locks to a *single* AP on purpose (mixing multiple transmitters would read as fake motion).

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
| **Blips / sectors** | **Decorative.** Blip *intensity* reflects real signal (how much a subcarrier band reacted), but the *position/angle* on the dial is arbitrary — the 8 sectors are mapped to fixed angles, **NOT** directions. A blip at "top" does not mean motion is in front of you. |
| **Sweep cone** | **Decorative only** — a time-based animation, not scanning or aiming at anything. |
| `AUTO c<ch> <ssid>` / `LINK c<ch> <ssid>` | Source AP: mode, channel + network name (truncated; full BSSID/SSID in the CSV log) |
| `NBVI on/off` | Smart subcarrier weighting state (toggle with `n`) |
| `fr:<n>` + `CSI live` | Bring-up health: CSI frames received + status |

### Keys

| Key | Action |
|-----|--------|
| `a` / `l` / trackball | Sensitivity (− / +). In adaptive mode these nudge the noise-floor *margin*. |
| `c` | Recalibrate — re-baseline the room (also resets the noise floor) |
| `t` | **Adaptive threshold** on/off — auto-tracks the quiet-room noise floor |
| `n` | NBVI smart-subcarrier weighting on/off (A/B test) |
| `s` | **SD logging** on/off → `/apps/csidetect/NNN.csv` |
| `h` | In-app help overlay |
| `q` | Quit |

### Signal processing (what makes it more precise)

- **NBVI subcarrier weighting** — instead of averaging all ~56 subcarriers equally, each subcarrier is weighted by its *normalized variance*, so the ones actually reacting to movement dominate and static ones contribute ~nothing. Auto-learned (no manual calibration); toggle `n` to compare on/off.
- **Hampel filter** — a 7-sample median/MAD outlier reject on the motion signal, so a single RF glitch can't false-trip CONTACT.
- **Adaptive threshold** (`t`) — instead of a fixed sensitivity, the trip level sits a small *margin* above the continuously-learned quiet-room noise floor. When the source gets noisier (e.g. `csi auto`'s sparse beacons), the bar auto-raises to match → fewer false CONTACTs. `a`/`l` adjust the margin; the panel shows `THR auto`.
- **Single-source lock** (auto mode) — only one AP's frames feed the detector, avoiding false motion from channel traffic switching between transmitters.

### SD logging (`s`)

Press `s` to record **presence transitions** to `/apps/csidetect/NNN.csv` (sequential, never overwritten — like wguard/bmon). A row is written each time the state flips CLEAR↔CONTACT, so the file stays small and answers "when was there motion." Columns:

```
time,event,motion_pct,thresh_pct,zones,mode,channel,bssid,ssid
```

`time` is a real date-time when GPS/NTP is available, otherwise an `@<uptime>ms` counter (same convention as the other apps). `mode` is `AUTO`/`LINK`. `channel`/`bssid`/`ssid` identify the AP the motion was sensed off — the **full BSSID** (e.g. `A1:B2:C3:D4:7F:78`) and its **network name** (`ssid`, commas stripped for CSV; `(hidden)` if none). The panel shows `L<n>` next to `fr:` while recording. Writes are GDMA-guarded (promiscuous is paused for each write), so logging is safe even though CSI keeps the radio busy.

### How to use it

1. Either connect with `cw` then run `csi`, **or** just run `csi auto` (no join — it
   picks the strongest AP itself). A busier source gives more frames and snappier detection.
2. Leave it still for ~10–15 s so it learns the empty room → **CLEAR**.
3. Move or walk near it → **CONTACT** + the MOTION bar rises.
4. Best confidence check: **leave the room → CLEAR; walk back → CONTACT**, repeated.

### Honest limits

- **The radar layout is decorative.** The circular dial, the sweeping cone, and
  the *angle/position* of every blip are a visual style only — **not** a map of
  where motion is. Only two things carry real meaning: the centre (CLEAR/CONTACT)
  and the MOTION % bar. A blip's brightness ≈ how much a subcarrier band moved;
  its place on the dial is arbitrary.
- **Single antenna = one motion-energy signal.** There is **no true direction,
  no person count, and no localization** — in either `csi` or `csi auto`. The 8
  sectors are independent signal measurements spread around the dial for
  readability, *not* a compass bearing. (Reference projects either randomize the
  angle or use a multi-node mesh + ML to get real position; one T-Deck can't.)
- **`csi auto` looks noisier than `csi`** — passive beacon-rate (~10 frames/s) is
  far sparser than a connected link, so the signal jitters more. Press `c` to
  recalibrate while still, lower sensitivity with `a`, or use connected `csi` for
  the cleanest read.
- **Motion only** — a perfectly still person can read CLEAR.
- **Environment-dependent** — needs WiFi traffic, benefits from `c` recalibration
  when you change rooms. Best treated as a covert *motion/occupancy* indicator,
  not a precision sensor.

Adapted from the single-device CSI path of
[skizzophrenic/Cardputer-CSI-Human-Detector](https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector)
(MIT); subcarrier-band idea from [ruvnet/ruview](https://github.com/ruvnet/ruview); the
NBVI subcarrier weighting, Hampel filter, and passive operation are methodology from
[ESPectre](https://github.com/francescopace/espectre) (GPL — no code copied). See `NOTICES`.
