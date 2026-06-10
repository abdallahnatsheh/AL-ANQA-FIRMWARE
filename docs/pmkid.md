---
title: PMKID Attack
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 5
---

# PMKID Attack

## `pmkid` / `pm` — PMKID Capture + Crack

Captures the PMKID from a single EAPOL M1 frame and optionally cracks the WPA2 password on-device. **No client required** — stealthier than WPA handshake capture (`ws`).

```
CMD> pm <index|bssid> [channel]
CMD> pm 2
CMD> pm AA:BB:CC:DD:EE:FF 6
```

---

### How PMKID works

The AP embeds a PMKID in every EAPOL M1 frame it sends to associating clients:

```
PMKID = HMAC-SHA1-128(PMK, "PMK Name" || AP_MAC || STA_MAC)
```

Because the PMKID is derived directly from the PMK (which comes from the password), cracking it only requires one M1 frame — no M2, no deauth, no client interaction needed.

---

### Step 1 — Capture

```
CMD> sw          ← scan first to build index
CMD> pm 2        ← target AP at index 2
```

T-Rex monitors the target channel passively. The AP sends M1 whenever any client naturally associates. When a PMKID is found in the Key Data, the screen shows:

```
[PMKID CAPTURED!]
A1B2C3D4E5F6A7B8   ← first 8 bytes preview
[c] crack   [q] stop
```

> **Note:** Not all routers include the PMKID KDE in M1. If you see `M1 seen — no PMKID in Key Data`, the router does not support this attack — use `ws` instead.

---

### Step 2 — Crack

Press `c` after capture to start on-device cracking.

T-Rex computes `PBKDF2-SHA1(passphrase, SSID, 4096) → PMK → HMAC-SHA1-128("PMK Name"||AP||STA)` and compares against the captured PMKID.

| Wordlist source | Path | Behaviour |
|-----------------|------|-----------|
| SD wordlist | `/apps/pmkid/wordlist.txt` | Tried first, unlimited size |
| Built-in list | (embedded) | 101 common WPA passwords, used as fallback |

Results are saved to `/apps/pmkid/cracked.csv` with a `PMKID` tag to distinguish from handshake cracks.

---

### Offline cracking with hashcat

Pull the PCAP from SD and convert:

```bash
hcxpcapngtool -o hash.hc22000 /apps/pmkid/AA-BB-CC-DD-EE-FF.cap
hashcat -m 22000 hash.hc22000 wordlist.txt
```

---

### Keys

| Key | Action |
|-----|--------|
| `c` | Start cracking (after PMKID captured) |
| `q` | Stop capture or cracking |

---

### Files

| Path | Contents |
|------|----------|
| `/apps/pmkid/<BSSID>.cap` | Raw 802.11 PCAP (libpcap, linktype 105) |
| `/apps/pmkid/cracked.csv` | Cracked PMKID passwords |
| `/apps/pmkid/wordlist.txt` | Custom wordlist (one password per line, ≥8 chars) |

---

### vs WPA Sniff (`ws`)

| | `pm` (PMKID) | `ws` (Handshake) |
|--|-------------|-----------------|
| Frames needed | M1 only | M1 + M2 |
| Client required | No | Yes (or deauth to force) |
| Deauth sent | No | Yes, every 4s |
| Router support | ~80% of modern routers | Universal |
| Stealth | Higher | Lower |
