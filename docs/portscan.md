---
title: Port Scan
lang: en
parent: Network Recon
nav_order: 4
---

# Port Scan

## `portscan` / `ps` — TCP Port Scanner

```
CMD> ps <ip|index> <start> <end>
CMD> ps 192.168.1.1 1 1024
CMD> ps 0 1 65535       # netdiscover index 0
CMD> ps ns2 1 1024      # netspy device 2
```

Scans a TCP port range using 4 parallel tasks with a 150ms timeout per port. Open ports are collected and displayed in a paginated table with service names.

**Target by index:** a bare number (or `nd<#>`) is the **netdiscover** ARP-cache index; **`ns<#>`** is the **netspy** discovered-device index (the `#` column in the `ns` table). So `ps top ns2` scans netspy device #2 without typing its IP.

## `ps top` — Top Common Ports

```
CMD> ps top <ip|index>
CMD> ps top 192.168.1.1
CMD> ps top 0
```

A sub-command of `portscan` (formerly the standalone `topscan` / `ts`). Scans the most common ports:

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
