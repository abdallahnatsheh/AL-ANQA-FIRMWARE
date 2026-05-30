---
title: WGuard IDS
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 5
---

# WGuard — WiFi Intrusion Detection System

> Passive monitoring only — wguard never injects any frames. Running it is invisible to the target network.

```
CMD> sw                           # scan first to get AP index
CMD> wg <index|bssid> [ch]        # interactive mode
CMD> wg <index|bssid> [ch] bg     # background mode — returns to prompt
CMD> wg view                      # enter live view of running bg session
CMD> wg stop                      # stop background session
```

---

## How It Works

`wguard` locks onto a single AP (by index from `sw`, or by BSSID + channel directly) and puts the radio into **promiscuous mode** on that channel. It captures every 802.11 management and data frame in the air and runs each frame through a set of detection rules.

The channel is fixed for the duration of the session — wguard does not hop. This is intentional: hopping would miss the rapid burst sequences that most attacks produce.

Frame counters shown in the header update in real time:
- **Bcn** — beacon frames from the target AP
- **Prb** — probe requests seen on the channel
- **Ath** — authentication frames
- **Dth** — deauthentication / disassociation frames
- **EAP** — EAPOL frames (WPA2 4-way handshake)

---

## Display

**Interactive mode** shows a live cyberpunk-style header, a status line (`OK` / `WARNING` / `CRITICAL`), the frame counters, and a scrollable event log with session-relative timestamps.

```
[WGRD::IDS]  MyNetwork  ch6  AA:BB:CC:DD:EE:FF
Bcn:420  Prb:31  Ath:4  Dth:210  EAP:8    STATUS: WARNING
────────────────────────────────────────────────
EVENTS [2/5]
00:01:04  WARNING  -36  BCAST DEAUTH AA:BB:CC:DD:EE:FF
00:01:50  WARNING  -47  EVIL TWIN+DTH CC:DD:EE:FF:00:11
00:02:14  CRITICAL -75  HANDSHAKE harvest M1+M2
────────────────────────────────────────────────
[↑/↓]scroll  [s]save  [q]quit
```

Timestamps are **session-relative** — `00:01:04` = 1 min 4 sec after `wg` started.
`rssi_dbm` shows the signal strength of the triggering frame — useful to estimate attacker proximity.

---

## Detection Algorithms

### BCAST DEAUTH
**Trigger:** 5+ broadcast deauthentication frames from the same source MAC within 5 seconds.

Broadcast deauths (destination `FF:FF:FF:FF:FF:FF`) disconnect all clients simultaneously. A single deauth is normal (roaming). A burst at this rate is characteristic of a deauth attack script.

---

### DEAUTH Storm
**Trigger:** 15+ targeted deauthentication frames from the same source MAC within 5 seconds.

Targeted at a specific client MAC rather than broadcast. High-rate targeted deauths are the signature of an active client-eviction attack (forcing re-association to capture a handshake, or as an aggressive DoS).

---

### AUTH Flood
**Trigger:** 32+ unique source MACs sending authentication frames within 10 seconds.

An authentication flood sends fake auth requests from randomly generated MACs to fill the AP's association table. A legitimate network sees at most a handful of auth frames in any 10-second window.

---

### PROBE Storm
**Trigger:** 50+ probe request frames from the same MAC within 5 seconds.

A single device probing at this rate is either a pentest tool actively mapping the channel, or a malfunctioning device. Normal clients probe a handful of times per minute.

---

### PMKID Grab
**Trigger:** 5+ rapid association requests from the same MAC within 5 seconds.

PMKID attacks send rapid association requests to extract the PMKID from the AP's response without ever completing the 4-way handshake. Distinguishable from a normal re-association by the burst rate.

---

### EVIL TWIN (two-tier)

Detection is two-tier to reduce false positives from legitimate range extenders.

**Tier 1 — INFO (pending):** A foreign beacon is seen advertising the exact same SSID as the monitored AP, from a different BSSID. This alone fires an `INFO` event and starts a 3-second observation window.

**Tier 2 — WARNING (confirmed):** If deauthentication frames arrive concurrently with the foreign beacon (the attacker is actively driving clients toward the fake AP), the event is upgraded to `WARNING` → `EVIL TWIN+DTH`.

