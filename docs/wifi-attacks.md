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
| [PMKID Attack](pmkid) | `pmkid` / `pm` | PMKID capture + crack — **active clientless by default** (`pm passive` = silent sniff), no client needed |
| [Karma](karma) | `karma` / `km` | Rogue-AP suite — probe harvest, PNL fingerprint, half-handshake / portal bait |
| [Cap Cracker](capcrack) | `crack` / `cc` | Offline crack of a `.cap` (handshake or PMKID) with wordlists — resume cursor, type-a-path picker, and a **background mode** (`cc bg`) that grinds even under the undercover cover |
| [WGuard IDS](wguard) | `wguard` / `wg` | Passive WiFi intrusion detection |
| [Beacon Flood](beacon-flood) | `beaconflood` / `bf` | Flood WiFi scan lists with fake SSIDs |
| [WPS](wps) | `wps` | WPS recon (IE decode + device-info leak) + PIN calculator + push-button connect |
| [Pwnagotchi Pet](pwn) | `pwn` / `pw` | Autonomous **AI-adaptive roam** (all 13 ch; `basic`=1/6/11) + capture handshakes/PMKIDs (clientless solicit) + **crack on-device** (active/stealth/passive); **runs card-less** (RAM crack → NVS) + **pack-shares cracked creds over the grid** |

---

Each attack has its own dedicated guide — select one from the table above or use the sidebar.
