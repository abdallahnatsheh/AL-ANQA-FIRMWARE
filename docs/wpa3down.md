---
title: WPA3 Downgrade
lang: en
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 8
---

# WPA3 Transition Downgrade — `wpa3down` / `w3d`
{: .no_toc }

<span class="label label-yellow">EXP</span>

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
CMD> w3d auto 3         # auto-pick the busiest client (hands-free)
CMD> w3d 3 AA:BB:CC:DD:EE:FF   # or name the victim MAC explicitly
```

### Picking the victim — no MAC typing

After you choose the AP, `w3d` **sniffs the AP's channel for ~5 s** and lists its **active clients** (station MACs seen in data frames to/from that BSSID), with vendor and signal, busiest first. Pick one with the trackball:

- **trackball / Enter** — attack the selected client
- **`[a]`** — auto-pick the busiest client
- **`[u]`** — re-sniff
- **`[b]`** — skip and just broadcast-deauth (no directed target, no PMF probe)
- **`[q]`** — cancel

`w3d auto <idx>` does the sniff and takes the busiest client automatically. Giving a MAC (`w3d 3 AA:BB:…`) skips discovery entirely.

Live counters show `Prb / Ath / Asc / M2` as the victim reconnects. On `M2` the capture is saved and the AP is torn down.

---

## PMF probe (Phase 3)

Whether the attack can drop a client depends on **PMF (802.11w)** — and the AP's beacon flags lie (a modern client negotiates PMF even when the AP only marks it "capable"). So `w3d` tests the **client empirically**:

```
CMD> w3d probe 3 AA:BB:CC:DD:EE:FF   # PMF recon only — no attack
```

It sniffs the victim on the AP's channel, hits it with a short **directed deauth burst**, and watches the reaction:

| Observation | Verdict |
|-------------|---------|
| Victim sends fresh **auth / (re)assoc** requests | **PMF OFF** — the deauth dropped it → downgrade should work |
| Victim's **data keeps flowing** through the burst | **PMF ON** — deauth ignored → pre-assoc flood only |
| Victim idle / silent, no re-auth | **Inconclusive** |

When you launch the attack **with** a victim MAC, `w3d` runs this probe first, shows the verdict, and picks the flood mode (press any key to start, `q` to cancel). The live attack uses a **pre-association-aware flood**: it hammers the real AP but yields the air the moment the victim starts joining the rogue, so the deauth loop doesn't starve the rogue's own handshake responses.

**Output:** `/apps/wpa3down/<ssid>.cap` (beacon + M1 + M2, libpcap linktype 105; never overwrites — `<ssid>-1.cap`, `<ssid>-2.cap`…). Crack it:

```
CMD> cc /apps/wpa3down/HomeNet.cap /apps/wpasniff/wordlist.txt
```

---

## Requirements & honest limits

| Condition | Result |
|-----------|--------|
| Transition mode, PMF **off/optional** | ✅ works — victim can be deauthed and downgraded |
| PMF (802.11w) **required** | ⚠️ an **established** link won't drop — the pre-assoc flood only disrupts the victim's *next* (re)association attempt (auth/assoc frames aren't PMF-protected) |
| **Pure WPA3** (SAE-only, no WPA2) | ❌ nothing to downgrade to |

Phase 2 (core downgrade) **and** Phase 3 (empirical PMF probe + pre-association-aware flood) are built. What Phase 3 honestly does **not** do: force an *already-connected* PMF-required client to drop — no single-radio tool can. It tells you *whether* PMF is in the way, and keeps disrupting re-joins. Output is a `.cap` for offline cracking with [`cc`](capcrack)/hashcat. Phase 4 (defensive downgrade-detection in `wg`) is not built. **Capture not yet HW-proven against a non-PMF victim.**

---

## See also

- [WPA Sniff](wpasniff) — capture a handshake the normal way (deauth + EAPOL)
- [Cap Cracker](capcrack) — crack the resulting `.cap`
- [Karma](karma) — the rogue-AP engine `w3d` is built on
