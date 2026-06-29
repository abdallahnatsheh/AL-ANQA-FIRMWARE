---
name: netspy + isoscan plan (client-isolation bypass, AirSnitch)
description: Stage 0 DONE (GTK extraction proven) — netspy/ns (discover devices past client isolation) + isoscan/is (GTK inject, gateway bounce, port stealing)
type: project
---

## BUILD STATUS (2026-06-29) — Stage 0 + Stage 1 built (not yet committed)
- **Stage 0 ✅ HW-VERIFIED:** `ns gtk` prints the live GTK from `gWpaSm+0x174`, changes per reconnect.
  Platform PINNED `espressif32@7.0.1` (offset can't drift). Build fix: `dm.printText()` not `print()`.
- **KEY ARCHITECTURAL FINDING (HW-verified, changes everything):** while ASSOCIATED, the ESP32's WiFi
  HARDWARE already DECRYPTS group/broadcast frames and hands them to the promiscuous callback in CLEAR
  (CCMP header still present, payload plaintext). Confirmed: captured an ARP who-has whose payload @
  hdrlen+8 was `AA AA 03 00 00 00 08 06 …` (LLC/SNAP + ARP), decoding to a real device 10.0.111.254 /
  xx:xx:xx:xx:xx:xx — a device `nd` (ARP scan) can't see under client isolation. So **passive discovery
  needs NO software CCMP and NO GTK** — just sniff group data frames from our BSSID and parse the
  plaintext. (The `ns dec` CCMP-decrypt experiment failed MIC precisely BECAUSE the data was already
  decrypted — that's how we found this. The GTK/Stage-0 work is still needed for Stage 2 INJECT, where
  `esp_wifi_80211_tx` does NOT auto-encrypt.)
- **Stage 1 BUILT (untested):** `ns` = live device-discovery scanner. Promiscuous-captures group data
  frames (fromDS, A1 group, A2==our BSSID), rings them to the main loop, parses LLC/SNAP → ARP (sender
  MAC+IP) and IPv4 (A3 src MAC + IP) → `NsDev[48]` table (MAC/IP/vendor via oui_lookup/how). UI table
  (IP/VENDOR/H), `[s]` save `/apps/netspy/NNN.csv` (GDMA via ScopedPromiscPause since promiscuous is
  live), `[c]` clear, `[l]/[a]` page, `[q]` quit. Subcmds: `ns gtk`, `ns dump`. Module `wifi/intel/`.
- **NEXT:** Stage 1b — parse DHCP/mDNS/SSDP for hostnames/services (richer table). Stage 2 — GTK inject
  (software CCMP-encrypt a broadcast data frame + AP-MAC spoof + high PN) = the AirSnitch active attack.
- **Name: keep `netspy`/`ns`.** Module `wifi/intel/netspy.cpp/.h`, registered Network, `-I wifi/intel`,
  `SD_DIR_NETSPY=/apps/netspy` + ensureTree. Cmd count 60/64.
- **Feasibility resolved:** full AirSnitch GTK *inject* core is blocked on ESP32 by the CLOSED WiFi
  blob (no GTK-export API; `esp_wifi_80211_tx` won't CCMP-encrypt) — a *firmware/programming* wall,
  NOT silicon. BUT the GTK is negotiated by the OPEN `wpa_supplicant` whose global `gWpaSm` is an
  **exported symbol** → read the key from RAM, no PHY-blob disassembly needed.
- **GTK OFFSET (framework-specific): `gWpaSm + 0x174`** = 16-byte CCMP GTK; **len at `+0x194`** (=16).
  Confirmed 2 ways: struct-math vs IDF-4.4.7 `struct wpa_sm` (pmk[64]@0, pmk_len@0x40, ptk@0x44,
  gtk@0x174, ssid@0x1c4) AND `wpa_supplicant_install_gtk` disasm reads `+0x194` as gtk_len. Also PMK
  is at `gWpaSm+0x00` (32B). **Valid ONLY for arduino-esp32 2.0.17 / IDF 4.4.7** (platform
  espressif32 7.0.1, UNPINNED — pin before relying). `ns dump` → 844B hex to `/apps/netspy/gwpasm.txt`
  to re-find the offset if toolchain changes; `ns` shows live GTK with a len!=16/32 sanity guard.
- **NEXT — Stage 1:** software AES-CCMP **decrypt** of promiscuous-captured group/broadcast frames
  with the GTK → real isolation-bypass discovery (DHCP/ARP/mDNS + MACs). **Stage 2:** software CCMP
  **encrypt** + inject non-QoS broadcast data frame (80211_tx allows it) spoofing AP MAC + high PN →
  AirSnitch active attack. **Stage 3:** wrap into netspy/isoscan. HW-test-gated (instrument, 1 flash =
  1 datapoint). ⚠️ Stage 0 uncommitted (research instrumentation) — commit once Stage 1 works/stable.

**Commands: `netspy`/`ns` + `isoscan`/`is`** (Network). Stage 0 built (above). Full plan in repo:
`.claude/memory/TREX_NETSPY_ISOSCAN_PLAN.md`. Based on **AirSnitch (NDSS 2026, Mathy Vanhoef)** —
https://github.com/vanhoefm/airsnitch. **No AirSnitch code used** — techniques reimplemented from
the paper (AirSnitch = all-rights-reserved, no license file). Credit paper+repo in comments/README only.

Both require T-Deck **connected to the target network** first (`cw`/`connectwifi`).

## netspy/ns — discover devices despite client isolation
ARP scan (`nd`) fails under client isolation (AP blocks unicast ARP between clients). netspy uses
broadcast/multicast channels isolation doesn't filter, run in parallel:
1. **DHCP snoop** (passive promisc): DISCOVER/REQUEST/ACK → MAC, hostname, IP, vendor.
2. **mDNS** (224.0.0.251:5353): hostnames `.local`, service types (`_airplay/_googlecast/_ipp/_smb/_ssh`…).
3. **SSDP/UPnP** (239.255.255.250:1900): device type/maker/model.
4. **IPv6 ND** multicast: link-local IPv6 + MAC.
5. **Assoc-frame sniff**: MACs from management frames (isolation only filters data frames).
6. **GTK broadcast ARP** (active): encrypt a broadcast `who-has 255.255.255.255` with the shared GTK
   → AP forwards to all clients → responders are `GTK-REACHABLE` = confirmed isoscan targets.
7. **OUI vendor ID** (reuse `oui_lookup.h`).
Table: IP/VENDOR/GTK/HOW(D/M/S/G). `[i]` detail, `[a]` export→isoscan, `[s]` save, `[g]` GTK-ARP now.

## isoscan/is [#idx] — client-isolation bypass attacks on one target
- **gtk** — capture 4-way handshake during assoc, extract GTK; report shared vs randomized.
- **inject** — GTK-encrypt a broadcast frame the victim accepts; flagship = **ICMPv6 RA DNS poison**
  (`dns=T-Deck-IP`) → victim's DNS → T-Deck → log queries to SD (UDP 53 listener). Also arp/raw.
- **bounce** — reach victim at IP layer via gateway MAC (isolation only checks MAC layer).
- **bcast** — to-DS broadcast frame w/ victim unicast IP; reply ⇒ AP forwards ⇒ GTK inject viable.
- **portdown / portup** — spoof victim/gateway MAC (`esp_wifi_set_mac`) → steal victim's incoming/
  outgoing traffic → PCAP to SD.
- **auto [#]** — run all, report which succeed + best attack.
**CRITICAL safety**: port stealing changes T-Deck MAC — save `originalMac` at start, restore on
EVERY exit path (q/error). Reuse `mac_changer`/`mac_util.h` restore idiom.

## Reuse map / current-repo mapping (plan paths are foreign — DO NOT follow verbatim)
- Plan: `shell.cpp`, `attacks/netspy|isoscan|iso_common`. Current: register in
  `core/cli/command_manager.cpp` `setupCommands()`; put modules under `wifi/` (e.g. `wifi/attacks/`
  or a new `wifi/intel/`). SD → `/apps/netspy/`, `/apps/isoscan/` (v2 layout, NOT `/logs/`).
- **GDMA rule applies hard**: promisc + SD logging together → use `ScopedPromiscPause` for every
  mid-session SD write (this is exactly the bug class just fixed in macwatch — see [[progress_log]]).
- Reuse: promiscuous setup/hop + IRAM_ATTR sniff pattern (trackme/wifimon), `oui_lookup.h`,
  handshake/GTK extraction groundwork in `handshake_capture.cpp`, PCAP writer (`pcap_writer.h`).
- Radio-conflict: both are promisc/injection foreground cmds — abort if a conflicting WiFi mode is up;
  call `stopEspchatBg()`/`stopMacwatchBg()` before starting (singleton radios).
- New/hard parts: GTK extraction+use, ICMPv6 RA crafting, MAC-spoof port stealing, the multi-protocol
  passive parsers (DHCP/mDNS/SSDP/IPv6-ND).

Coding standards (plan): no dynamic alloc / no STL / fixed buffers / RED-ish header / dm. + inputHandler.
Authorization disclaimer required (intercepts other users' traffic).
