---
title: Cap Cracker
lang: en
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
  **Type a path…**, or any individual `.txt` found in the current directory.
- **Type a path…** lets you point at a wordlist **file or directory anywhere on the card** (not just
  the current dir) — press **`'`** while typing to autocomplete file and directory names, exactly
  like the command line. A directory runs every `*.txt` inside it; a file runs just that file.
- The **built-in 100-password list always runs last** as a fallback, so a quick `cc cap`
  always tries something even with no SD wordlist.
- Passwords shorter than 8 or longer than 63 characters are skipped (WPA limits).

Press **`q`** any time to abort; the screen shows live tries, rate, current wordlist, and
candidate.

---

### Resume a long crack

On-device cracking is slow (each candidate is a full PBKDF2 derivation), so a big wordlist
can take a while. You don't have to babysit it: press **`q`** to stop, use the device, and
**relaunch the same `.cap` with the same wordlist later** — `cc` picks up at the exact byte
offset where it left off instead of restarting from word 0. It resumes even across a reboot.

- The capture is identified by the **BSSID + SSID read from inside the `.cap`** (not its
  filename), so two different networks that happen to share a filename resume independently,
  and two captures of the *same* network share progress.
- The wordlist is identified by its **full path + size**, so **different lists never share a
  cursor** (even at the same size), the same list resumes whichever way you selected it, and
  **editing a list re-arms it** (its size changes). Saved to `/apps/capcrack/progress.csv`.
- A wordlist that was **fully exhausted** is skipped instantly on the next run (nothing left
  to try), and a **successful crack clears** its cursor.
- The small built-in 100-password list isn't tracked — it finishes in seconds.

---

### Background crack — `cc bg` (crack under the cover)

For long jobs you don't want to babysit — or that you want to run **in public behind the
undercover disguise** — start it in the background:

```
CMD> cc bg capture.cap big.txt   # start straight away
CMD> cc bg                       # WATCH the running crack live (or start one if none is running)
CMD> cc                          # same — bare cc watches the running bg crack
CMD> cc bg status                # live view if running, else the last outcome
CMD> cc bg stop                  # halt (the resume cursor is saved)
```

- **Watch it live:** once a bg crack is running, `cc` or `cc bg` opens a real-time monitor (list %,
  tried, rate, current guess). **`[q]` leaves without stopping** (it keeps grinding), and **`[s]` stops
  it** right there (cursor saved). (To start a *new* foreground crack while one runs in the bg, pass a
  cap: `cc <cap>`.) `cc bg stop` still works from the command line too.
- It returns you to the CLI and shows a small **`CC`** tag in the status bar while it runs.
- It **keeps cracking while you use the CLI** *and* **while the undercover cover is active** — so
  a device that looks like a Notes app is quietly grinding your wordlist the whole time.
- It's **cooperative and single-threaded** (time-sliced off the main loop), so it never fights the
  crypto hardware — rock-solid, at the cost of sharing CPU with whatever's on screen. It throttles by
  **screen state**: while you're **looking at the undercover disguise (screen on) it pauses**, so touch
  and the UI stay perfectly responsive; on the CLI (screen on) it cracks *gently*; and only when the
  screen is *off* (pocket / put down) does it crack *hard* — which is the real "grind in public" case.
- A hit is **silent while under the cover** (no beep to give you away) and is written to `cracked.csv`
  regardless — you find it when you drop the disguise.
- It **yields the SD card automatically** whenever WiFi is doing heavy DMA (a scan, monitor, evil-twin),
  so it's safe to leave running alongside other tools.
- Uses the same **resume cursor**, so stopping, rebooting, or yielding to WiFi never loses progress.

> On-device cracking is slow either way — `cc bg` is for leaving a weak/common/wordlist-present
> password grinding over time, not for brute-forcing strong keys.

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
/apps/capcrack/progress.csv     ← resume cursor: bssid,ssid,wordlist_id,offset
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
