---
title: ARP Spoof
lang: en
parent: Network Recon
nav_order: 6
---

# ARP Cache Poisoning — `arpspoof` / `as`
{: .no_toc }

Bidirectional **Layer-2 ARP poisoning**: tell the victim "the gateway is at me" and tell the gateway "the victim is at me", so both machines' ARP caches point at the T-Deck. Their traffic is then redirected to you, where `as` logs where the victim is going.

**Own networks only.**

1. TOC
{:toc}

---

## Usage

You must be connected to the network first (`cw`). Then give a victim — an IP, a `netdiscover` index (`nd#`), or a `netspy` index (`ns#`); the gateway is auto-detected.

```
CMD> cw MyWiFi          # connect
CMD> nd                 # find the victim → note its IP or index
CMD> as 192.168.1.42    # poison that host
CMD> as nd1             # or by netdiscover index
CMD> as ns3             # or by netspy index
CMD> as 192.168.1.42 192.168.1.1   # explicit victim + gateway
```

Press **`q`** to stop. On exit it **heals both ARP caches** (re-broadcasts the real MACs ×5) and leaves WiFi idle — so the victim's connectivity is restored cleanly (an improvement over tools that just drop the poison).

---

## What you see — redirected traffic

While poisoning, the AP relays the victim's uplink to the T-Deck **decrypted**, and `as` parses it into a live **"victim reaching:"** list plus a CSV log:

| Extracted | From |
|-----------|------|
| Destination IP | every packet |
| **DNS domain** | UDP 53 query name |
| **HTTP host** | `Host:` header, port 80 |
| **HTTPS domain** | TLS **SNI** in the ClientHello |
| Port label | otherwise |

Logged to `/apps/arpspoof/NNN.csv` (`time_ms, dst_ip, detail`), sequential, never overwritten.

---

## How it works

The ARP replies are raw 42-byte ethernet+ARP frames built by byte offset and handed to lwip's `netif->linkoutput()` under a TCP/IP core lock — so the **WiFi driver encrypts them with the real PTK** and the AP accepts them on WPA2 (a plain injected frame would be dropped). Capture uses promiscuous mode on the AP channel; SD writes pause promiscuous (GDMA rule).

---

## Honest limit

A single radio has **no IP forwarding**, so this is a **redirect / blackhole (DoS)**: you *see* the victim's requests, but you can't relay them onward — so the victim's traffic breaks while the attack runs. Full transparent interception needs a second radio.

For credential capture on the redirected traffic, pair it with [Responder](responder).

---

## See also

- [Responder](responder) — capture NetNTLM hashes from the redirected/poisoned victim
- [Net Discover](netdiscover) / [Net Spy](netspy) — find victims (`nd#` / `ns#` targeting)
- [Iso Scan](isoscan) — the isolation-bypass counterpart for client-isolated networks
