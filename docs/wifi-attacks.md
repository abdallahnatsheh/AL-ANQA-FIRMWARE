---
title: WiFi Attacks
lang: en
parent: WiFi
nav_order: 5
has_children: true
---

# WiFi Attacks

> All attacks require being in range of the target. Run `scanwifi` (`sw`) first to populate the network index.

| Guide | Command | What it does |
|-------|---------|-------------|
| [Deauth](deauth) | `deauth` / `da` | Disconnect clients from an AP |
| [Evil Twin](eviltwin) | `eviltwin` / `et` | Rogue AP + captive portal |
| [Hidden SSID](hiddenssid) | `hiddenssid` / `hs` | Reveal hidden network names |
| [WPA Sniff](wpasniff) | `wpasniff` / `ws` | Capture + crack WPA2 handshake (needs client) |
| [PMKID Attack](pmkid) | `pmkid` / `pm` | PMKID capture + crack — no client needed |
| [Karma](karma) | `karma` / `km` | Rogue-AP suite — probe harvest, PNL fingerprint, half-handshake / portal bait |
| [Cap Cracker](capcrack) | `crack` / `cc` | Offline crack of a `.cap` (handshake or PMKID) with wordlists |
| [WGuard IDS](wguard) | `wguard` / `wg` | Passive WiFi intrusion detection |
| [Beacon Flood](beacon-flood) | `beaconflood` / `bf` | Flood WiFi scan lists with fake SSIDs |
| [WPS](wps) | `wps` | WPS recon (IE decode + device-info leak) + PIN calculator + push-button connect |

---

Each attack has its own dedicated guide — select one from the table above or use the sidebar.
