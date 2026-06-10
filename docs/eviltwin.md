---
title: Evil Twin
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 2
---

# Evil Twin

## `eviltwin` / `et`

Clones a nearby AP and runs a captive portal to capture credentials. Run `et` to open the network picker.

```
CMD> et
```

### Strategy by target type

| Target auth | MAC strategy | Effect |
|-------------|-------------|--------|
| Open network | Clone real AP's exact MAC + channel | Devices reconnect seamlessly |
| WPA/WPA2 | Random locally-administered MAC + deauth real AP | Portal captures credentials |

**Adaptive deauth** — deauth bursts fire every 8 seconds and pause automatically while a portal client is connected so the credential form can be submitted.

### Custom portal pages

Drop `.html` files into `/apps/eviltwin/portal/` on the SD card. Templates are picked from the picker menu.

### Keys

| Key | Action |
|-----|--------|
| `c` | View captured credentials (portal keeps running) |
| `s` | Save credentials to SD |
| `q` | Stop Evil Twin |

Up to 20 credentials are held in memory. All captures are appended live to `/apps/eviltwin/creds.csv`.
