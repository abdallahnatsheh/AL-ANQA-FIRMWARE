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
- **Stage 1b DHCP BUILT (untested, 2026-06-30):** added DHCP/BOOTP hostname parsing inside `nsParse`'s
  IPv4 branch → UDP(17)→ports 67/68→`nsParseDHCP()`. Extracts chaddr (real client MAC), yiaddr/ciaddr/
  opt50 IP, and **opt 12 hostname** → `NsDev.name[24]`. Table now shows NAME (falls back to vendor) +
  a `D` how-flag; CSV header now `time,mac,ip,name,vendor,type,how`. **GOTCHA fixed:** bumped
  `NS_PL_MAX 256→400` — DHCP options sit past BOOTP sname(64)+file(128), magic cookie @236/options @240,
  so 256B capture never reached them (ring now ~4.9KB DRAM). `nsAddDev` gained optional `name` param
  (DHCP name authoritative, overwrites). **NEXT in 1b:** mDNS (5353, `.local`+service types) + SSDP
  (1900, SERVER/USN model strings) — defer the `[i]` detail view until those add per-device services.
  Docs/man/CLAUDE.md NOT yet updated (pending HW-verify). Flash + test: `cw` → `ns`, watch for hostnames
  appearing (forces a device to DHCP-renew = quickest test; phones re-DHCP on wifi reconnect).
  **✅ DHCP HW-VERIFIED 2026-06-30** (user confirmed hostnames appear with the `D` flag). First test w/o
  any reconnect showed 37 devices but 0 names (no DHCP event in-window, all `A`/`I`/`AI`) — expected,
  confirms the timing caveat; forcing a reconnect populated names.
- **Stage 1b mDNS BUILT (untested, 2026-06-30):** added `nsParseMDNS()` (UDP 5353) → takes `<host>.local`
  from A(1)/AAAA(28) answer records, strips `.local`, attaches to the frame's L2 src MAC with an `M`
  how-flag (A-record also fills the IPv4). Devices announce mDNS far more often than they DHCP → fills
  names on a quiet net w/o waiting for a DHCP moment (esp. the RandMAC phones). New `dnsName()` helper
  handles DNS label compression (0xC0 ptrs) with a 6-jump loop guard + full bounds checks (can't loop/
  over-read on a crafted pkt). Responses only (QR=1 = announcements). PTR/SRV **service-type** enum
  deliberately deferred → goes in the future `[i]` detail view (with a `services` field). Table/CSV now
  carry `M`. **Caveat:** big multi-record mDNS responses may exceed NS_PL_MAX(400) → later records
  truncated (bounded, no crash; A record is usually near the front). If names are sparse in testing,
  bump NS_PL_MAX.
  **✅ mDNS HW-VERIFIED 2026-06-30** (user confirmed `.local` names appear).
- **Stage 1b SSDP BUILT (untested, 2026-06-30):** added `nsParseSSDP()` (UDP 1900) — `ssdpHeader()` does a
  case-insensitive line-start scan for the `SERVER:` header in the plaintext NOTIFY/M-SEARCH-response,
  extracts the product token after `UPnP/1.x` (e.g. `Roku/9.4`) → name. Uses the new `strongName=false`
  path in `nsAddDev` so a model string fills the name **only when empty** (DHCP/mDNS hostnames stay
  authoritative). `S` how-flag added to table + CSV. Good for media players / TVs / printers that do UPnP
  but no friendly mDNS. **Stage 1b passive parsers now: ARP, IPv4, DHCP(D), mDNS(M), SSDP(S).**
  **✅ SSDP HW status pending** (built 2026-06-30).
