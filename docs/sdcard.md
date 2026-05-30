---
title: SD Card
parent: System
nav_order: 9
---

# SD Card Layout

Format the card as **FAT32**. Insert before power-on.

---

## Quick-start checklist

- [ ] FAT32 formatted microSD (any size)
- [ ] Insert card before powering on
- [ ] Boot — `/logs/`, `/badusb/`, `/captures/` auto-created
- [ ] Run `sdinfo` to confirm card is detected
- [ ] Run `sdls /` to verify directory structure

Everything else is created automatically as you use features.

---

## Directories — auto-created on first boot

| Path | Created by |
|------|-----------|
| `/logs/` | SDCardManager on init |
| `/badusb/` | SDCardManager on init |
| `/captures/` | SDCardManager on init |
| `/logs/hs/` | `wpasniff` on first capture |
| `/logs/wguard/` | `wguard` on first session |

---

## Files — auto-created on first use

Do nothing — firmware creates these when the relevant feature is first used.

| File | Created by | Contains |
|------|-----------|----------|
| `/wpa_supplicant.conf` | `connectwifi` on successful connect | saved WiFi credentials (Linux-compatible) |
| `/wpa_supplicant.bak` | first T-Rex write to conf | backup of original file before T-Rex modifies it |
| `/logs/eviltwin.csv` | `eviltwin` on capture or `[s]` | `user,password` per line |
| `/logs/trackme.csv` | `trackme` on alert | tracking event log |
| `/logs/trackme_known.csv` | `trackme` on whitelist | `mac,label` per line |
| `/logs/hidden_ssids.csv` | `hiddenssid` on find | `bssid,ssid,channel` per line |
| `/logs/cracked.csv` | `wpasniff` on crack | `bssid,ssid,password` per line |
| `/logs/hs/<BSSID>.cap` | `wpasniff` on EAPOL capture | raw pcap handshake file |
| `/logs/wguard/001.csv` … `999.csv` | `wguard` — one file per session | CSV: `time,severity,rssi_dbm,message` + session header/footer |
| `/logs/wifi.txt` | `wifimon` packet log | raw WiFi monitor log |
| `/logs/packets.txt` | `wifimon` sniffer | raw packet log |
| `/logs/bt.txt` | `scanblue` | BLE device log |
| `/logs/ports.txt` | `portscan` / `topscan` | port scan results |
| `/pwrsave.conf` | `pwrsave set ...` | power save settings (key=value) |
| `/macchanger.conf` | `macchanger` on save | MAC spoof state + address |
| `/lockscreen.conf` | `lock new` / `lock timeout` | PIN hash + salt + idle timeout |
| `/notif.conf` | `notif` on any change | notification levels + volume + MP3 paths |

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
Format: `type,company_id_hex,name,severity` — one per line, no header

```
BLE,0x004C,Apple AirTag,HIGH
BLE,0x00D7,Tile Tracker,HIGH
BLE,0x0157,Samsung SmartTag,MEDIUM
```

---

### `/wordlist_beacons.txt`

Required for: `beaconflood` → `[4] file` mode  
Without it: the file mode is unavailable (other modes work fine)  
Format: one SSID per line, plain UTF-8

```
FreeAirport_WiFi
Starbucks_Guest
xfinitywifi
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
