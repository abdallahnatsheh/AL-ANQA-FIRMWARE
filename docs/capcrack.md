---
title: Cap Cracker
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 8
---

# Cap Cracker

## `crack` / `cc` — Offline WPA/WPA2 `.cap` Cracker

Dictionary-attacks a captured handshake **offline on the device**. Reads a libpcap
`.cap`/`.pcap` file, extracts either a **4-way handshake** (M1 ANonce + M2 SNonce/MIC)
or a **PMKID**, and tries passwords from one or more wordlists. Works on captures from
[`karma`](karma.md), [`wpasniff`](wpasniff.md) (`ws`), [`pmkid`](pmkid.md) (`pm`), or
any external tool (aircrack-ng, hcxdumptool, …).

```
CMD> cc                       ← pick a .cap in the current dir, then a wordlist
CMD> cc MyNet.cap             ← crack this cap, pick a wordlist
CMD> cc MyNet.cap rockyou.txt ← cap + explicit wordlist
CMD> cc MyNet.cap lists/      ← run EVERY *.txt in lists/
CMD> cc /apps/karma           ← a directory → pick a .cap inside it
```

---

### Paths are relative to your `cd` directory

Like `cat`/`ls`, `cc` resolves paths against the current SD working directory, so you can
`cd` into the folder once and then pass bare filenames:

```
CMD> cd /apps/karma
CMD> cc MyNet.cap wordlist.txt
```

Tab-style autocomplete (`'` key) completes **both files and directories** for either
argument.

| Argument | Accepts |
|----------|---------|
| cap | a `.cap`/`.pcap` file · a **directory** (lists caps inside to pick) · **omitted** (picks from the current dir) |
| wordlist | a `.txt` file · a **directory** (runs every `*.txt`) · **omitted** (picker: built-in / all `*.txt` here / one file) |

---

### Wordlists

- Pass a **single file**, or a **directory** to run every `*.txt` in it in sequence.
- With no wordlist argument you get a picker: **Built-in (100)**, **ALL `*.txt` in this dir**,
  or any individual `.txt` found in the current directory.
- The **built-in 100-password list always runs last** as a fallback, so a quick `cc cap`
  always tries something even with no SD wordlist.
- Passwords shorter than 8 or longer than 63 characters are skipped (WPA limits).

Press **`q`** any time to abort; the screen shows live tries, rate, current wordlist, and
candidate.

---

### What it needs in the capture

To derive the PMK the cracker needs the network **ESSID**, which it reads from a **beacon
or probe-response** frame in the `.cap`. Captures made by `karma` already include a beacon,
so they work directly. If the cap has no beacon, `cc` reports `noESSID` and which handshake
pieces were found (`noM1` / `noM2`).

| Capture type | Needs |
|--------------|-------|
| 4-way handshake | beacon (ESSID) + M1 + M2 |
| PMKID | beacon (ESSID) + M1 with a PMKID KDE |

---

### Output

Cracked passwords are appended to:

```
/apps/capcrack/cracked.csv      ← ssid,password,HS|PMKID
```

---

### Crack it on a PC instead

For big wordlists, copy the `.cap` to a computer — it is standard libpcap (linktype 105):

```
aircrack-ng -w rockyou.txt MyNet.cap
# or
hcxpcapngtool -o hash.22000 MyNet.cap && hashcat -m 22000 hash.22000 rockyou.txt
```

---

### Notes

- Reads the SD card only — no WiFi, no transmission. Safe to run any time.
- Classic pcap only (`.cap`/`.pcap`), not pcapng.
- The on-device crack is single-core PBKDF2 — fine for a targeted list, slow for huge ones;
  use the PC path for `rockyou`-scale lists.

See also: [Karma](karma.md) · [WPA Handshake](wpasniff.md) · [PMKID](pmkid.md)