Additional filters:
- **RSSI filter > −82 dBm** — distant APs below this threshold are logged as INFO only, never promoted. A legitimate range extender at −90 dBm won't false-fire.
- **3-second beacon silence expiry** — if the rogue AP disappears and restarts, wguard re-detects it after 3 seconds of silence rather than treating it as a persistent threat.
- **OUI check** — an AP with the same SSID but a vendor OUI matching a known mesh/enterprise vendor is logged as `CO-AP` (INFO), not an evil twin.

---

### HANDSHAKE Harvest
**Trigger:** An EAPOL exchange (M1 + M2 or M2 + M3) is seen following a deauthentication burst within a short time window.

The classic handshake-capture attack: send deauths to force re-association, then capture the EAPOL frames. wguard detects the combination — deauth burst followed by EAPOL — as CRITICAL.

---

### BSSID Clone
**Trigger:** The BSS timestamp in beacon frames from the monitored BSSID goes **backward** (Δts < 0).

Every AP increments its BSS timestamp continuously from boot. If two radios are advertising on the same BSSID (a clone scenario), the timestamps from the two radios will be out of sync — wguard sees a backward jump when frames from each radio interleave in the capture.

> **Clock-skew note:** A rebooting AP also resets its timestamp to ~0, triggering both a backward-jump detection and a clock-skew anomaly simultaneously. This is indistinguishable from a real BSSID clone at the firmware level. wguard fires the detection but the RSSI and beacon interval data in the event log can help a human operator decide which scenario it is.

---

### BEACON FLOOD
**Trigger:** 100+ unique BSSIDs seen advertising on the channel within 30 seconds.

The signature of a `beaconflood` / `bf` attack. A normal busy environment has at most 20–30 APs visible on any channel.

---

### KARMA Attack
**Trigger:** The same BSSID responds to 3+ different probe request SSIDs within 60 seconds.

A KARMA attack makes a rogue AP respond affirmatively to any SSID a device probes for, allowing it to impersonate any network. wguard watches for probe responses — not just beacons — from a fixed BSSID and counts unique SSIDs answered.

> Real-world testing note: KARMA detection requires probe-response sniffing. Depending on timing, some KARMA implementations may not trigger the threshold within the 60-second window. Consider this detection experimental.

---

## Rate Limiting

To prevent log and notification spam during sustained attacks:
- Each detection fires **at most once per 30 seconds** per source MAC
- Notification sounds are further throttled: **WARNING ≤ 1 per 10s**, **CRITICAL ≤ 1 per 5s** (via `notifyThrottled()`)

The event log still records every individual event — rate limiting only affects notifications and screen popups.

---

## Background Mode

`wg <idx> bg` returns to the command prompt immediately. wguard keeps sniffing in the background while you use other commands.

The **shield icon** in the status bar reflects the current threat level:

| Colour | Meaning |
|--------|---------|
| Grey | Not running |
| Green | Running — no threats detected |
| Yellow | WARNING level event detected |
| Red | CRITICAL event detected |

When a threat fires in background mode a **coloured popup bar** appears at the bottom of the screen for 4 seconds and a notification sound plays. INFO events are silent.

`wg view` enters the live event view from any screen. `q` returns to the prompt without stopping the session.

**WiFi lock** — while wguard bg is active, other WiFi commands (`scanwifi`, `deauth`, `eviltwin`, etc.) are blocked. Run `wg stop` first. `trackme` BLE scanning works alongside wguard bg — only the WiFi probe-sniff phase of `trackme` is skipped automatically.

---

## Session Logs

Each `wg` run creates a new numbered file: `/logs/wguard/001.csv`, `/002.csv`, etc. The number increments on every boot and session start — **files are never overwritten**.

### CSV format

```
# SESSION 3  wguard "MyNetwork"  bssid=AA:BB:CC:DD:EE:FF  ch6  uptime=12s
time,severity,rssi_dbm,message
# CHECKPOINT 1  Bcn=420 Prb=31 Ath=4 Dth=210 EAP=8
00:01:04,WARNING,-36,BCAST DEAUTH AA:BB:CC:DD:EE:FF
00:01:50,WARNING,-47,EVIL TWIN+DTH CC:DD:EE:FF:00:11
00:02:14,CRITICAL,-75,HANDSHAKE harvest M1+M2
# FINAL SAVE  total=3 events  maxSev=CRITICAL  duration=4m22s
```

### Save types

