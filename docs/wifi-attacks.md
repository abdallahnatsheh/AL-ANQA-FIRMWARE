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

---

## `wguard` / `wg` — WiFi IDS (Intrusion Detection)

`wguard` is a passive WiFi intrusion-detection system. It locks onto one AP and monitors all 802.11 management and data frames on that channel, detecting deauth floods, evil twin APs, handshake harvesting, Karma attacks, PMKID grabs, auth floods, probe storms, beacon floods, and BSSID cloning.

```
CMD> sw                       # scan first to get AP index
CMD> wg 2                     # interactive monitor on AP index 2
CMD> wg 2 bg                  # background mode — returns to prompt
CMD> wg view                  # enter live view of running bg session
CMD> wg stop                  # stop background session
CMD> wg AA:BB:CC:DD:EE:FF 6   # target by BSSID + channel directly
```

### Interactive mode

The screen shows a live cyberpunk header, status line (OK / WARNING / CRITICAL), frame counters (Bcn / Prb / Ath / Dth / EAP), and a scrollable event log with session-relative HH:MM:SS timestamps.

| Key | Action |
|-----|--------|
| Trackball ↑ / ↓ | Scroll event history (5 lines per step) |
| `s` | Save new events to SD — footer briefly shows `Saved N events` or `Nothing new to save` |
| `q` | Quit (also triggers final save) |

The event list shows page position `EVENTS [2/5]` when history spans multiple pages.

### Background mode

`wg <idx> bg` returns to the command prompt immediately. T-Rex keeps sniffing in the background while you use other commands.

The shield icon in the status bar reflects the current threat level:

| Colour | Meaning |
|--------|---------|
| Grey | wguard not running |
| Green | Running — no threats |
| Yellow | Running — WARNING detected |
| Red | Running — CRITICAL alert |

When a threat fires in background mode a coloured bar appears at the bottom of the screen for 4 seconds and a notification sound plays. INFO events are silent.

**WiFi lock** — while wguard bg is active, other WiFi commands are blocked. Run `wg stop` first, or use `wg view` to watch live, then `q` to return without stopping monitoring.

**TrackMe** — BLE scanning works alongside wguard bg. On T-Deck Plus the WiFi probe-sniff phase of `trackme` is skipped automatically.

### Detections

| Event | Trigger | Severity |
|-------|---------|----------|
| BCAST DEAUTH | 5 broadcast deauth frames from same MAC in 5 s | WARNING |
| DEAUTH storm | 15 targeted deauth frames from same MAC in 5 s | CRITICAL |
| AUTH flood | 32 unique MACs authenticating in 10 s | WARNING |
| PROBE storm | 50 probe requests from same MAC in 5 s | WARNING |
| PMKID grab | 5 rapid association requests from same MAC in 5 s | WARNING |
| EVIL TWIN+DTH | Foreign AP with your SSID + concurrent deauths | WARNING |
| FOREIGN AP | Foreign AP with your SSID, no deauths yet (pending) | INFO |
| CO-AP | Same SSID, same vendor OUI — mesh/enterprise node | INFO |
| HANDSHAKE harvest | EAPOL exchange after deauth burst | CRITICAL |
| BSSID CLONED | BSS timestamp went backward — two radios on same BSSID | CRITICAL |
| BEACON FLOOD | 100+ unique BSSIDs in 30 s | WARNING |
| KARMA | Same BSSID responding to 3+ different SSIDs in 60 s | WARNING |

**Evil twin detail** — wguard checks both SSID (must match exactly) and signal strength (beacon RSSI must be stronger than −82 dBm). Legitimate extenders that are far away at −90+ dBm are logged as INFO only and never promoted to WARNING. A rogue AP that disappears and restarts is re-detected after 3 seconds of silence.

**Rate limiting** — to prevent log and notification spam during sustained attacks, each detection fires at most once per 30 seconds per source MAC. Notification sounds are further throttled: WARNING ≤ 1 per 10 s, CRITICAL ≤ 1 per 5 s.

### Session logs

Each `wg` run creates a new numbered file — `/logs/wguard/001.csv`, `/002.csv`, etc. The number increments on every boot and session start so files are never overwritten.

**CSV format:**
```
# SESSION 3  wguard "MyNetwork"  bssid=XX:XX:XX:XX:XX:XX  ch6  uptime=12s
time,severity,rssi_dbm,message
# CHECKPOINT 1  Bcn=420 Prb=31 Ath=4 Dth=210 EAP=8
time,severity,rssi_dbm,message
00:01:04,WARNING,-36,BCAST DEAUTH XX:XX:XX:XX:XX:XX
00:01:50,WARNING,-47,EVIL TWIN+DTH XX:XX:XX:XX:XX:XX
00:02:14,CRITICAL,-75,HANDSHAKE harvest M1
# FINAL SAVE 2  Bcn=890 Prb=61 Ath=6 Dth=398 EAP=14
time,severity,rssi_dbm,message
00:04:30,WARNING,-52,BCAST DEAUTH XX:XX:XX:XX:XX:XX
# SESSION END  total=9 events  maxSev=CRITICAL  ch=6  duration=4m55s  Bcn=890 ...
# THREATS  evil_twin=1  deauth_storm=2  karma=0  clone=0  beacon_flood=0
```

Timestamps are **session-relative** — `00:01:04` means 1 min 4 sec after `wg` was started.

The `rssi_dbm` column shows the signal strength of the triggering frame — useful for estimating attacker proximity.

**Automatic saves** — wguard saves without any action from you. Each save block writes only the **new events since the last save** — no duplicates.

| Type | When | Ring cleared? |
|------|------|---------------|
| AUTO-SAVE | Ring hits 128 events | Yes — continues fresh |
| CHECKPOINT | Every 2 minutes | No — ring stays for display |
| MANUAL (`[s]`) | You press `s` | No |
| FINAL | Session ends (quit / stop) | No |

All saves append to the same session file so the full session history is always in one place.

---

## `beaconflood` / `bf` — Beacon Flood

```
CMD> bf
```

Injects hundreds of fake 802.11 beacon frames per second — each with a different SSID and randomly generated MAC address. Every device in range sees its WiFi scan list flooded with fake networks.

Type `bf` to open the mode picker:

| Key | Mode | What it floods |
|-----|------|---------------|
| `1` | list | 40 built-in humorous SSIDs (Abraham Linksys, FBI Surveillance Van…) |
| `2` | rickroll | Never Gonna Give You Up lyrics as sequential SSIDs |
| `3` | seq | Your base name + incrementing number (`Starbucks1`, `Starbucks2`…) |
| `4` | file | One SSID per line from `/wordlist_beacons.txt` on SD |
| `5` | clone | Pick a real network from last `sw` scan — flood its exact SSID |

Live display shows channel, total frames sent, error count, and rate (~90–100 frames/sec typical).

Press `q` to stop. WiFi is restored to STA mode cleanly on exit.

> **Note:** Cannot run simultaneously with `wguard` — both require promiscuous mode. To test wguard's beacon flood detection, use a second device running `bf`.

For full usage and technical detail see the [Beacon Flood guide](beacon-flood).
