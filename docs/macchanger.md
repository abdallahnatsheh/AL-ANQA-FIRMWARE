---
title: MAC Changer
parent: WiFi
nav_order: 4
---

# MAC Changer

## `macchanger` / `mc` — MAC Spoofer

Spoofs the STA MAC address at the driver level using a locally-administered unicast address (bit 1 of byte 0 set, bit 0 clear).

```
CMD> mc on                          # enable spoofing
CMD> mc off                         # disable, restore real hardware MAC
CMD> mc random                      # apply a new random MAC immediately
CMD> mc set AA:BB:CC:DD:EE:FF       # apply a specific MAC
CMD> mc restore on|off              # enable/disable auto-restore on boot
CMD> mc target wifi|bt|both         # set which interface is spoofed
CMD> mc status                      # show current MAC and state
```

Config is saved to `/macchanger.conf` on the SD card and restored on boot.

### When it applies

MAC spoofing is applied automatically only in:

| Command | When |
|---------|------|
| `scanwifi` / `sw` | Before scan starts |
| `connectwifi` / `cw` | Before connecting |

It is **not** applied during `wifimon`, `deauth`, `wpasniff`, or any attack tool — injected frames use their own spoofed source addresses; passive sniffers do not transmit.
