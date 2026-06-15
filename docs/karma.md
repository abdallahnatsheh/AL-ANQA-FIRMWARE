---
title: Karma
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 7
---

# Karma

## `karma` / `km` — Anti-Client Rogue-AP Suite

Targets **clients**, not access points. Phones and laptops constantly broadcast probe
requests for the networks in their saved list (PNL). Karma harvests those, fingerprints
the physical devices behind them, and lets you bait a network a client wants — either a
**WPA2 half-handshake** (crackable offline) or an **open captive portal** (credential
capture).

```
CMD> km                 ← harvest + live table (interactive)
CMD> km hs <ssid> [ch]  ← WPA2 half-handshake bait for one SSID
CMD> km portal <ssid>   ← open AP + captive portal for one SSID
```

Works headless — SD and GPS are enrichment only, never required.

---

### Harvest + fingerprint

Promiscuous probe-request sniff, channel-hopping 1→13. Two views (`[v]` toggles):

- **HARV** — SSID table: who-wants-what, sorted by number of distinct devices.
- **DEVS** — physical devices, clustered from randomized MACs by their probed-SSID set
  (**PNL fingerprinting** defeats MAC randomization when a device leaks a multi-SSID PNL).
  Shows vendor/type (OUI), PNL, and how many MACs collapsed into one device.

| Key | Action |
|-----|--------|
| `[v]` | toggle HARV (nets) ↔ DEVS (devices) |
| trackpad | select a row |
| `[a]` / `[l]` | page |
| `[h]` | WPA2 half-handshake bait on the selected target |
| `[p]` | open AP + captive portal on the selected target |
| `[s]` | **save harvest + devices** → `/apps/karma/NNN.csv` |
| `[c]` | clear tables |
| `[q]` | stop |

`[s]` writes a sequential `NNN.csv` (never overwrites) with two sections — `[NETS]`
(ssid, devices, hits, rssi, channel) and `[DEVICES]` (id, vendor, type, macs, randomized,
pnl_count, rssi, pnl).

---

### WPA2 half-handshake bait — `[h]` / `km hs`

T-REX stands up a **manual rogue AP** that clones the target SSID as WPA2. Because T-REX
**is** the AP, it generates its own ANonce and injects its own **M1** — so it never needs
to capture M1 over the air (an ESP32 can't hear its own transmissions). A client that has
the real network saved associates and replies with **M2**, whose MIC is keyed by the
**real** password. With the known ANonce + the sniffed M2 you have a crackable
**half-handshake** — no deauth, no real AP, WPA3/SAE immune.

The live screen shows the attack stages so you can see how far each client gets:

```
Prb  ← directed probe requests for our SSID
Ath  ← open-auth requests
Asc  ← association requests
M1   ← M1 frames we injected
M2!  ← the client's reply captured  ← success
```

On `M2!`:

- A crackable capture is written to **`/apps/karma/<ssid>.cap`** (beacon + M1 + M2,
  libpcap linktype 105) — open it in Wireshark or crack on a PC.
- Press **`[c]`** to crack on-device: choose **`[1]` SD wordlist** (`/apps/karma/wordlist.txt`)
  or **`[2]` built-in (100)**. A hit is shown and appended to `/apps/karma/cracked.csv`. A
  miss still leaves the `.cap` on the card and tells you where it is.

> The bait relies on the client associating to an AP that can't ACK at the MAC layer.
> T-REX sets its interface MAC to the rogue BSSID to coax hardware ACKs; success varies by
> client. The stage counters tell you exactly where a given client stalls.

To crack the saved `.cap` later (bigger wordlists, a whole directory of lists), use
[`crack` / `cc`](capcrack.md).

---

### Open captive portal — `[p]` / `km portal`

Brings up an **open** AP cloning the SSID + a captive portal for credential capture. The
template picker (`[p]`) lists the shared built-ins (Generic / Google / Router) **plus** any
`.html` in `/apps/karma/portal/` **and** `/apps/eviltwin/portal/` — so portals you already
made for [Evil Twin](eviltwin.md) work here too. Captured credentials are written to
`/apps/karma/creds.csv` on exit.

---

### Files — `/apps/karma/`

| File | Contents |
|------|----------|
| `<ssid>.cap` | half-handshake capture (beacon + M1 + M2) |
| `cracked.csv` | on-device crack results (`ssid,password`) |
| `creds.csv` | captive-portal credentials (`ssid,user,pass`) |
| `wordlist.txt` | optional SD wordlist for `[c]` |
| `NNN.csv` | saved harvest + device tables (`[s]`) |
| `portal/*.html` | custom portal templates |

See also: [Cap Cracker](capcrack.md) · [Evil Twin](eviltwin.md) · [WPA Handshake](wpasniff.md)
