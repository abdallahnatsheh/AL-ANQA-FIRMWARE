---
title: WPS
---

# WPS Recon + Assisted Attack — `wps`

WPS (WiFi Protected Setup) recon and the maximum honest attack surface the ESP32 allows. **Own networks only.**

```
CMD> sw               # scan first (WPS APs are flagged)
CMD> wps              # list WPS-enabled APs with their index
CMD> wps 3            # recon AP #3 (IE decode + PIN calc)
CMD> wps pbc 3        # attempt a push-button connect
```

## Recon — `wps <idx>`

Parks on the AP's channel, promiscuous-captures its beacon, and decodes the **WPS Information Element**:

- **Version** (1.0 / 2.0) and **WPS state** (configured / open)
- **AP-Setup-Locked** — if set, the AP has locked WPS and a PIN attempt would be refused (shown in red)
- **Config methods** — PBC / Display / Keypad / Label
- **Device-info leak** — the **Manufacturer, Model, Device Name and Serial** the AP advertises in its WPS IE (genuinely useful fingerprinting)
- **Candidate PINs** — computed from the BSSID (ComputePIN) plus the `12345670` default

Results are logged to `/apps/wps/wps.csv`.

## Push-button connect — `wps pbc <idx>`

Attempts a WPS **push-button** connection. This works **only while the AP's WPS button is physically active** — press it on the router, then run the command. On success the ESP32 enrolls and recovers the **SSID + PSK** (saved to `/apps/wps/creds.csv`). It's the one credential-recovery path the WiFi stack allows.

## The PIN calculator is display-only

The `wps` recon screen *computes* likely PINs, but **it cannot test them on-device** — copy them to a Reaver-capable radio.

> **Honest limit:** an **automated WPS PIN brute-force or Pixie-Dust attack is not possible on the ESP32.** The closed WiFi stack's `esp_wps_config_t` has no PIN field (it only lets the ESP32 *generate* its own enrollee PIN, never supply the AP's), and registrar mode is explicitly unsupported; Pixie-Dust needs M1/M3 crypto the stack never exposes. No ESP32 firmware (Marauder, Bruce, esp32-wifi-penetration-tool) ships a working WPS PIN/Pixie attack for the same reason. `wps` therefore does the real recon + PIN math + PBC, and is honest that on-device PIN testing isn't achievable.
