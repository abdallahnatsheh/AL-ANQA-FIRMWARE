---
title: Workflows
nav_order: 4
---

# Workflows

Real-world usage examples that combine multiple commands into complete tasks. Each workflow shows the exact sequence of commands and what to expect at each step.

---

## Workflow 1 — WPA2 Handshake Capture + Crack

**Goal:** Capture a WPA2 handshake from a target network and crack the password on-device.

**Requirements:** WiFi connection to T-Rex itself is not needed. The target network just needs to have at least one client connected.

### Steps

**1. Scan for target networks**
```
CMD> sw
```
A paginated table appears. Note the **index** of your target network (leftmost column).

**2. (Optional) Spoof your MAC before transmitting**
```
CMD> mc random
```
This randomizes your MAC before any frames are sent. Undo later with `mc off`.

**3. Start capture**
```
CMD> ws 2              # replace 2 with your target's index
```
Or by BSSID if you already know it:
```
CMD> ws AA:BB:CC:DD:EE:FF 6
```

T-Rex locks to the target's channel, enters monitor mode, and sends deauth bursts every 4 seconds to force clients to re-authenticate. Watch the status:
```
[M1] waiting...        ← waiting for first EAPOL frame
[M1+M2] COMPLETE       ← handshake captured
```
The handshake is saved to `/apps/wpasniff/<BSSID>.cap`.

**4. Crack the password**

Press **`c`** immediately after capture completes. T-Rex starts cracking:

```
Trying wordlist: /apps/wpasniff/wordlist.txt
[142/8432] testing: password123...
CRACKED: letmein2024
```

| Wordlist | Location | Notes |
|----------|---------|-------|
| Your custom list | `/apps/wpasniff/wordlist.txt` on SD | Tried first, unlimited size |
| Built-in fallback | Embedded | 101 common passwords |

Results are saved to `/apps/wpasniff/cracked.csv`.

**5. Press `q` to stop** at any time (capture or cracking).

> **Tip:** The `.cap` file is aircrack-ng / hashcat compatible — copy it to a PC via `usbmsc` for GPU-accelerated cracking if on-device cracking fails.

---

## Workflow 2 — Network Reconnaissance

**Goal:** Discover all hosts on a local network, find open ports, and identify services and OS.

**Requirements:** Active WiFi connection (`cw` first).

### Steps

**1. Connect to the target network**
```
CMD> cw 3              # connect to network at scan index 3
```

**2. Discover live hosts**
```
CMD> nd
```
ARP-scans the full /24 subnet. Results show each host's IP and MAC. Note the **index** numbers — you'll use them instead of typing IPs.

```
[0] 192.168.1.1   AA:BB:CC:DD:EE:FF    (gateway)
[1] 192.168.1.5   11:22:33:44:55:66
[2] 192.168.1.12  CC:DD:EE:FF:00:11
```

**3. Quick top-port scan on the most interesting host**
```
CMD> ts 0              # top 26 common ports on the gateway
```
Results show open ports with service names. OS fingerprinting runs automatically.

**4. Full port scan on a specific host**
```
CMD> ps 1 1 65535      # full scan of host at index 1
```
For faster results on common ranges:
```
CMD> ps 1 1 1024       # well-known ports only
```

**5. Grab service banners**

While viewing port scan results, press **`b`** on any open port to grab its banner:
- HTTP: shows server header
- SSH: shows version string
- MySQL: shows greeting
- Redis: `PONG` response

**6. Ping a host to check latency**
```
CMD> pg 0              # ping host at index 0
CMD> pg google.com     # or by hostname for internet test
```

> **Tip:** `show hosts` re-displays the last `nd` result without rescanning. Useful when switching between `ts`, `ps`, and `pg` without running `nd` again.

---

## Workflow 3 — Monitor Your Network for Attacks (Background IDS)

**Goal:** Run wguard in the background while using other tools — get notified the moment an attack is detected against your AP.

**Requirements:** Active WiFi connection. Know your AP's scan index.

### Steps

