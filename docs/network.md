---
title: Network Recon
nav_order: 7
has_children: true
---

# Network Recon

> All network tools require an active WiFi connection. Run `connectwifi` first.

| Guide | Commands |
|-------|---------|
| [Net Discover](netdiscover) | `netdiscover` / `nd` — ARP scan local /24 |
| [Net Spy](netspy) | `netspy` / `ns` — **[EXP]** passive client-isolation device recon (AirSnitch) |
| [Iso Scan](isoscan) | `isoscan` / `is` — **[EXP]** active isolation audit: GTK inject + capture (AirSnitch) |
| [Port Scan](portscan) | `portscan` / `ps` · `ps top` · banner grabber · OS fingerprint |
| [Ping](ping) | `ping` / `pg` — ICMP ping |

---

## `netdiscover` / `nd` — ARP Host Discovery

```
CMD> nd
```

Sends ARP requests across the entire local /24 subnet and displays a table of live hosts with their IP address and MAC address. Results are cached — use `show hosts` to view them again, or `u` to rescan.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-scan |
| `q` | Quit |

**Index shortcut:** once `nd` runs, you can use the host index number instead of the IP address in `portscan` (incl. `ps top`) and `ping`.

```
CMD> nd         # discovers: [0] 192.168.1.1  [1] 192.168.1.5 ...
CMD> ps top 0   # top-scan the router without typing the IP
CMD> ps 1 1 1024
```

---

## `netspy` / `ns` — Client-Isolation Device Recon  `[EXP]`

Finds devices that `nd` **can't** see when the network has **client isolation** (the AP blocks client-to-client unicast, so ARP scans only reveal the gateway). Connect first with `cw`. **Own networks only.**

```
CMD> ns
```

`ns` is **100% passive — it never transmits.** Client isolation doesn't filter broadcast/multicast, and an associated ESP32 decrypts those group frames in hardware, so `ns` just listens and parses them. It reconstructs each device's IP, MAC, vendor, hostname and services from **ARP, IPv4, DHCP, mDNS and SSDP**.

| Key | Action |
|-----|--------|
| trackball `↑`/`↓` | Select a row |
| **Enter** (or `i`) | Device detail (name, vendor, seen-via, services) |
| **`p`** / **`o`** | Ping / port-scan the selected device in place |
| `s` / `c` | Save to SD / clear |
| `l` / `a` | Page |
| `q` | Quit |

The **HOW** column shows the discovery source: `A`=ARP `I`=IPv4 `D`=DHCP `M`=mDNS `S`=SSDP. A `+` marks rows with detected services. Saves to `/apps/netspy/NNN.csv`. Subcommands: `ns gtk` (show group key), `ns dump` (state → SD). See the [full Net Spy guide](netspy).

---

## `isoscan` / `is` — Active Isolation Audit  `[EXP]`

The offensive counterpart to `netspy`: where `ns` only listens, **`is` transmits** at a chosen victim to test whether the network's client isolation actually holds. Pick a victim from the `ns` list, then run an attack (it confirms before firing). **Own networks only.**

```
CMD> ns              # discover devices, note a victim #
CMD> is ns3 auto     # probe device #3 and get a recommendation
```

Start with **`auto`** — it runs a 6-step probe (CCMP, GTK, ARP, GTK-inject reach, poison L2 test, ICMP) and prints a verdict recommending the attack that fits this network. Attacks: `inject` (GTK broadcast ARP → reach a victim past isolation, **HW-proven**), `bounce` (ARP reachability — works even against a Windows firewall that drops ping), `portdown` (capture the victim's frames → `/apps/isoscan/NNN.pcap`), and the `[exp]` poison attacks `mitm`/`portup`/`dns`.

> **Honest limit:** real traffic **MITM is not achievable on the single-radio T-Deck** — AirSnitch's actual interception (port stealing) needs a second BSSID / second radio. The `[exp]` attacks run and report *truthfully* whether a poison holds (a sustained data-redirect rate), which on most networks is "not held." The reliable, useful capabilities are **isolation-bypass demonstration (inject), reachability (bounce), and device capture (portdown)**. See the [full Iso Scan guide](isoscan).

---

## `portscan` / `ps` — TCP Port Scan

```
CMD> ps <ip|index> <start_port> <end_port>
CMD> ps 192.168.1.1 1 1024
CMD> ps 0 1 65535
```

Scans a TCP port range using 4 parallel tasks with a 150ms timeout per port. Open ports are collected and displayed in a paginated table with service names.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `b` | Banner grab on selected port |
| `q` | Quit |

---

## `ps top` — Top Common Ports

```
CMD> ps top <ip|index>
CMD> ps top 192.168.1.1
CMD> ps top 0
```

A sub-command of `portscan` (formerly the standalone `topscan` / `ts`). Scans the most common ports (same list as nmap's default scan):

`21 22 23 25 53 80 110 111 135 139 143 161 389 443 445 587 993 995 1433 1521 1723 3306 3389 5432 5900 6379 8080 8443 8888 9200 27017`

Faster than a full port scan and catches most real-world services. Same paginated result table as `portscan`.

---

## `ping` / `pg` — ICMP Ping

```
CMD> pg <ip|hostname>
CMD> pg 192.168.1.1
CMD> pg google.com
```

Sends 4 ICMP echo requests and displays RTT for each reply plus a summary with min/avg/max RTT and packet loss percentage.

---

## Banner Grabber

Available inside `portscan` results (both `ps` and `ps top`) — press `b` while viewing an open port to grab its banner.

T-Rex sends a protocol-aware probe and reads the response:

| Protocol | Detection | Probe sent |
|----------|-----------|-----------|
| HTTP | Port 80/8080/8443/8888 | `GET / HTTP/1.0` |
| TLS/HTTPS | TLS ClientHello bytes | TLS handshake init |
| MySQL | Port 3306 | Connect + read greeting |
| Redis | Port 6379 | `PING\r\n` |
| Other | Any | Newline probe |

Displays the raw banner and, for HTTP, extracts the `Server:` header. An animated spinner shows while waiting for the response.

---

## OS Fingerprinting

Shown automatically in `portscan` results (both `ps` and `ps top`) alongside open ports.

| Method | How |
|--------|-----|
| TTL | Raw ICMP ping via lwip — TTL ≤ 64 → Linux/macOS, TTL ≤ 128 → Windows |
| Banner | SSH version string, HTTP `Server:` header |
| Port presence | RDP (3389) + SMB (445) + MSRPC (135) → Windows |
