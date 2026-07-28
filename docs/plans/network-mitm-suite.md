# Plan — Network MITM suite (`arpspoof` + `responder`), phased

> Status: Phase 1 (`arpspoof`, now also logs redirected dst IP/DNS/HTTP) +
> Phase 2 (`responder`) BUILT, builds clean — AWAITING HW LAB TEST. Responder now
> covers LLMNR + NBT-NS + **mDNS**, HTTP **+ SMB(445, best-effort 2b)**, NetNTLMv2
> **+ NTLMv1 + HTTP-Basic**, **WPAD PAC**, and logs **every** poisoned query.
> Remaining: HW-verify (esp. the SMB2/SPNEGO framing vs a real Windows victim).

## Context
T-REX has recon (`nd`, `ns`) and isolation-bypass (`is`) but no active LAN MITM tooling.
Built in two phases.

**Honesty constraint (settled in the isoscan work):** true transparent traffic interception
(`lanmitm` gateway takeover with forwarding) is NOT achievable on this single-radio ESP32 —
ARP poison doesn't hold against Windows, there's no IP-forwarding path, and DTIM/routing
defeats it. So the suite delivers the two pieces that genuinely work, each `[EXP]` and honest
on-screen about limits:

- **Phase 1 — `arpspoof`/`as`**: L2 ARP-cache poisoning (redirect/blackhole a victim +
  gateway). Without forwarding this is a redirect/DoS, not a silent interceptor — stated plainly.
- **Phase 2 — `responder`/`rsp`**: LLMNR/NBT-NS poisoner + fake HTTP(-auth) listener capturing
  **NetNTLMv2** hashes to SD for offline cracking (hashcat -m 5600). No ARP/forwarding needed.

Both are own-network-only offensive tools.

## Reuse (found during exploration)
- **ARP TX primitive:** raw ethernet+ARP `pbuf` → `netif_default->linkoutput(netif, p)` under
  `LOCK_TCPIP_CORE()`. WiFi driver encrypts with the PTK (correct for WPA2; `esp_wifi_80211_tx`
  would NOT encrypt). Mirrors Bruce `src/modules/ethernet/ARPoisoner.cpp` (AGPL-3.0, already in
  NOTICES — extend that entry).
- **MAC resolution:** `etharp_request()` + `etharp_find_addr()` under `LOCK_TCPIP_CORE()` —
  pattern at `wifi/monitor/netscanner/network_scanner.cpp:443-459`.
- **Victim targeting:** `resolveTarget()` (ns#/nd#/IP) at `network_scanner.cpp:41` — expose as
  public `bool resolveNetTarget(const String&, IPAddress&)` in `network_scanner.h` and reuse.
  MAC for `ns#` via `netspyDeviceMac()` (`wifi/intel/netspy.h`).
- **Gateway:** `WiFi.gatewayIP()`; our MAC via `WiFi.macAddress()`.
- **Raw UDP listeners (responder):** `udp_new_ip_type`/`udp_bind`/`udp_recv` tcpip-thread cb →
  ring → main-loop drain — pattern at `wifi/attacks/isoscan/isoscan.cpp:1089-1152`.
- **TCP listener:** Arduino `WiFiServer`/`WiFiClient` (as in eviltwin).
- **Base64 decode (NTLM Type-3):** mbedTLS base64 (already a dep).
- **Interactive picker + flicker-free UI + `q`-poll + lock-awareness:** isoscan/netspy patterns.

## Phase 1 — `arpspoof`/`as`  (Network, [EXP])
New module `wifi/attacks/arpspoof/arpspoof.{cpp,h}` — free fn `runArpSpoof(char*)`. Register
one-liner in `command_manager.cpp` `setupCommands()`, category "Network", `COMP_NONE`.
- `as <victim> [gateway]` — victim = ns#/nd#/IP via `resolveNetTarget`; gateway defaults to
  `WiFi.gatewayIP()`. Bare `as` → netspy-style interactive picker.
- Require `WiFi.status()==WL_CONNECTED` (else "run cw first").
- Resolve victim MAC (netspyDeviceMac for ns#, else etharp) + gateway MAC (etharp). Abort if unresolved.
- Loop ~1 s: send two `ARP_REPLY` via `linkoutput` — (a)→victim: sender=gatewayIP@ourMAC;
  (b)→gateway: sender=victimIP@ourMAC. Poll `q`; live stats + `[EXP]` "redirect/DoS — no forwarding".
- **Heal on exit (better than Bruce):** send corrective replies with the REAL MACs to both ends,
  then leave WiFi in STA (never `disconnect(true)`/`WIFI_OFF`).
- No SD writes → no GDMA concern; lwip TX, not promiscuous.

## Phase 2 — `responder`/`rsp`  (Network, [EXP])
New module `wifi/attacks/responder/responder.{cpp,h}` — free fn `runResponder(char*)`. Register
"Network", `COMP_NONE`.
1. **Name poisoners** (raw lwip udp pcbs): LLMNR (5355, mcast 224.0.0.252) + NBT-NS (137, bcast).
   Parse queried name (incl. `wpad`), reply A-record = our STA IP for every query (QR=0). Ring →
   drain → live list. (mDNS 5353 optional/off by default.)
2. **HTTP NTLM catcher** (`WiFiServer` :80): `401 WWW-Authenticate: NTLM`; on `Authorization: NTLM
   <b64>` Type-3, base64-decode → user/domain/NTProofStr/blob → hashcat **-m 5600** line using a
   fixed 8-byte challenge (`1122334455667788`). `wpad` poison → client fetches wpad.dat → 401 → NTLM.
3. **Output:** `/apps/responder/hashes.txt` (append) + `/apps/responder/NNN.csv`
   (`time,proto,src_ip,user`). Plain STA sockets (no promiscuous/AP) → SD writes safe. Live UI +
   `q` quit, lock-aware. `[EXP]` "NetNTLMv2 for OFFLINE cracking; HTTP-auth only (SMB is future 2b)".
4. **SD wiring:** `SD_DIR_RESPONDER "/apps/responder"` in `sdcard_manager.h`; `ensureDir` +
   `ensureAppsReadme()` row.

## Cross-cutting
- `platformio.ini` `build_flags`: add `-I .../wifi/attacks/arpspoof` and `.../wifi/attacks/responder`.
- `core/cli/man_pages.cpp`: entries for both.
- Docs: README command table (Network) + `docs/network.md` + CLAUDE.md + NOTICES (extend Bruce;
  credit lgandx/Responder as methodology). progress_log memory entry.

## Verification (hardware — real LAN + victim host)
- **arpspoof:** `cw` → `nd`/`ns` → `as ns#`. On victim `arp -a` shows gateway IP → T-Deck MAC;
  victim loses internet while running; quit → cache heals, internet returns.
- **responder:** `cw` → `rsp`. From a victim trigger a bad name lookup (e.g. `\\wpad\`) → poisoned
  query on-screen + NetNTLMv2 appended to `/apps/responder/hashes.txt`; cracks with `hashcat -m 5600`.
- Build: `pio run -e T-Deck`; CI on push.

## Scope / non-goals
- No `lanmitm` transparent forwarding (not feasible single-radio).
- Responder v1 = LLMNR + NBT-NS + HTTP-NTLM; SMB (445) capture is a labelled future 2b.
- No on-device cracking of NetNTLMv2 (offline hashcat).
