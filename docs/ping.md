---
title: Ping
parent: Network Recon
nav_order: 3
---

# Ping

## `ping` / `pg` — ICMP Echo

```
CMD> pg <ip|hostname>
CMD> pg 192.168.1.1
CMD> pg google.com
CMD> pg 0              # ping by netdiscover index after running nd
```

Requires an active WiFi connection.

---

## How It Works

Sends **4 ICMP echo request** packets to the target and waits for replies. Each reply line shows the sequence number, bytes received, and round-trip time. A final summary shows min/avg/max RTT and packet loss percentage.

```
PING 192.168.1.1
[1] 64 bytes  rtt=2ms
[2] 64 bytes  rtt=1ms
[3] timeout
[4] 64 bytes  rtt=2ms

--- 192.168.1.1 ping ---
3/4 received  loss=25%
rtt min/avg/max = 1/1/2 ms
```

DNS hostnames are resolved before sending — `pg google.com` resolves the IP first, then pings it.

---

## Notes

- **Index shortcut** — after running `netdiscover` (`nd`), you can ping hosts by their index number: `pg 0` pings the first discovered host. No need to type the IP.
- **Packet loss > 0%** — could indicate wireless interference, the host firewall dropping ICMP, or the host being down.
- **Very high RTT** — normal for internet hosts (50–200ms); <5ms expected for local LAN targets.
- **All timeouts** — host is either down, blocking ICMP, or not reachable from this network.
