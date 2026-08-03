---
title: Ping
lang: en
parent: Network Recon
nav_order: 5
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

Pings the target **continuously until you press `q`**. The screen shows a fixed layout: a rolling window of the most recent results (sequence number + RTT, or `timeout`), plus a live stats line (sent / received / loss %) and the running min/avg/max RTT.

```
[NET::PING]  192.168.1.1
-> 192.168.1.1
--------------------------------
[+] seq 4    2 ms
[+] seq 5    1 ms
[-] seq 6    timeout
[+] seq 7    2 ms
--------------------------------
sent 7  recv 6  loss 14%
rtt 1/1/2 ms (min/avg/max)
q=stop
```

DNS hostnames are resolved before sending — `pg google.com` resolves the IP first, then pings it. Press `q` at any time to stop.

---

## Notes

- **Index shortcut** — after running `netdiscover` (`nd`), ping hosts by their index number: `pg 0` pings the first discovered host. After running `netspy` (`ns`), use the `ns` prefix: `pg ns3` pings netspy device #3 (the `#` column in the netspy table). No need to type the IP.
- **Packet loss > 0%** — could indicate wireless interference, the host firewall dropping ICMP, or the host being down.
- **Very high RTT** — normal for internet hosts (50–200ms); <5ms expected for local LAN targets.
- **All timeouts** — host is either down, blocking ICMP, or not reachable from this network.
