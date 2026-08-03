---
title: Deauth Attack
lang: en
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 1
---

# Deauth Attack

## `deauth` / `da`

Sends raw 802.11 deauthentication (0xC0) and disassociation (0xA0) frames in bursts to disconnect clients from a target AP.

```
CMD> da <bssid|index> [channel] [client_mac]
CMD> da 2                            # broadcast deauth, index from last sw (channel auto)
CMD> da AA:BB:CC:DD:EE:FF 6         # by BSSID on channel 6
CMD> da 2 6 11:22:33:44:55:66       # targeted — one client only
```

| Argument | Description |
|----------|-------------|
| `bssid\|index` | Target AP — scan index from last `sw`, or full BSSID |
| `channel` | Optional. Auto-detected when using a scan index |
| `client_mac` | Optional. Omit for broadcast (all clients) |

Shows a live counter of frames sent and failed. Press `q` to stop.

---

## Prerequisite

Run a scan first so `da` knows the target (and its channel):

```
CMD> sw            # scan — note the target's index
CMD> da 2          # deauth scan-result #2 (channel auto-filled)
```

You can skip the scan by giving a full BSSID + channel directly.

## How it works

The radio switches to `APSTA` + promiscuous mode and injects spoofed **deauth (`0xC0`)** and **disassoc (`0xA0`)** frames with the AP's BSSID as the source. A **broadcast** deauth (`FF:FF:FF:FF:FF:FF`) knocks off every client; a **targeted** deauth (with `client_mac`) hits one station only, which is quieter and less disruptive.

## Honest limits

- **PMF / 802.11w** (Protected Management Frames) makes clients **ignore** these frames — modern WPA3 and many WPA2 networks enable it, so the target may not drop. This is expected, not a bug.
- Deauth is inherently **noisy** and easy to detect (see [WGuard](wguard), which flags exactly this).
- Own networks / authorized testing only.

## See also

- [WPA Sniff](wpasniff) — deauth is used there to force a handshake you can capture
- [Evil Twin](eviltwin) — pairs deauth with a rogue AP
- [WGuard IDS](wguard) — detects deauth attacks (the defensive side)
