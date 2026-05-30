---
title: T-Deck vs T-Deck Plus
nav_order: 5
---

# T-Deck vs T-Deck Plus

The T-Deck and T-Deck Plus run **identical firmware**. The only hardware difference is the GPS module on the Plus. Everything else — display, keyboard, WiFi, Bluetooth, USB, SD card, speaker, audio — is the same.

---

## Comparison

| Feature | T-Deck | T-Deck Plus |
|---------|:------:|:-----------:|
| Display (320×240 ST7789) | ✅ | ✅ |
| Physical QWERTY keyboard | ✅ | ✅ |
| WiFi 2.4 GHz | ✅ | ✅ |
| Bluetooth 5 (BLE) | ✅ | ✅ |
| I2S speaker + audio | ✅ | ✅ |
| LoRa SX1262 | ✅ | ✅ |
| microSD slot | ✅ | ✅ |
| USB-C (flashing + USB gadget) | ✅ | ✅ |
| Trackball | ✅ | ✅ |
| Battery (with charge circuit) | ✅ | ✅ |
| **GPS module** (L76K / u-blox M10Q) | ✗ | ✅ |

---

## What GPS Unlocks

GPS affects only two commands:

### `trackme` — Tracking Detection

| Capability | T-Deck | T-Deck Plus |
|-----------|--------|-------------|
| BLE advertisement scanning | ✅ | ✅ |
| Baseline learning period | ✅ | ✅ |
| Behaviour scoring (Gate 2) | ✅ | ✅ |
| Time-path confirmation (Gate 3) — 5+ min, RSSI > −80 dBm | ✅ | ✅ |
| GPS movement gate (Gate 3) — 200m physical movement | ✗ | ✅ |
| WiFi probe sniff | ✗ | ✅ |

On the standard T-Deck, `trackme` still works and can reach WARNING/ALERT — it just uses the time-path for Gate 3 instead of GPS movement. The GPS path is faster and more reliable in a moving scenario.

### GPS Commands

| Command | T-Deck | T-Deck Plus |
|---------|--------|-------------|
| `gps on` — GPS background task + live status | ✗ | ✅ |
| `gps off` — stop GPS task | ✗ | ✅ |
| `gps test` — one-shot coordinate read | ✗ | ✅ |

---

## Build Configuration

| | T-Deck | T-Deck Plus |
|--|--------|-------------|
| PlatformIO environment | `env:T-Deck` | `env:T-Deck-Plus` |
| Compile flag | — | `-DBOARD_TDECK_PLUS=1` |

The `BOARD_TDECK_PLUS` flag gates only the GPS task init and the WiFi probe-sniff phase of `trackme`. All other code compiles and runs identically on both.

---

## GPS Cold Start

The GPS module needs approximately **4 minutes outdoors** for a cold fix (first fix after a power cycle or long idle). Once fixed it stays warm for the session.

**Recommended workflow for T-Deck Plus users:**

```
CMD> gps on      # start GPS task before doing anything else
```

Leave it running in the background. By the time you finish scanning WiFi and want to run `trackme`, the fix is already acquired — `trackme` will skip its own warm-up and start immediately.

The satellite icon in the status bar shows the state:

| Colour | Meaning |
|--------|---------|
| Grey | GPS off |
| Yellow | Searching (no fix yet) |
| Green | Fix acquired — location available |