**1. Scan to get your AP's index**
```
CMD> sw
```
Note the index of the network you want to protect.

**2. Start wguard in background mode**
```
CMD> wg 0 bg           # monitor AP at index 0 in background
```

T-Rex returns immediately to the prompt. The **shield icon** in the status bar turns green — wguard is running silently.

**3. Use other commands normally**

While wguard monitors in the background, you can run anything else:
```
CMD> nd                # discover hosts
CMD> ts 0              # scan ports
CMD> sbl               # scan BLE devices
```

**4. Read threat alerts**

When a threat is detected:
- A **coloured bar** appears at the bottom of the screen for 4 seconds:
  - **Yellow bar** = WARNING (deauth burst, foreign AP, auth flood, etc.)
  - **Red bar** = CRITICAL (deauth storm, handshake harvest, BSSID clone)
- The **shield icon** changes colour to match the severity
- A notification sound plays (T-Deck Plus, if not muted)

**5. View the live event log**
```
CMD> wg view           # enter live view without stopping monitoring
```
Scroll with trackball up/down. Press `s` to save events to SD. Press `q` to return to the prompt — monitoring continues.

**6. Save the full session log**

Press `s` while in `wg view`, or wguard saves automatically every 2 minutes and on session end.

```
CMD> wg stop           # stop monitoring and trigger final save
```

Session log: `/apps/wguard/001.csv` (new numbered file each session, never overwritten).

> **Tip:** wguard bg blocks other WiFi commands while running. Run `wg stop` before using `deauth`, `eviltwin`, or `wpasniff`. BLE commands (`sbl`, `trackme`, `buddy`) work alongside it fine.

---

## Workflow 4 — Detect a Physical Tracker

**Goal:** Detect if a BLE or WiFi device is physically following you. Best used on the T-Deck Plus with GPS.

**Requirements:** T-Deck Plus recommended (GPS significantly improves accuracy). Standard T-Deck works for BLE-only detection.

### Steps

**1. (T-Deck Plus) Pre-warm the GPS**

GPS needs ~4 minutes for a cold fix. Start it before leaving:
```
CMD> gps on
```
Watch the satellite icon: grey → yellow (searching) → green (fixed). Once green, `trackme` skips its own GPS warm-up and starts immediately.

**2. Start trackme BEFORE you leave your current location**

This is important — the first 60 seconds are the **baseline period** where all devices currently around you (your phone, watch, car BT) are learned as companions and ignored.

```
CMD> trackme           # with speaker alerts (T-Deck Plus)
CMD> tm silent         # silent mode
```

The display shows: `BASELINE 47s — learning your devices...`

Stay where you are during this countdown.

**3. Move around normally**

After baseline ends, trackme starts scoring new devices that appear. The GPS tracks how far you've moved — any device that keeps following you after 200m of movement gets promoted from NOTICE to WARNING or ALERT.

**4. Interpret the display**

Devices are listed by threat level:
- **Grey** — companion (baselined, your own device) or not suspicious
- **Yellow** — NOTICE: suspicious but Gate 3 not yet confirmed
- **Orange** — WARNING: confirmed, score 60–79
- **Red** — ALERT: confirmed, known tracker or score ≥ 80

**5. If you get a WARNING or ALERT**

The speaker plays beeps. On screen you see the MAC address and (for known trackers) the brand.

If it's a false positive (your own device that wasn't present during baseline):
```
w      # whitelist the highest-threat non-companion device
```
The MAC is saved to `/apps/trackme/known.csv` permanently.

**6. Save the session log**
```
s      # save current session to SD
q      # quit
```

Session log: `/apps/trackme/session.csv`

> **Tip:** Press `v` to check the Tier 2 (background) pool, `o` to sort by score/RSSI, `f` to filter Tier 1 down to alerts only, or `h` for a full-screen legend of colors and keys.

> **Tip:** If you suspect an AirTag specifically — look for ALERT level devices with the Apple company ID (`0x004C`). AirTags use stable MACs (they don't randomize) so they're reliably detected from the first sighting.
