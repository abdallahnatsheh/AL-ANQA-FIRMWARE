---
title: SD Card
nav_order: 7
---

# SD Card Layout

Format the card as **FAT32**. Insert before power-on.

---

## Quick-start checklist

- [ ] FAT32 formatted microSD (any size)
- [ ] Insert card before powering on
- [ ] Boot — `/logs/`, `/scripts/`, `/captures/` auto-created
- [ ] Run `sdinfo` to confirm card is detected
- [ ] Run `sdls /` to verify directory structure

Everything else is created automatically as you use features.

---

## Directories — auto-created on first boot

| Path | Created by |
|------|-----------|
| `/logs/` | SDCardManager on init |
| `/scripts/` | SDCardManager on init |
| `/captures/` | SDCardManager on init |
| `/logs/hs/` | `wpasniff` on first capture |

---

## Files — auto-created on first use

Do nothing — firmware creates these when the relevant feature is first used.

| File | Created by | Contains |
|------|-----------|----------|
| `/wpa_supplicant.conf` | `connectwifi` on successful connect | saved WiFi credentials (Linux-compatible) |
| `/wpa_supplicant.bak` | first T-Rex write to conf | backup of original file before T-Rex modifies it |
| `/logs/eviltwin.csv` | `eviltwin` on capture or `[s]` | `user,password` per line |
| `/logs/trackme.txt` | `trackme` on alert | tracking event log |
| `/logs/trackme_known.csv` | `trackme` on whitelist | `mac,label` per line |
| `/logs/hidden_ssids.csv` | `hiddenssid` on find | `bssid,ssid,channel` per line |
| `/logs/cracked.csv` | `wpasniff` on crack | `bssid,ssid,password` per line |
| `/logs/hs/<BSSID>.cap` | `wpasniff` on EAPOL capture | raw pcap handshake file |
| `/logs/wifi.txt` | `wifimon` packet log | raw WiFi monitor log |
| `/logs/packets.txt` | `wifimon` sniffer | raw packet log |
| `/logs/bt.txt` | `scanblue` | BLE device log |
| `/logs/ports.txt` | `portscan` / `topscan` | port scan results |
| `/pwrsave.json` | `pwrsave set ...` | power save settings |
| `/macchanger.cfg` | `macchanger` on save | MAC spoof state + address |

---

## Files — create manually (optional)

### `/wordlist.txt`

Required for: `wpasniff` → `[c]` crack with custom wordlist  
Without it: falls back to built-in 100-word list (weak, demo only)  
Format: one password per line, plain UTF-8, no size limit

```
password
123456789
letmein
iloveyou
```

---

### `/signatures.csv`

Required for: `trackme` custom tracker signatures  
Without it: uses built-in AirTag / Tile / SmartTag signatures only  
Format: `name,uuid_fragment` — one per line, no header

```
MyTracker,FDA50693
CustomTag,0000FE9F
```

---

### `/evilportal/<name>.html`

Required for: `eviltwin` custom captive portal page  
Without it: uses the built-in T-Rex portal (works fine out of the box)  
Place one or more `.html` files — `eviltwin` lets you pick at runtime

Minimal example:
```html
<html><body>
<form method="POST" action="/post">
  <input name="username" placeholder="Username">
  <input name="password" type="password" placeholder="Password">
  <button type="submit">Login</button>
</form>
</body></html>
```

> Field names must be `username` and `password` for `eviltwin` to capture them.

---

## WiFi credentials and Linux sync

`/wpa_supplicant.conf` is in standard Linux format. See the [WiFi Credentials](wifi-credentials) guide for:
- Viewing saved passwords (`wifipass/wp`)
- Importing networks from Linux (RPi, desktop via `nmcli`)
- Exporting T-Deck networks back to Linux
- `update_config=1` behaviour and fix
