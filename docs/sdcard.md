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
- [ ] Boot — `/config/`, `/config/notification/`, `/apps/<tool>/` (one per command), `/apps/README.txt` auto-created
- [ ] Run `sdinfo` to confirm card is detected
- [ ] Run `sdls /` to verify directory structure

The full `/config` + `/apps/<tool>` tree is created **eagerly** on first
boot/format by `ensureTreeStructure()` — every tool's folder exists from the
start, even before you've used it. The on-device `/apps/README.txt` file always
lists the current folder→command map — read it with `cat /apps/README.txt` if
you forget where something lives.

> **Note on layout (v2)**: the SD layout groups every tool's data — logs,
> captures, wordlists, and tool-specific config — under its own
> `/apps/<tool>/` folder. Device-wide settings (pwrsave, lockscreen, notif,
> clock, macchanger) live in `/config/`. There is no `/logs/` folder anymore.
> If you're upgrading from an older firmware build, files at old paths (e.g.
> root `/pwrsave.conf`, v1 `/logs/<tool>/...`, or `/config/<tool-specific
> file>`) are left in place but no longer read — move them manually into the
> new locations below if you want to keep them.

---

## Directories — auto-created on first boot

All of these are created up front by `ensureTreeStructure()` — nothing is
created lazily.

| Path | Notes |
|------|-----------|
| `/config/` | device-wide settings |
| `/config/notification/` | shared per-level alert WAVs (16-bit PCM, 22050Hz, mono) |
| `/apps/` | one self-contained folder per command |
| `/apps/trackme/` | session log, whitelist, custom signatures.csv |
| `/apps/eviltwin/` | creds.csv |
| `/apps/eviltwin/portal/` | custom captive portal HTML |
| `/apps/hiddenssid/` | found.csv |
| `/apps/wpasniff/` | wordlist.txt, `<BSSID>.cap`, cracked.csv |
| `/apps/pmkid/` | wordlist.txt, `<BSSID>.cap`, cracked.csv |
| `/apps/wifimon/` | PCAP captures + probes.csv |
| `/apps/wguard/` | session CSV logs |
| `/apps/beaconflood/` | wordlist.txt |
| `/apps/bmon/` | BLE advertisement logs |
| `/apps/i2cscan/` | results.csv |
| `/apps/fastpair/` | keys.csv, paired.csv, sniff.csv |
| `/apps/espsniff/` | ESP-NOW captures (CSV + pcap) |
| `/apps/bleinfo/` | GATT enum/sniff/replay saves |
| `/apps/espchat/` | contacts.csv, config.conf |
| `/apps/espchat/pub/` | public chat logs |
| `/apps/espchat/prv/` | private chat logs |
| `/apps/badusb/` | (parent of scripts/) |
| `/apps/badusb/scripts/` | DuckyScript payload files |

---

## Files — auto-created on first use

Do nothing — firmware creates these when the relevant feature is first used.

| File | Created by | Contains |
|------|-----------|----------|
| `/wpa_supplicant.conf` | `connectwifi` on successful connect | saved WiFi credentials (Linux-compatible) |
| `/wpa_supplicant.bak` | first T-Rex write to conf | backup of original file before T-Rex modifies it |
| `/apps/README.txt` | SDCardManager on init/format | folder → owning command map (never overwritten) |
| `/apps/eviltwin/creds.csv` | `eviltwin` on capture or `[s]` | `user,password` per line |
| `/apps/trackme/session.csv` | `trackme` on alert / `[s]` save | tracking event log |
| `/apps/trackme/known.csv` | `trackme` on `[w]` whitelist | `mac,label` per line |
| `/apps/hiddenssid/found.csv` | `hiddenssid` on find | `bssid,ssid,channel` per line |
| `/apps/wpasniff/cracked.csv` | `wpasniff` on crack | `bssid,ssid,password` per line |
| `/apps/wpasniff/<BSSID>.cap` | `wpasniff` on EAPOL capture | raw pcap handshake file |
| `/apps/pmkid/cracked.csv` | `pmkid` on crack | `bssid,ssid,password,PMKID` per line |
| `/apps/pmkid/<BSSID>.cap` | `pmkid` on M1 capture | raw pcap PMKID file |
| `/apps/wguard/001.csv` … `999.csv` | `wguard` — one file per session | CSV: `time,severity,rssi_dbm,message` + session header/footer |
| `/apps/wifimon/NNN.cap` | `wifimon` — one file per session | raw 802.11 PCAP (libpcap, linktype 105) |
| `/apps/wifimon/probes.csv` | `wifimon` `[p]` probe logger | `time_ms,mac,vendor,ssid,rssi` |
| `/apps/i2cscan/results.csv` | `i2cscan` `[s]` save | `timestamp,0xADDR,chip_name,type,ACK/DEAD` |
| `/apps/bmon/001.csv` … `NNN.csv` | `bmon` `[s]` toggle — one file per session | BLE advertisement log |
| `/apps/espsniff/NNN.csv` + `NNN.pcap` | `espsniff` capture | ESP-NOW capture (CSV + pcap) |
| `/apps/fastpair/keys.csv` | `fastpair` on key capture | cached anti-spoofing keys |
| `/apps/fastpair/paired.csv` | `fastpair` on pairing | `addr,name` per line |
| `/apps/fastpair/sniff.csv` | `fastpair scan` | `mac,modelId,name,rssi,status` per line |
| `/config/pwrsave.conf` | `pwrsave set ...` | power save settings (key=value) |
| `/config/macchanger.conf` | `macchanger` on save | MAC spoof state + address |
| `/config/lockscreen.conf` | `lock new` / `lock timeout` | PIN hash + salt + idle timeout |
| `/config/notif.conf` | `notif` on any change | notification levels + volume + WAV paths |
| `/config/clock.conf` | `tz` on save | timezone (`tz=<POSIX TZ string>`) |

---

## Files — create manually (optional)

### `/apps/wpasniff/wordlist.txt`

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

### `/apps/pmkid/wordlist.txt`

Required for: `pmkid` → `[c]` crack with custom wordlist  
Without it: falls back to built-in 100-word list (weak, demo only)  
Format: one password per line, plain UTF-8, no size limit (same format as `wpasniff`)

---

### `/apps/trackme/signatures.csv`

Required for: `trackme` custom tracker signatures  
Without it: uses built-in AirTag / Tile / SmartTag signatures only  
Format: `type,company_id_hex,name,severity` — one per line, no header

```
BLE,0x004C,Apple AirTag,HIGH
BLE,0x00D7,Tile Tracker,HIGH
BLE,0x0157,Samsung SmartTag,MEDIUM
```

---

### `/apps/beaconflood/wordlist.txt`

Required for: `beaconflood` → `[4] file` mode  
Without it: the file mode is unavailable (other modes work fine)  
Format: one SSID per line, plain UTF-8

```
FreeAirport_WiFi
Starbucks_Guest
xfinitywifi
```

---

### `/apps/eviltwin/portal/<name>.html`

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
