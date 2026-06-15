---
name: Karma rogue-AP EAPOL responder (manual half-handshake)
description: Phase-3 rewrite — karma WPA2 bait now a fully manual injected AP that picks its own ANonce, so the half-handshake is crackable. Replaces the IDF-softAP capture.
type: project
---

# karma WPA2 handshake bait — rewritten as a manual EAPOL responder (2026-06-15)

## Why the old approach couldn't crack
Phase-3 used an **IDF softAP** (`WiFi.softAP(ssid,"trexkarma",...)`). HW-verified that
the client's **M2 reaches the promiscuous cb** (good). But cracking needs the **ANonce
from M1**, and M1 is transmitted **by us** — ESP32 promiscuous can't hear self-TX, and
IDF's softAP exposes no API to read the ANonce it chose. So `karmaCrack` always hit
"Need M1+M2". M2 alone (or a .cap of it) is uncrackable. This is a protocol fact, not a bug.

## The fix — we ARE the AP, so we GENERATE the ANonce
New module `wifi/attacks/karma/rogue_handshake.{h,cpp}` (`namespace roguehs`).
We **don't use the IDF softAP at all**. In STA+promiscuous we inject every AP-side
frame by hand and pick the ANonce ourselves → never need to capture M1.

Flow: inject RSN **beacon** (every 100ms) → on directed **probe-req** for our SSID inject
**probe-resp** → on **auth-req** inject open **auth-resp** → on **assoc-req** inject
**assoc-resp** + schedule **M1** (our random ANonce) → sniff the client's **M2**
(SNonce+MIC). Crack with our known ANonce + M2 via the existing `wpacrack::verifyHandshake`.

API: `begin(ssid,ch)` / `poll()` (call every loop) / `state()` / `end()`. `State` carries
live counters (probes/auths/assocs/m1Sent/m2Seen) + crack material (anonce/snonce/mic/eapol/
apMac/staMac) valid once `gotM2`.

## Key implementation details
- **Hardware ACK is the make-or-break.** `esp_wifi_80211_tx` does NOT ACK unicast frames,
  so a pure-injection AP normally can't complete association (client gets no ACK, abandons).
  Mitigation: set the **STA interface MAC = our BSSID** via the stop→`esp_wifi_set_mac`→start
  sequence (mac_changer's proven path) so the **MAC-layer hardware ACKs** the client's
  auth/assoc/M2. This is the single biggest lever for whether it works. UNVERIFIED on hw yet.
- **M1 retransmit schedule** — after assoc we resend M1 up to `RH_M1_RESENDS=10` times,
  `RH_M1_GAP_MS=30`ms apart, until M2 arrives. Sending M1 once right after assoc-resp loses
  it (client not yet settled / no ACK). Stops on gotM2.
- ANonce = 32×`esp_random()` chosen in `begin()`; BSSID = random LA-MAC.
- EAPOL-Key M1 layout verified byte-for-byte against `dot11::parseEapol`: keyinfo `0x008A`
  (ver2|pairwise|ACK), keylen 16, replay=1, nonce@body+13 (=e+17), MIC@e+81 zeroed, body len 95.
- IRAM cb only records events into a DRAM ring (`RhEvt`) + sniffs M2; all injection in `poll()`
  on the main task (can't meet SIFS for a real ACK anyway, injecting from cb is timing-risky).
- M2 capture mirrors handshake_capture offsets (SNonce e+17, MIC e+81, frame len (e[2]<<8|e[3])+4).

## karma.cpp changes
- `karmaHandshake()` rewritten to drive `roguehs::begin/poll/end`; live screen shows
  `Prb/Ath/Asc/M1` counters + M2 status (stage diagnostic so you can see where each client
  stalls on real hardware). Snapshots `State` before `end()`, then cracks.
- `karmaCrack()` refactored: now takes explicit `(ssid, apMac, staMac, aNonce, sNonce, eapol,
  eapolLen, mic)` instead of re-parsing captured frames (ANonce is known, not parsed). Kept the
  SD/built-in **wordlist picker** (`[1]`/`[2]`, mirrors `ws`).
- Removed the old IDF-softAP machinery: `KmHsFrame`/`s_hs`/`karmaHsCb`/`sanitizeSsid` + the
  `.cap` write. (Old .cap was M2-only → uncrackable; not a real loss. A proper crackable .cap
  = synthesized M1 + sniffed M2 could be re-added later for offline aircrack/hashcat.)

## Also fixed same session (unrelated karma bugs)
- **Portal SD templates**: `cpPickTemplate(sdDir, out, sdDir2=nullptr)` gained an optional 2nd
  dir; karma portal now scans `/apps/karma/portal` **and** `/apps/eviltwin/portal` so existing
  eviltwin templates show up (was: karma's own empty dir → only built-ins listed).
- **Wordlist picker** added to the crack (was auto-SD-then-builtin with no choice).

## Follow-ups added after first hw success (2026-06-15)
HW MILESTONE: **`M2!` captured on real hardware** — the manual rogue-AP half-handshake
works (client associated to our injected AP + sent M2). Believed first on ESP32. Then added:
- **Crackable .cap export** (`karmaSaveCap` in karma.cpp): writes `/apps/karma/<ssid>.cap`
  (libpcap lt 105) = our beacon (ESSID) + the injected **M1** + the sniffed **M2**. Engine
  now retains the raw frames in `State` (`beacon/m1Raw/m2Raw` + `capTs`). M1 replay counter (1)
  == the counter the client echoes in M2 → aircrack/hashcat pair them. **`m2Raw` keeps the MIC
  intact** (only `State.eapol` is MIC-zeroed, for the on-device HMAC) — essential for offline crack.
  Saved after teardown (GDMA-safe), unconditionally when gotM2.
- **Harvest/devices SD save** (`[s]` in harvest loop → `karmaSaveTables`): sequential
  `/apps/karma/NNN.csv` (wguard/bmon scheme via `nextKarmaSeq`), one file with `[NETS]`
  (ssid,devices,hits,rssi,channel) + `[DEVICES]` (id,vendor,type,macs,randomized,pnl_count,
  rssi,pnl) sections. Wrapped in `ScopedPromiscPause` (GDMA). `csvSafe()` neutralises commas
  in SSIDs. Transient "Saved NNN.csv" notice in the footer (`s_saveNoticeMs`, 2.5s); `[s]`
  added to both footer hint strings.

## Status — HW-VERIFIED END TO END ✅ (2026-06-15)
Full pipeline confirmed on real hardware: rogue AP → client associates → injected M1 (our
ANonce) → sniffed M2 → **on-device dictionary crack recovered the test network's password
from a wordlist.** The manual-AP hardware-ACK approach (STA MAC = BSSID) works — clients
associate and complete M2. Believed to be the first ESP32 rogue-AP half-handshake captured
AND cracked on-device. (.cap export + NNN.csv table-save coded but not separately confirmed;
the crack path that consumes the same material is verified.)
First open question on hw: does setting STA MAC=BSSID get the chip to ACK so the client
associates + sends M2? The on-screen Prb/Ath/Asc/M1/M2 counters answer it. If clients reach
Asc but never M2 → ACK/association is the wall; next lever = verify esp_wifi_set_mac succeeded
+ try injecting from cb / tune timing. Believed to be the first ESP32 rogue-AP half-handshake.

Related: [[project_karma_plan]] (Phase 3 superseded), `wpa_crack`/`ws`, `dot11.h`, `captive_portal`.
