---
title: Deauth Attack
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
