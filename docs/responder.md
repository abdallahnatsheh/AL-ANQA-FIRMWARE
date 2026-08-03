---
title: Responder
parent: Network Recon
nav_order: 7
---

# Name Poisoner + NTLM Capture — `responder` / `rsp`
{: .no_toc }
{: .label .label-yellow }[EXP]

Answers Windows name-resolution broadcasts with the T-Deck's IP, then presents fake **HTTP** and **SMB** login prompts to harvest **NetNTLM** hashes for offline cracking. Modelled on the classic [Responder](https://github.com/lgandx/Responder) tool.

**Own networks only.**

1. TOC
{:toc}

---

## Usage

Connect first (`cw`), then:

```
CMD> cw MyWiFi        # connect
CMD> rsp              # ACTIVE — poison name queries + run capture servers
CMD> rsp passive      # PASSIVE — log queries only, transmit nothing
```

| Mode | Behaviour |
|------|-----------|
| **`rsp`** (active) | Answers every LLMNR/NBT-NS/mDNS query — including `wpad` — with our IP, and runs the HTTP(:80) + SMB(:445) catchers |
| **`rsp passive`** | Binds the listeners and joins the multicast groups (normal host behaviour), logs every observed name query, but **never replies and never opens the capture servers** — pure, silent recon |

The screen shows live `LL / NB / MD` query counters, a `HASH` counter, the most recent queried names, and the latest capture. Press `q` to quit.

---

## What it poisons & captures

| Protocol | Port | Role |
|----------|------|------|
| LLMNR | 5355 | name-query poison |
| NBT-NS | 137 | name-query poison |
| mDNS | 5353 | name-query poison |
| HTTP | 80 | NTLM / Basic auth catcher (+ serves a PAC to `wpad` requests) |
| SMB | 445 | NTLM catcher (SMB2 / SPNEGO) |

Captured hashes use a fixed 8-byte challenge (`1122334455667788`) so they're crackable offline. **NTLMv2** → hashcat `-m 5600`; **NTLMv1** → `-m 5500`.

---

## Output

A per-session folder `/apps/responder/NNN/`:

| File | Contents |
|------|----------|
| `hashes.txt` | hashcat-ready NetNTLM (v2/v1) |
| `creds.txt` | HTTP **Basic** cleartext `user:pass` |
| `captures.csv` | `ms, proto, src, user` |
| `queries.csv` | every observed name query `ms, proto, src, name` |

Crack it on a PC:

```
hashcat -m 5600 hashes.txt rockyou.txt
```

---

## Honest limits

- Hashes are for **offline cracking** — nothing is cracked on-device.
- The **SMB2 path is best-effort/[EXP]** — the SPNEGO framing is hand-built and may need iteration against a real Windows target. **LLMNR/NBT-NS + HTTP capture are the reliable paths.**
- The victim must actually *attempt* a name lookup / NTLM auth (a well-patched host with WPAD disabled may only leak the query name in passive mode).
- **Not yet HW-tested** against a live domain.

---

## See also

- [ARP Spoof](arpspoof) — redirect a victim's traffic toward you first, then capture here
- [Net Spy](netspy) — passively map the devices on the network
