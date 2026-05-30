---
title: Port Scan
parent: Network Recon
nav_order: 2
---

# Port Scan

## `portscan` / `ps` — TCP Port Scanner

```
CMD> ps <ip|index> <start> <end>
CMD> ps 192.168.1.1 1 1024
CMD> ps 0 1 65535
```

Scans a TCP port range using 4 parallel tasks with a 150ms timeout per port. Open ports are collected and displayed in a paginated table with service names.

## `topscan` / `ts` — Top 31 Ports

```
CMD> ts <ip|index>
CMD> ts 192.168.1.1
CMD> ts 0
```

Scans the 31 most common ports:

`21 22 23 25 53 80 110 111 135 139 143 161 389 443 445 587 993 995 1433 1521 1723 3306 3389 5432 5900 6379 8080 8443 8888 9200 27017`

### Keys

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `b` | Banner grab on selected port |
| `q` | Quit |

---

## Banner Grabber

Press `b` on an open port to grab its banner.

| Protocol | Detection | Probe sent |
|----------|-----------|-----------|
| HTTP | Port 80/8080/8443/8888 | `GET / HTTP/1.0` |
| TLS/HTTPS | TLS ClientHello bytes | TLS handshake init |
| MySQL | Port 3306 | Connect + read greeting |
| Redis | Port 6379 | `PING\r\n` |
| Other | Any | Newline probe |

---

## OS Fingerprinting

Shown automatically alongside open ports.

| Method | How |
|--------|-----|
| TTL | ≤ 64 → Linux/macOS · ≤ 128 → Windows |
| Banner | SSH version string, HTTP `Server:` header |
| Ports | RDP (3389) + SMB (445) + MSRPC (135) → Windows |
