---
title: Default-Password Check
lang: en
parent: Network Recon
nav_order: 10
---

# Default-Password Checker — `dpwo` / `dw`
{: .no_toc }

<span class="label label-yellow">EXP</span>

Audits a host you've already discovered for services still running **factory / default credentials** — the `admin:admin` class of finding that dominates real LAN audits (IP cameras, routers, printers, NAS, IoT). It is a **default-credential checker, not a brute-forcer**: a small curated per-service list, own-networks-only.

**Own networks only.**

1. TOC
{:toc}

---

## Usage

You must be on the network (`cw`) so the T-Deck can reach the target.

```
CMD> cw MyWiFi password        # join the LAN
CMD> nd                        # (optional) discover hosts → indices
CMD> dw 192.168.1.10           # audit an IP directly
CMD> dw nd3                    # …or a netdiscover index
CMD> dw ns2                    # …or a netspy index
```

Each service is shown on its own row and updates live as it's tested:

```
[DPWO::AUDIT]  192.168.1.10
21    FTP    admin:admin        ← green = default creds found
23    TELNET open, no default
80    HTTP   closed
554   RTSP   admin:(blank)      ← camera with a default login
6379  REDIS  NO AUTH            ← orange = open, no auth at all
161   SNMP   community:public
```

Findings are appended to **`/apps/dpwo/results.csv`** (`ip,port,service,user,pass`).

---

## What it checks

| Port | Service | Method |
|------|---------|--------|
| 21 | FTP | `USER`/`PASS` → `230` |
| 22 | SSH | `ssh_userauth_password` (reuses the [`sc`](ssh) LibSSH stack) — **slow**: a full key-exchange per credential, so it uses a short SSH-specific list |
| 23 | Telnet | login/password, shell-prompt heuristic |
| 80 / 81 / 8000 / 8080 | HTTP | Basic **and** Digest auth (`WWW-Authenticate`) |
| 554 | RTSP | `DESCRIBE` + Basic/Digest — **IP cameras**. Probes common brand stream paths (Hikvision `/Streaming/Channels/101`, Dahua `/cam/realmonitor`, Reolink `/h264Preview_01_main`, …) since many cameras 404 on `/` and only challenge auth on a valid path |
| 6379 | Redis | `PING` → open-no-auth, else `AUTH` |
| 161 (UDP) | SNMP | community strings (`public`, `private`, …) |

Everything is a short scripted exchange over a **raw socket** — no heavy libraries, so it's RAM-light. It's plain STA traffic, so there's no GDMA concern and results are written straight to SD.

---

## Custom credential list

Drop extra defaults on the SD card at **`/apps/dpwo/creds.csv`**, one `user,pass` per line (blank lines and `#` comments ignored):

```
# user,pass
admin,vendor123
installer,installer
```

They're appended to the built-in list for that run.

---

## Honest limits

- **Default-cred check, not a wordlist grinder** — a short curated list, so a run is seconds per open service.
- **No modern web-form logins** — HTTP is **Basic/Digest only**; panels that use an HTML form with CSRF tokens/JS won't be tested. (Most consumer routers/cameras still use Basic/Digest on the LAN.)
- **No HTTPS** — the TLS handshake's DRAM cost isn't worth it here; plain HTTP only.
- **Telnet success is a heuristic** (shell-prompt vs. re-prompt), so treat Telnet hits as "likely" and confirm.
- **Own networks only.**

---

## See also

- [Netdiscover](netdiscover) / [NetSpy](netspy) — find hosts to target (`nd#` / `ns#`)
- [Port Scan](portscan) — see which services are actually open first
