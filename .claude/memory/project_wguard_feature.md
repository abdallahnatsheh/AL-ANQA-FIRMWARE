# wguard — WiFi Intrusion Detection System (Future Feature)

## Command
`wguard` / `wg` — passive WiFi IDS for your own network

## Flow
1. Run `sw` first to scan → pick your AP by index (same as deauth/wpasniff)
2. Lock to target BSSID + channel, enable promiscuous mode
3. Monitor all 802.11 frames, alert on attack patterns

## Threat Detection

| Threat | Method | Alert |
|---|---|---|
| Deauth attack | >5 deauth frames targeting your BSSID or clients in 10s | 🔴 CRITICAL |
| Evil Twin | Beacon with your SSID from a different BSSID | 🔴 CRITICAL |
| Handshake harvest | Deauth burst + EAPOL frames within short window | 🔴 CRITICAL |
| PMKID grab | Association requests with no prior probe (modern hashcat, no client needed) | 🔴 CRITICAL |
| Auth flood | Dozens of auth requests from randomized MACs in short window | 🟡 WARNING |
| Probe storm | Single device sending 30+ probes/5s for your SSID | 🟡 WARNING |
| Beacon flood | 50+ new APs appearing suddenly (Mana/Karma rogue AP attack) | 🟡 WARNING |

## Explicitly NOT included
- **New unknown client detection** — NOT feasible: modern phones/laptops use MAC randomization,
  rotating MAC on every scan. Would generate constant false positives with no signal value.

## Frame parsing
- Management subtype 0x0C = deauth
- Management subtype 0x00 = association request
- Management subtype 0x04 = probe request
- Management subtype 0x08 = beacon
- Data frames with EtherType 0x888E = EAPOL (WPA2 handshake)
- Rate counters per source MAC with rolling 10s window

## UI
```
[CYAN::WGUARD]  ch6 · YourNetwork · monitoring

STATUS: ⚠ WARNING                           14:23:01

  Beacons:142  Probes:23  Auth:4  Deauths:0  EAPOL:0

EVENTS
  14:21:33  ℹ  New client joined: XX:XX:XX:XX:XX:XX
  14:22:01  ⚠  Probe storm: 31 probes/5s (XX:XX:XX:XX:XX:XX)
  14:22:58  🔴 EVIL TWIN: "YourNetwork" on ch11 from foreign BSSID

[q]quit  [s]save log
```

## SD output
`/logs/wguard.csv` — timestamp, threat type, attacker MAC (where known), detail

## Implementation notes
- Reuse `WiFiMonitor` promiscuous infrastructure and frame parser from `handshake_capture.cpp`
- Scan index input same pattern as `deauth` / `wpasniff`
- Rolling counter: `struct ThreatCounter { uint8_t srcMac[6]; uint32_t count; uint32_t windowStart; }`
- Evil Twin: collect all beacon BSSIDs in map → flag SSID match with different BSSID
- GDMA rule: open SD log file BEFORE enabling promiscuous mode