- **Stage 1b detail view + service enum + UI polish BUILT (untested, 2026-06-30):** Stage 1b now
  FEATURE-COMPLETE.
  - **Service enum:** `NsDev.svc` uint16 bitmask (11 bits: AirPlay/Cast/Apple/Printer/SSH/SMB/HomeKit/
    Spotify/Alexa/HTTP/DLNA). mDNS: `nsSvcFromName()` maps PTR/SRV record names (`_airplay._tcp.local`
    etc.) → bits; `nsParseMDNS` refactored to ACCUMULATE host+svc across the whole response then commit
    once (`nsAddDev` + `nsOrSvc`). SSDP: `nsRawHas()` scans for `MediaRenderer`/`MediaServer` → DLNA bit.
  - **`[i]` detail overlay** (`nsDetail`): full MAC/IP/Name/Vendor(Type)/Seen(spelled HOW)/Svc(tag list),
    any-key/click to return, lock-aware. Entered via `[i]` OR trackball CLICK on the selected row.
  - **UI polish** (`nsDraw` rewrite): trackball UP/DOWN row selection (bmon pattern) w/ dark-blue
    highlight bar + `>` marker; color-coded — name=cyan, vendor-fallback=grey, selected=yellow; pixel
    columns (NSX_*); `+` cyan service marker on rows with services. Footer shows `trkbl+i=info`.
  - **DOCS DONE (2026-06-30):** man_pages (`ns` entry), README (Network table row + autocomplete example
    `net`→`netd` since `net` is now ambiguous + feature checklist), CLAUDE.md (NetSpy module note +
    Network cmd line + `/apps/netspy` SD-layout line), docs/netspy.md (new standalone page) + docs/index.md
    + docs/network.md (table + inline section), NOTICES #15 (AirSnitch), sdcard_manager `/apps/README.txt`
    folder map line. Autocomplete arg-hints (`gtk dump`) + `SD_DIR_NETSPY` ensureTree already existed.
  - **USABILITY (2026-06-30, user-requested):** (1) detail overlay now opens on **Enter** (`\r`/`\n`) or
    `[i]` — trackball CLICK is NO LONGER the open trigger (user found it annoying); click still *closes*
    the detail (forgiving). Footer→`ent=info`. (2) **CSV now saves ALL fields** incl. a new `services`
    column → header `time,mac,ip,name,vendor,type,how,services`. Shared `nsSvcStr()` helper (+ `NS_SVC_BITS[]`
    moved up next to the early svc helpers) builds the space-sep tag list, used by BOTH `nsSave` and
    `nsDetail`. Line buf 128→256. Docs synced (man/CLAUDE/netspy.md/network.md).
  - **IN-APP PROBE (2026-06-30):** `[p]` ping / `[o]` port-scan the SELECTED device IN PLACE (`nsProbe()`).
    Chosen OVER the `nd`-style `ps <#>`/`pg <#>` index shortcut (user asked "why only netdiscover") because
    in-context probing has no stale-index problem and acts on the row you're looking at. `nsProbe` suspends
    promiscuous (ps/pg need normal TCP/ICMP), calls `networkScanner.pingHost(ip)`/`topPortScan(ip)` (which
    take over the screen until `q`), then re-enables promiscuous+`nsCb` (s_dev table preserved, no SD so no
    GDMA issue). netspy.cpp now `#include "network_scanner.h"` + `extern NetworkScanner networkScanner`.
    Footer→`p=ping o=port`. Docs/man/CLAUDE synced.
  - **CLI TARGETING (2026-06-30):** ALSO support targeting the netspy list from the CLI (user wanted both,
    like nd's `ps 0`). netspy exports `netspyDeviceCount()`/`netspyDeviceIp(idx)` (netspy.h; read `s_dev[]`
    which persists after exit). `network_scanner.cpp` `#include "netspy.h"` + `resolveTarget()` now parses a
    source prefix on the index token: bare `#`/`nd#` = netdiscover ARP idx (unchanged), **`ns#` = netspy idx**
    → `ps ns3`, `ps top ns2`, `pg ns0` (all 3 scan paths share resolveTarget @645/909/1039). Added a `#`
    INDEX COLUMN to the netspy table (replaced the `>` marker; selection still shown via highlight bar +
    yellow) so the user can see which number to type. Columns reflowed (NSX_IDX/IP/WHO/HOW/SVC). Prefix
    collision w/ a bare hostname like `ns3` is accepted/rare (a dotted host `ns3.x.com` still DNS-resolves
    fine — substring isn't all-digits → falls through). man(ps/pg/ns)/README/docs(netspy/portscan/ping)/
    CLAUDE all synced.
  - **✅ FULLY HW-VERIFIED 2026-06-30** (user confirmed working): the whole Stage-1b set (DHCP/mDNS/SSDP
    flags + services + Enter/`[i]` detail + CSV w/ services), the in-app `[p]`/`[o]` probe→resume, AND the
    CLI `ps ns#`/`pg ns#` targeting + `#` index column. Also validated end-to-end earlier: `ns` found a
    device pingable but invisible to `nd` (soft/discovery-only isolation on the test net).
  - **Committed:** `5c45ee8` (Stage 1b) + `dbceef8` (probe + CLI targeting). Unpushed → user pushes manually.
  - **STAGE 1b + probe = DONE.** Next milestone is **Stage 2 — the active `isoscan` GTK-inject** (separate
    command, opt-in, transmits; uses the Stage-0 GTK at `gWpaSm+0x174`). See below.
- **NEXT (after 1b commit):** Stage 2 — GTK inject
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
