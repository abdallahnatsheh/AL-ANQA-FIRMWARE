---
title: Network Recon
lang: en
nav_order: 7
has_children: true
---

# Network Recon

Tools for mapping and attacking the LAN you're connected to. **All of them require an active WiFi connection** — run [`connectwifi`](wifi-scan) first — and are for **your own / authorized networks only.**

---

## Discovery

| Tool | Command | What it does |
|------|---------|--------------|
| [Net Discover](netdiscover) | `nd` | ARP-scan the whole local /24 → table of live hosts (IP + MAC) |
| [Net Spy](netspy) | `ns` · **[EXP]** | **Passive** device recon on client-isolated networks (parses group ARP/DHCP/mDNS/SSDP — never transmits) |

## Scanning a host

| Tool | Command | What it does |
|------|---------|--------------|
| [Port Scan](portscan) | `ps` · `ps top` | TCP port scan (range or top-common), **banner grab** (`b`), and **OS fingerprint** |
| [Ping](ping) | `pg` | ICMP echo × 4 with RTT + loss summary |
| [SSH Client](ssh) | `ssh` · `sc` | Interactive colour SSH terminal with scrollback |
| [Default-Password Check](dpwo) | `dw` · **[EXP]** | Try factory/default creds on FTP/SSH/Telnet/HTTP/RTSP/Redis/MQTT/SNMP (or a custom `svc:port`) → `results.csv` |

> **Targeting shortcut:** after `nd` or `ns`, use the host **index** instead of typing an IP — e.g. `ps top 0`, `pg 1`, `as ns3`.

## Attacks

| Tool | Command | What it does |
|------|---------|--------------|
| [Iso Scan](isoscan) | `is` · **[EXP]** | **Active** isolation audit — GTK-inject to reach "isolated" clients, capture frames, reachability probes |
| [ARP Spoof](arpspoof) | `as` | L2 ARP cache poisoning → redirect + log the victim's DNS/HTTP/HTTPS destinations |
| [Responder](responder) | `rsp` · **[EXP]** | Poison LLMNR/NBT-NS/mDNS + fake HTTP/SMB login → capture NetNTLM hashes |

---

## Typical flow

```
CMD> cw MyWiFi     # 1. connect
CMD> nd            # 2. discover hosts        (or: ns  on isolated networks)
CMD> ps top 0      # 3. scan a host of interest
CMD> as ns3        # 4. (authorized) redirect a victim, or rsp to capture creds
```

Each tool has its own page (left) with full options, output files, and honest limits.
