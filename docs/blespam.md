---
title: BLE Spam
parent: Bluetooth
nav_order: 5
---

# BLE Spam

## `blespam` / `bs` — BLE Advertisement Spam

Floods the air with BLE advertisements that trigger pairing or notification popups on nearby devices. MAC address is randomized each cycle to bypass device-side filters.

```
CMD> bs              # cycle all vendors
CMD> bs apple        # iOS Continuity popups
CMD> bs android      # Google Fast Pair (Android)
CMD> bs ms           # Windows Swift Pair toast
CMD> bs samsung      # Samsung Galaxy accessory popup
CMD> bs all          # cycle through all four vendors
```

| Vendor | What appears on target |
|--------|----------------------|
| `apple` | iOS AirPods / Apple TV pairing popup |
| `android` | "Headphones found nearby" (Google Fast Pair) |
| `ms` | Windows Swift Pair notification toast |
| `samsung` | Samsung Galaxy accessory pairing popup |

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous vendor |
| `q` | Stop |
