---
title: WiFi Attacks
nav_order: 2
---

# WiFi Attack Tools

> All WiFi attacks require being in range of the target. Always run `scanwifi` first to populate the network list.

---

## `scanwifi` / `sw` — Scan WiFi Networks

```
CMD> sw
```

Scans all 2.4 GHz networks and shows a paginated table with index, SSID, RSSI, auth type, and WPS flag.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-scan |
| `q` | Quit |

The scan result is cached — use `show wifi` to view it again without rescanning.

---

## `connectwifi` / `cw` — Connect to a Network

```
CMD> cw <index>       # connect by scan index from last sw
CMD> cw <ssid>        # connect by SSID name — no scan needed
```

Connects to a network by scan index or SSID name. Passwords are resolved automatically from NVS or SD card; you are only prompted if the password is unknown.

On successful connection the network is saved to `/wpa_supplicant.conf` on the SD card.

> For credential management, Linux sync, and the `wifipass` command see the [WiFi Credentials](wifi-credentials) guide.

---

## `clearwifi` / `clrw` — Clear Saved Credentials

```
CMD> clearwifi
CMD> clrw
```

Erases all saved WiFi passwords from NVS (non-volatile storage). Next time you connect to a known network, you will be prompted for the password again.

---

## `wifimon` / `wm` — Monitor Mode

```
CMD> wm [channel]
CMD> wm 6       # lock to channel 6
CMD> wm 0       # hop between all channels
CMD> wm         # same as wm 0
```

Puts the radio into promiscuous mode and displays a live feed of 802.11 frames: beacons, probe requests/responses, deauth frames, and data frames. Shows frame type, SSID (when present), source MAC, BSSID, channel, and RSSI.

Press `q` to stop.

---

## `deauth` / `da` — Deauth Attack

```
CMD> da <bssid|index> [channel] [client_mac]
CMD> da 2                          # broadcast deauth at AP from last scan (channel auto)
CMD> da AA:BB:CC:DD:EE:FF 6        # by BSSID on channel 6
CMD> da 2 6 11:22:33:44:55:66      # target a specific client
```

Sends raw 802.11 deauthentication frames to disconnect clients from an AP. Both deauth (0xC0) and disassoc (0xA0) frames are sent in bursts.

- **Index**: use the number from the last `sw` scan — channel is auto-detected
- **Broadcast**: without a client MAC, all connected clients are targeted
- **Directed**: with a client MAC, only that client is targeted

Shows live frame count (sent / failed). Press `q` to stop.

---

## `hiddenssid` / `hs` — Reveal Hidden SSID

```
CMD> hs <index|bssid> [channel] [silent]
CMD> hs 4
CMD> hs AA:BB:CC:DD:EE:FF 11
CMD> hs 4 11 silent
```

Reveals the SSID of a hidden network by:
1. Sending deauth bursts every 3 seconds to force clients to re-associate
2. Sniffing probe responses and association requests for the target BSSID
3. Extracting the SSID from the captured frame

When found, T-Rex beeps (unless `silent`), displays the SSID, and saves it to `/logs/hidden_ssids.csv`. The SSID then appears as `~name` in cyan on the next `sw` scan.

Press `q` to stop without waiting.

---

## `wpasniff` / `ws` — WPA2 Handshake Capture + Crack

```
CMD> ws <index|bssid> [channel]
CMD> ws 2
CMD> ws AA:BB:CC:DD:EE:FF 6
```

Captures a WPA2 4-way handshake (EAPOL M1+M2) and optionally cracks it on-device.

**Step 1 — Capture**

T-Rex sets the radio to monitor mode on the target channel and sends deauth frames every 4 seconds to force clients to re-authenticate. When both M1 and M2 EAPOL frames are captured from the same exchange, the handshake is complete.

Status shows: `[M1] waiting...` → `[M1+M2] COMPLETE`

**Step 2 — Crack (optional)**

Press `c` after capture to start cracking.

T-Rex tries passwords using PBKDF2-SHA1 (the actual WPA2 KDF), deriving the PMK then PTK then MIC, and comparing against the captured MIC.

| Wordlist source | Path | Behaviour |
|-----------------|------|-----------|
| SD wordlist | `/wordlist.txt` | Tried first, unlimited size |
| Built-in list | (embedded) | 101 most common WPA passwords, used as fallback |

Results are saved to `/logs/cracked.csv`.

Press `q` at any time to stop capture or cracking.

---

## `eviltwin` / `et` — Evil Twin AP + Captive Portal

```
CMD> et
CMD> eviltwin
```

T-Rex scans nearby networks and shows a picker. Pick a target and T-Rex automatically applies the right strategy:

| Target | MAC strategy | Deauth target | Result |
|--------|-------------|---------------|--------|
| Open network | Clone real AP's MAC + channel | Real AP BSSID | Devices reconnect to fake AP seamlessly |
| WPA/WPA2 network | Random locally-administered MAC | Real AP BSSID | Deauth hits real AP; portal clients are unaffected |

**Adaptive deauth** — bursts every 8 seconds, automatically pauses when a device connects to the portal so credentials can be submitted uninterrupted.

**Custom portal pages** — drop `.html` files into `/evilportal/` on the SD card. Press `[s]` while running to switch templates live.

| Key | Action |
|-----|--------|
| `c` | View captured credential table (portal keeps running) |
| `s` | Flush captures to SD |
| `q` | Stop Evil Twin |

Credentials are stored in memory (up to 20 entries) and appended live to `/logs/eviltwin.csv`.

Captive portal popup works automatically on Android and iOS. On Windows, click the "Additional sign-in required" toast or the Sign in button in WiFi Settings.

---

## `macchanger` / `mc` — MAC Spoofer

```
CMD> mc on              # enable spoofing (random MAC applied on next WiFi op)
CMD> mc off             # disable, restore real hardware MAC
CMD> mc random          # apply a new random MAC immediately
CMD> mc set AA:BB:CC:DD:EE:FF   # apply a specific MAC
CMD> mc status          # show current MAC and spoofing state
```

Spoofs the STA MAC address at the driver level. The spoofed MAC is a locally-administered unicast address (bit 1 of byte 0 set, bit 0 clear).

**Hooks** — when enabled, MAC spoofing is automatically applied before:
- `scanwifi` / `sw`
- `connectwifi` / `cw`
- `deauth` / `da`
- `wpasniff` / `ws`

Config is saved to `/macchanger.cfg` on the SD card and restored on boot.
