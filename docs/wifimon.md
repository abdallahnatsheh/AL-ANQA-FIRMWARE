---
title: WiFi Monitor
parent: WiFi
nav_order: 2
---

# WiFi Monitor

## `wifimon` / `wm` — Promiscuous 802.11 Monitor

```
CMD> wm             # channel-hop across all 2.4 GHz channels
CMD> wm 0           # same as above
CMD> wm 6           # lock to channel 6
CMD> wm 11          # lock to channel 11
```

Press `q` to stop.

---

## How It Works

`wifimon` puts the ESP32-S3 radio into **promiscuous mode** — it stops filtering frames by destination MAC and captures everything in the air on the selected channel. Each captured frame is decoded and printed as a single line.

Every line shows:
```
[TYPE]  SSID/name        SRC_MAC            BSSID              CH  RSSI
[BCN]   HomeNetwork      AA:BB:CC:DD:EE:FF  AA:BB:CC:DD:EE:FF  6   -42
[PRB]   MyPhone          11:22:33:44:55:66  FF:FF:FF:FF:FF:FF  0   -67
[DTH]   -                AA:BB:CC:DD:EE:FF  CC:DD:EE:FF:00:11  6   -55
```

---

## Frame Types

| Code | Frame | What it means |
|------|-------|---------------|
| `[BCN]` | Beacon | AP advertising itself — shows SSID, channel, RSSI |
| `[PRB]` | Probe request | Device looking for a network — shows device MAC, requested SSID |
| `[PRS]` | Probe response | AP responding to a probe |
| `[DTH]` | Deauth / Disassoc | Client being kicked — useful to spot attacks |
| `[DAT]` | Data frame | Encrypted payload (no content visible, just MACs + RSSI) |
| `[MGT]` | Other management | Auth, assoc, reassoc frames |

---

## Channel Modes

| Mode | Behaviour |
|------|-----------|
| `wm` / `wm 0` | Hops channels 1→2→…→13, dwelling ~200ms per channel. Sees all traffic but may miss short bursts on any given channel. |
| `wm <ch>` | Locks to one channel. Captures every frame on that channel with no gaps — use this when targeting a specific AP. |

---

## Use Cases

- **Passive recon** — see every AP and device in range without sending a single frame
- **Verify deauth attack** — lock to the target channel and watch for `[DTH]` frames you're injecting
- **Spot hidden SSIDs** — probe requests reveal what SSIDs nearby devices are looking for
- **Catch rogue devices** — unusual MACs probing at night, unexpected data frames

> BLE and WiFi share one antenna — they cannot run simultaneously. Stop any active BLE scan before starting `wm`.
