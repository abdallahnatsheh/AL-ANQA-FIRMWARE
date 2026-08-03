---
title: WPA3 Downgrade
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 8
---

# WPA3 Transition Downgrade — `wpa3down` / `w3d`
{: .no_toc }
{: .label .label-yellow }[EXP]

Forces a **WPA3-capable** client off its network and back onto a **WPA2** clone of the same SSID, so you can capture a crackable 4-way handshake. Exploits the *transition mode* many routers run (WPA2 **and** WPA3 on one SSID) — the Dragonblood class of attacks (CVE-2019-9494…9499).

**Own networks only.**

1. TOC
{:toc}

---

## How it works

Transition-mode APs advertise both WPA2 and WPA3 so old and new devices can join. `w3d`:

1. Stands up a **WPA2-PSK-only rogue AP** with the target's SSID and channel (reuses the `karma` rogue-handshake engine — it injects its own M1 with a known ANonce and sniffs the victim's M2).
2. **Deauthenticates** the victim from the real AP (broadcast deauth on the same channel).
3. The WPA3-capable victim, seeing only WPA2 now, **reconnects over WPA2** → you capture the handshake.
4. Crack it offline with [`cc`](capcrack) or hashcat.

---

## Usage

Requires a prior scan so the tool knows the target:

```
CMD> sw                 # scan — WPA3/transition APs are flagged
CMD> w3d                # opens a picker of downgrade-able (transition-mode) APs
CMD> w3d 3              # or target scan-result index 3 directly
```

Live counters show `Prb / Ath / Asc / M2` as the victim reconnects. On `M2` the capture is saved and the AP is torn down.

**Output:** `/apps/wpa3down/<ssid>.cap` (beacon + M1 + M2, libpcap linktype 105; never overwrites — `<ssid>-1.cap`, `<ssid>-2.cap`…). Crack it:

```
CMD> cc /apps/wpa3down/HomeNet.cap /apps/wpasniff/wordlist.txt
```

---

## Requirements & honest limits

| Condition | Result |
|-----------|--------|
| Transition mode, PMF **off/optional** | ✅ works — victim can be deauthed and downgraded |
| PMF (802.11w) **required** | ❌ the deauth is rejected — victim won't drop. UI warns "PMF? may not drop" |
| **Pure WPA3** (SAE-only, no WPA2) | ❌ nothing to downgrade to |

This is the **core downgrade only** (Phase 2). The PMF-bypass pre-association flood (Phase 3) is **not built**. Output is a `.cap` for offline cracking, not an on-device crack. **Not yet HW-tested.**

---

## See also

- [WPA Sniff](wpasniff) — capture a handshake the normal way (deauth + EAPOL)
- [Cap Cracker](capcrack) — crack the resulting `.cap`
- [Karma](karma) — the rogue-AP engine `w3d` is built on
