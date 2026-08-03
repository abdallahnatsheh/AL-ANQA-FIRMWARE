---
title: Iso Scan
parent: Network Recon
nav_order: 3
---

# Iso Scan

<span class="label label-yellow">EXP</span>

## `isoscan` / `is` — Active Isolation Audit

The **active** counterpart to [Net Spy](netspy). Where `ns` is 100% passive, **`is` transmits** at a chosen victim to test whether a network's "client isolation" actually protects its clients. Based on the **AirSnitch** research (Vanhoef et al., NDSS 2026) — techniques only, no code used.

> **Own networks only.** This command puts frames on the air aimed at another device. Connect to the target network first (`cw`), and only run it against equipment you are authorised to test.

### Workflow

```
CMD> cw <ssid>       # join the target network
CMD> mc off ; cw     # pin a stable MAC
CMD> ns              # discover devices, note a victim's #
CMD> is ns3 auto     # probe device #3, get a recommendation
```

Victims are chosen from the `netspy` device list — either on the CLI (`is ns3 <attack>`) or via an in-app picker (`is` with no index). Every attack **confirms before firing** (echoes the target MAC/IP/name and requires `y`).

### Attacks

| Attack | What it does | Status |
|--------|--------------|--------|
| **`auto`** | Smart probe → detect L2/L3 → **recommend** the attack that fits this network. Start here. | works |
| **`inject`** | GTK-encrypt a broadcast ARP + spoof the AP MAC → reach the victim past isolation | **HW-proven** |
| **`bounce`** | ARP reachability — "is the victim up?" Works even against a Windows firewall that drops ping | works |
| **`portdown`** | Capture every frame involving the victim → `/apps/isoscan/NNN.pcap` (Wireshark) | works |
| `mitm` | ARP poison + capture at once; shows a **sustained-rate** verdict (MITM live vs leak-not-held); capture → `/apps/isoscan/mitm_NNN.pcap` | `[exp]` |
| `portup` | ARP-poison the victim's gateway toward us | `[exp]` |
| `dns` | ICMPv6 RA → point the victim's DNS at us; live query list on screen + dual-stack UDP-53 log → `/apps/isoscan/dns_NNN.csv`. **IPv6-only** | `[exp]` |
| `cctest` | AES-CCMP encrypt→decrypt self-test (no transmit) | works |

### `auto` — the recommended front door

`is ns<#> auto` runs a 6-step probe and prints a live verdict:

1. **CCMP self-test** — is the inject crypto healthy?
2. **GTK read** — do we have the group key to inject with?
3. **Normal ARP** — can the stack reach the victim without injecting? (fails = isolation on)
4. **GTK inject reach** — inject a broadcast ARP, watch our ARP cache for a reply
5. **Poison L2 test** — brief gateway poison; count **sustained** data redirect (L2 vs L3)
6. **ICMP ping** — a secondary datapoint (Windows usually drops it — not decisive)

It ends with a recommendation, e.g. *"poison seen, not holding → portdown"* or *"L2 MITM VIABLE → run 'mitm'"*.

### How injection works

`esp_wifi_80211_tx()` transmits a raw frame but does **not** encrypt it. So `is` software-CCMP-encrypts the MPDU itself (mbedTLS AES-CCM) with the **live GTK** — read from `wpa_supplicant`'s in-RAM state — and spoofs the AP's MAC as the sender. The AP forwards the "broadcast" to every client, and the victim's OS decrypts and processes the embedded unicast IP payload, bypassing isolation. This is the AirSnitch **GTK-abuse** primitive, and it is verified end-to-end on hardware.

### Keys

| Key | Action |
|-----|--------|
| `k` | Toggle GTK key id 1 / 2 |
| `q` | Step **back one level** — a running attack returns to the attack menu, the menu returns to the victim picker, the picker leaves `is`. Quitting a tool no longer drops you all the way out to the CLI. |

Each attack screen names the exact file it is writing (e.g. `file  /apps/isoscan/mitm_003.pcap`), and every capture/log picks the next free `NNN` slot, so a run never overwrites an earlier one. `portdown` → `NNN.pcap`, `mitm` → `mitm_NNN.pcap`, `dns` → `dns_NNN.csv`.

### Honest limitations

- **Real traffic MITM is not achievable on the single-radio T-Deck.** AirSnitch's actual interception uses **port stealing** — spoofing the victim's MAC on a *second BSSID* so the AP's switch redirects the victim's traffic to the attacker. That needs a second radio; the T-Deck has one. The `mitm`/`portup`/`dns` attacks therefore run and report *honestly* whether a poison holds (they measure a sustained data-redirect rate) — on most networks the answer is "not held."
- **ARP poisoning does not survive a modern gateway.** Windows largely ignores unsolicited ARP, and without traffic forwarding a victim just drops. This is a technique limit, not a bug — the tool tells you the truth instead of faking success.
- **`dns` (RA DNS poison) is IPv6-only.** Router Advertisements + RDNSS are an IPv6 mechanism — there is no IPv4 equivalent. It only works on a network that gives *clients* real IPv6 (a global `2xxx:` address + an IPv6 default route). On IPv4-only networks — which includes essentially every **phone hotspot** — the victim has only a `fe80::` link-local and IPv4 DNS, so there is nothing to poison and `DNS rcvd` stays 0 (not a bug). The on-device UDP-53 listener is dual-stack (raw lwip, IPv4+IPv6) so it catches the v6 queries a plain `WiFiUDP` (IPv4-only) would miss — verify with a home router that has IPv6/dual-stack enabled.
- **The reliable, useful capabilities** are: proving a network's client isolation is **bypassable** (`inject`), **reachability** testing (`bounce`), and **device capture / fingerprinting** (`portdown`). That's the tool's real job — an isolation *audit* and recon aid, not a traffic interceptor.

Files: `portdown` capture → `/apps/isoscan/NNN.pcap`, `mitm` capture → `/apps/isoscan/mitm_NNN.pcap`, DNS query log → `/apps/isoscan/dns_NNN.csv`.

**Credit:** [AirSnitch: Demystifying and Breaking Client Isolation in Wi-Fi Networks](https://www.ndss-symposium.org/wp-content/uploads/2026-f1282-paper.pdf) (NDSS 2026) and [vanhoefm/airsnitch](https://github.com/vanhoefm/airsnitch) — technique reference, no code used (see `NOTICES`).
