---
title: Net Discover
parent: Network Recon
nav_order: 1
---

# Net Discover

## `netdiscover` / `nd` — ARP Host Discovery

Sends ARP requests across the full local /24 subnet and displays a table of live hosts with IP and MAC address. Requires an active WiFi connection.

```
CMD> nd
```

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-scan |
| `q` | Quit |

Results are cached — use `show hosts` to view them again without rescanning.

### Index shortcut

Once `nd` runs, use the host index number instead of the IP address in `portscan`, `topscan`, and `ping`:

```
CMD> nd          # [0] 192.168.1.1  [1] 192.168.1.5
CMD> ts 0        # top-scan the router
CMD> ps 1 1 1024
```
