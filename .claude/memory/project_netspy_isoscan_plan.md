---
name: netspy + isoscan plan (client-isolation bypass, AirSnitch)
description: NOT YET BUILT — netspy/ns (discover devices past client isolation) + isoscan/is (GTK inject, gateway bounce, port stealing)
type: project
---

**Commands: `netspy`/`ns` + `isoscan`/`is`** (Network). NOT YET BUILT. Full plan in repo:
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
