---
title: Net Spy
parent: Network Recon
nav_order: 2
---

# Net Spy

## `netspy` / `ns` — Client-Isolation Device Recon  `[EXP]`

Discovers devices on a WiFi network that has **client isolation** enabled — where a normal ARP scan (`netdiscover` / `nd`) sees only the gateway because the access point blocks client-to-client unicast traffic.

> **Connect first.** `ns` works only while associated to the target network — run `connectwifi` / `cw` first. **Use on your own networks only.**

```
CMD> cw MyNetwork
CMD> ns
```

### How it works — 100% passive

`ns` **never transmits a single frame.** Client isolation only blocks client↔client *unicast*; broadcast and multicast frames (ARP, DHCP, mDNS, SSDP) are still relayed by the AP to **all** associated clients, encrypted with the group key. Because the T-Deck is associated, its WiFi hardware **already decrypts those group frames** — in promiscuous mode they arrive in the clear. So `ns` simply listens to that group traffic and parses it. To the network it looks like an ordinary idle client.

From this passive stream it reconstructs each device's **IP, MAC, vendor, hostname, and services**:

| Source | Gives |
|--------|-------|
| **ARP** | MAC + IP |
| **IPv4** | MAC + IP |
| **DHCP** (opt 12) | hostname |
| **mDNS** (`.local`) | hostname + service types |
| **SSDP / UPnP** | product/model (Roku, Sonos, TVs, printers) |

The **HOW** column flags which sources saw each device: `A`=ARP `I`=IPv4 `D`=DHCP `M`=mDNS `S`=SSDP.

> **Timing note:** IP/MAC fill in within seconds (constant broadcast traffic). **Hostnames depend on the device talking** — DHCP only fires when a device joins or renews its lease (can be hours), while mDNS/SSDP devices (Apple/Google/IoT/media) announce constantly, so they name themselves quickly. Reconnect a phone's WiFi to force an immediate DHCP hostname.

### Keys

| Key | Action |
|-----|--------|
| trackball `↑` / `↓` | Select a device row |
| **Enter** (or `i`) | Open the device **detail** (full MAC, IP, name, vendor, seen-via, services) — any key returns |
| `s` | Save the table to SD |
| `c` | Clear the table |
| `l` / `a` | Next / previous page |
| `q` | Quit |

A `+` next to a row means services were detected — press **Enter** to see them (AirPlay, Cast, HomeKit, Printer, SSH, SMB, DLNA, …).

### Subcommands

```
CMD> ns gtk      # show the live group key (read from RAM; does not transmit)
CMD> ns dump     # dump wpa_supplicant state to /apps/netspy/gwpasm.txt
```

These read the device's own `wpa_supplicant` memory and are groundwork for a future **active** module — `ns` itself stays passive.

### Files

```
/apps/netspy/NNN.csv     # full recon table — time,mac,ip,name,vendor,type,how,services
/apps/netspy/gwpasm.txt  # ns dump output
```

### Credit

Technique reimplemented from **AirSnitch** (Mathy Vanhoef et al., NDSS 2026) — published research, no code used. See `NOTICES`.
