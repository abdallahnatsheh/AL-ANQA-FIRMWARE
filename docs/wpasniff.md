---
title: WPA Sniff
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 4
---

# WPA Sniff

## `wpasniff` / `ws` — WPA2 Handshake Capture + Crack

Captures a WPA2 4-way handshake (EAPOL M1+M2) and optionally cracks it on-device.

```
CMD> ws <index|bssid> [channel]
CMD> ws 2
CMD> ws AA:BB:CC:DD:EE:FF 6
```

### Step 1 — Capture

T-Rex sets the radio to monitor mode on the target channel and sends deauth frames every 4 seconds to force clients to re-authenticate.

Status: `[M1] waiting...` → `[M1+M2] COMPLETE`

### Step 2 — Crack

Press `c` after a successful capture to start on-device cracking.

T-Rex computes PBKDF2-SHA1(passphrase, SSID, 4096) → PMK → PTK → KCK → HMAC-SHA1 MIC and compares against the captured MIC.

| Wordlist source | Path | Behaviour |
|-----------------|------|-----------|
| SD wordlist | `/wordlist.txt` | Tried first, unlimited size |
| Built-in list | (embedded) | 101 common WPA passwords, used as fallback |

Results are saved to `/logs/cracked.csv`. The handshake pcap is written to `/logs/hs/<BSSID>.cap` (aircrack-ng / hashcat hcxpcapngtool compatible).

### Keys

| Key | Action |
|-----|--------|
| `c` | Start cracking (after capture complete) |
| `q` | Stop capture or cracking |
