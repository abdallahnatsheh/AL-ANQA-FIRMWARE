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
| [Port Scan](portscan) | `portscan` / `ps` · `topscan` / `ts` · banner grabber · OS fingerprint |
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

**Index shortcut:** once `nd` runs, you can use the host index number instead of the IP address in `portscan`, `topscan`, and `ping`.

```
CMD> nd         # discovers: [0] 192.168.1.1  [1] 192.168.1.5 ...
CMD> ts 0       # top-scan the router without typing the IP
CMD> ps 1 1 1024
```

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

## `topscan` / `ts` — Top 31 Ports

```
CMD> ts <ip|index>
CMD> ts 192.168.1.1
CMD> ts 0
```

Scans the 31 most common ports (same list as nmap's default scan):

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

Available inside `portscan` / `topscan` results — press `b` while viewing an open port to grab its banner.

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

Shown automatically in `portscan` / `topscan` results alongside open ports.

| Method | How |
|--------|-----|
| TTL | Raw ICMP ping via lwip — TTL ≤ 64 → Linux/macOS, TTL ≤ 128 → Windows |
| Banner | SSH version string, HTTP `Server:` header |
| Port presence | RDP (3389) + SMB (445) + MSRPC (135) → Windows |
