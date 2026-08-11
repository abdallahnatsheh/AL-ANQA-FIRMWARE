---
title: Pwnagotchi Pet
lang: en
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 12
---

# Autonomous Capture + Crack Pet — `pwn` / `pw`
{: .no_toc }

An unattended, channel-roaming "pet" that captures WPA/WPA2 handshakes and PMKIDs **and cracks them on the device** during idle time. Unlike every other ESP32 pwnagotchi (which only *capture* and leave cracking to a laptop), `pwn`'s triumphant **PWNED** state means it actually recovered a **real password** — the first pwnagotchi to close the capture→crack loop on-device.

**Own / authorized networks only.**

1. TOC
{:toc}

---

## What it does

Each cycle the pet:

1. **Roams** WiFi channels (1/6/11 by default, or all 1–13) and passively listens.
2. **Captures** crackable key material — full/half 4-way handshakes (deauth-assisted) and PMKIDs (from the first handshake message), saved as standard `.cap` files.
3. **Cracks** during idle moments, using a resume cursor so it picks up exactly where it left off — even across reboots — and a smart priority order so likely passwords are tried first.

Its mood face reflects **real** events: hunting when targets appear, excited on a capture, and a green **PWNED** flash when a password actually breaks.

> **Honest limits:** `pwn` is not a GPU cracker. It only breaks weak / common / wordlist-present passwords on-device; everything else is saved as a `.cap` for offline cracking with `crack` (`cc`) or hashcat, exactly like other tools. The pet only claims PWNED when it has *verified* a real key.

---

## Usage

```
CMD> pwn                 # active mode (deauth-forced captures, loud)
CMD> pwn stealth         # quiet, low IDS signature
CMD> pwn passive         # sniff-only, zero transmit (undetectable)
CMD> pwn full            # roam all channels 1-13 (default is 1/6/11)
```

### Modes

| Mode | Capture | Grid | Trade-off |
|------|---------|------|-----------|
| **active** (default) | broadcast deauth (loud) | **on** | best capture rate; trips deauth IDS |
| **passive** | sniff-only, no attack | **on** | captures only what clients leak; still social |
| **stealth** | quiet directed deauth | **off — dark** | the one truly undetectable mode |

Toggle live with **`[m]`**. `pwn` **never** emits the pwnagotchi `de:ad:be:ef` pwngrid beacon, so pwnagotchi detectors (nzyme, Kismet, Marauder) cannot fingerprint it — only our own private grid beacon (below), and only when not in stealth.

### Grid — AL-ANQA pets greet each other

Run `pwn` on **two or more T-Decks** and they find each other on the air and show each other's stats — the social side of a pwnagotchi.

- Each deck has an identity `A2:9A:0A:xx:xx:xx` (AL-ANQA prefix + its own MAC tail) and a name `ANQA-XXXX`, shown on screen. Override the name in `/apps/pwn/grid.conf` (`name=MyPet`).
- **Broadcast** happens only in **active** and **passive** (both "social/visible"); **stealth stays dark** — it never transmits the grid beacon, so it's undetectable, but it still **listens** and shows peers one-way.
- The HUD shows `g<N>` (peer count) on the mode line, and a `met ANQA-XXXX` message + a happy phoenix pose when a new pet appears.

Uses a private beacon frame (not ESP-NOW libraries) received in pwn's own sniffer — no extra radio setup. Cooperation (shared dedup / handshake-swap) is planned for a later version.

### On-screen controls

| Key | Action |
|-----|--------|
| `[m]` | cycle mode active → stealth → passive |
| `[c]` | channel plan: 1/6/11 ↔ all 1–13 |
| `[k]` | crack scope: all backlog ↔ this session only |
| `[s]` | show a stats snapshot |
| `[q]` | quit |

---

## Whitelist — never touch your own network

APs on the whitelist are **fully ignored** — never deauthed, captured, or cracked. Matching is by exact **BSSID** so you don't accidentally skip an authorised target that merely shares a common name.

```
CMD> sw                        # scan first (populates the index)
CMD> pwn wl add 3              # whitelist AP #3 from the scan (exact BSSID)
CMD> pwn wl add AA:BB:CC:11:22:33   # …or a raw MAC
CMD> pwn wl add ssid MyHome    # whitelist ALL APs named MyHome (broad — warns you)
CMD> pwn wl list               # show the whitelist
CMD> pwn wl rm 0               # remove row 0
CMD> pwn wl clear              # empty it
```

Whitelisting by **name** is a deliberate, warned opt-in because it skips *every* AP with that name.

---

## Smart cracking

When idle, the pet spends CPU (no radio — silent) working through its loot, per capture:

1. **Prior related passwords** — any password you've already cracked whose **SSID or BSSID** matches this AP. Catches mesh networks, reused passwords, and routers that were renamed/reset. Verified, never assumed.
2. **Common-default list** — a small built-in high-probability set.
3. **Your wordlist**, resumed from the saved cursor. Precedence: the **shared** `/apps/wordlists/*.txt` (drop your big list here once and every cracker — `pwn`/`ws`/`pm`/`karma` — uses it) → the tool's own `/apps/pwn/wordlist.txt` → the built-in ~100 list.

Every candidate is verified with the real handshake/PMKID check, so a recorded password is always genuine.

> **Tip:** the resume cursor keys on the wordlist's identity, so **changing or extending the wordlist automatically re-arms** caps that were previously exhausted — they get another pass against the new list.

---

## Files

Everything lives in `/apps/pwn/`:

| File | Contents |
|------|----------|
| `<BSSID>_<SSID>.cap` | one capture per network (Wireshark / hashcat compatible) |
| `captured.csv` | capture log — `time,bssid,ssid,ch,type,rssi,lat,lon` |
| `cracked.csv` | recovered passwords — `time,bssid,ssid,password` |
| `progress.csv` | per-capture resume cursor (so cracking never restarts) |
| `whitelist.csv` | never-touch APs |
| `wordlist.txt` | this tool's optional dictionary (fallback) |

The **shared** wordlist lives outside this folder at `/apps/wordlists/*.txt` and is preferred over the per-tool one — keep one big list there for all crackers.

On a T-Deck **Plus**, if the GPS task is already running (`gps`), captures are geotagged with `lat,lon`.

---

## Notes & honesty

- Not a GPU cracker — on-device cracking only breaks weak/common/wordlist passwords; the rest are saved for offline cracking.
- Stealth reduces your RF signature but does not make you invisible; only **passive** truly transmits nothing.
- `pwn` never advertises itself (no pwngrid beacon), so it is not detectable as a pwnagotchi by signature.
- Own / authorised networks only. See the [NOTICES](https://github.com/abdallahnatsheh/AL-ANQA-FIRMWARE/blob/main/NOTICES) file (#23) for prior-art credit (Pwnagotchi, Hash Monster, minigotchi — concept only, no code copied).