| Type | When | Ring cleared? |
|------|------|---------------|
| **AUTO-SAVE** | Ring hits 128 events | Yes — continues fresh |
| **CHECKPOINT** | Every 2 minutes (skipped if nothing new) | No |
| **MANUAL** (`s` key) | You press `s` — footer shows `Saved N events` or `Nothing new to save` | No |
| **FINAL** | Session ends (quit / `wg stop`) | No |

Each save block writes only **new events since the last save** — no duplicates across blocks.

---

## Keys (Interactive Mode)

| Key | Action |
|-----|--------|
| Trackball ↑ / ↓ | Scroll event history (5 lines per step) |
| `s` | Save new events to SD |
| `q` | Quit — triggers final save automatically |

---

## Resources

### Deauth / Disassoc attacks
- [IEEE 802.11-2020 §6.3.9](https://standards.ieee.org/ieee/802.11/7028/) — deauthentication frame specification (Management frame type 0xC0)
- [aircrack-ng `aireplay-ng --deauth`](https://www.aircrack-ng.org/doku.php?id=deauthentication) — reference implementation of the deauth flood technique
- [CVE-2012-2619](https://nvd.nist.gov/vuln/detail/CVE-2012-2619) — broadcast deauth DoS against 802.11n access points

### PMKID attack
- [Jens Steube — "New attack on WPA/WPA2 using PMKID" (2018)](https://hashcat.net/forum/thread-7717.html) — original discovery; no full handshake needed, PMKID extracted from EAPOL frame 1
- [`hcxdumptool`](https://github.com/ZerBea/hcxdumptool) — Linux tool that performs the same PMKID capture wguard detects
- [`hashcat` mode 22000](https://hashcat.net/wiki/doku.php?id=hashcat) — cracking WPA2 PMKID hashes offline

### WPA2 Handshake Harvest
- [WPA2 4-way handshake (IEEE 802.11i)](https://en.wikipedia.org/wiki/IEEE_802.11i-2004) — EAPOL message sequence M1–M4 and MIC derivation
- [`aircrack-ng` suite](https://www.aircrack-ng.org) — captures and cracks `.cap` files produced by wguard / wpasniff
- [`hashcat` EAPOL cracking](https://hashcat.net/wiki/doku.php?id=hashcat) — GPU-accelerated offline cracking of captured handshakes

### Evil Twin / Rogue AP
- [Mathy Vanhoef — KRACK attacks (2017)](https://www.krackattacks.com) — exploits WPA2 handshake; evil twin is a prerequisite in some variants
- [Captive portal attack taxonomy — DEF CON 22](https://www.defcon.org/html/defcon-22/dc-22-speakers.html#Westervelt) — evil twin + captive portal phishing methodology
- [RFC 7710](https://datatracker.ietf.org/doc/html/rfc7710) — Captive-Portal identification header used by wguard's eviltwin module

### KARMA Attack
- [Dino Dai Zovi & Shane Macaulay — "The Karma Attack" (2004)](https://web.archive.org/web/20160409054731/http://www.theta44.org/karma/) — original KARMA paper; rogue AP responds to any SSID probe
- [`hostapd-wpe`](https://github.com/OpenSecurityResearch/hostapd-wpe) — WPA enterprise KARMA + credential harvesting tool
- [`airbase-ng`](https://www.aircrack-ng.org/doku.php?id=airbase-ng) — Linux tool implementing KARMA mode that wguard's detection targets

### BSSID Cloning / BSS Timestamp
- [IEEE 802.11-2020 §9.4.1.10](https://standards.ieee.org/ieee/802.11/7028/) — BSS timestamp field definition (microseconds since AP boot)
- [Wi-Fi Alliance — AP cloning attack surface](https://www.wi-fi.org/security) — background on BSSID spoofing and its implications for WPA2-Enterprise

### Beacon Flood
- [CVE-2019-15126 ("kr00k")](https://nvd.nist.gov/vuln/detail/CVE-2019-15126) — flooding as a DoS precursor to force all-zero key decryption
- [`mdk4`](https://github.com/aircrack-ng/mdk4) — Linux beacon flood tool (mode `b`) that wguard's 100-BSSID/30s threshold was tuned against

### AUTH Flood
- [AP Association Table Exhaustion (2009)](https://www.cs.umd.edu/~waa/pubs/woot09.pdf) — original research on filling AP association tables to deny service to legitimate clients
- [`mdk4` mode `a`](https://github.com/aircrack-ng/mdk4) — authentication flood tool; sends fake auth from spoofed MACs at high rate
