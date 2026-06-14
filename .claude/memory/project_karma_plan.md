---
name: Karma attack plan (km)
description: Design + build plan for the karma/km command — two modes, two bait types, PNL fingerprinting, intel cards; NOT YET BUILT
type: project
---

# karma / km — anti-client rogue-AP suite (PLANNED, not yet built)

Command `karma`/`km`, WiFi category. New module `wifi/attacks/karma/karma.cpp`+`.h`
(own folder per coding rules); add include path to `platformio.ini` `[includes]`.
Goal: NOT a copy-paste of Marauder/Bruce karma — adds fingerprinting + intel +
reactive karma using assets they lack (GPS, `oui_lookup.h`, `ws` cracker, PSRAM).

## Research basis (verified this session)
- Marauder Karma (only shipping impl) = "Evil Portal using the SSID from sniffed
  probe requests as the rogue AP name" → single-SSID, open, portal. Bruce #1472 =
  same idea, still UNIMPLEMENTED.
- **ESP32 hardware limit:** softAP accepts associations for its ONE configured SSID
  only; `esp_wifi_80211_tx` can inject probe-resp but the HW MAC checks SSID on
  assoc → can't do Linux hostapd-mana multi-SSID/MANA or EAP-relay (hostapd-wpe).
  `ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED` does NOT carry the requested SSID → MUST
  use promiscuous sniff + parse SSID IE (reuse `wm` parser).
- **WPA2 half-handshake IS possible on ESP32-S3:** stand up WPA2 softAP with the
  probed SSID (any password), client auto-joins, sends M2 (SNonce+MIC keyed by the
  REAL password's PMK). M1(ours)+M2 = crackable offline (half handshake) with the
  existing `ws` engine. No deauth / no real AP needed. WPA3/SAE immune. Yields a
  crackable hash, not the password.
- One HARDWARE UNKNOWN to validate early: does our OWN softAP's M2 reach the
  promiscuous callback? (third-party EAPOL works — `ws`/`pm` prove it; local-AP M2
  unverified.) Fallback if not: deauth-driven evil-twin vs the REAL AP = today's `ws`.

## Two top-level modes (user's requirement)
- **Auto** — fire-and-forget: harvest, fingerprint, auto-detect each SSID's security,
  auto-bait (open→portal, WPA2→handshake), reactive-karma + lures, all logged. Watch only.
- **Interactive** — operator picks a specific sniffed network (or device→its networks),
  then chooses per-target: `[p]` portal or `[h]` handshake. Run → results → back to list.

Syntax: `km` (mode menu) · `km auto` · `km i` · `km <ssid>` (quick-bait one, then pick bait).

## Shared Listen/Harvest phase (both modes)
Promiscuous probe sniff, channel-hop 1→13 (reuse `WiFiMonitor::extractSSID()` + `bf`
hop). Three PSRAM tables (lazy `ps_malloc`, project pattern):
- `KmProbe[]` (MAC,SSID)→hits,rssi,first/last,gps
- `KmDevice[]` = **PNL fingerprint**: cluster MACs by their SSID-set; ssids, member MACs,
  vendor+type (`oui_lookup.h`), rssi, gps, "seen as N MACs"
- `KmNet[]` unique SSID→#devices, security(open/WPA2 via live-scan cross-ref), popularity

## Two bait sub-types (reuse, don't rewrite)
- **Mode-A portal:** extract a shared captive-portal helper from `eviltwin` (DNS+routes+
  `captureArgs`+GDMA-safe cred flush) — also helps future `responder`/`apbridge`.
- **Mode-B handshake:** WPA2 softAP + promiscuous EAPOL sniff → parse M1/M2 → `ws` cracker
  → `.cap`+`cracked.csv`. Fallback = deauth evil-twin vs real AP.

## The 3 differentiators (the "something new")
1. 🥇 **PNL fingerprinting → defeats MAC randomization**: cluster randomized MACs by their
   probed-SSID set (near-unique signature); show physical DEVICES not MACs. Bruce #1472
   gave up on this; nobody ships it on ESP32.
2. 🥈 **Reactive (just-in-time) Karma**: softAP SSID follows whoever is actively probing
   NOW (time-multiplex on single radio) + beacon-spam rest as lures (`bf`). Approximates
   MANA multi-SSID on one-SSID hardware. (v2 — timing-sensitive.)
3. 🥉 **Device intel cards**: per device = vendor+type (oui), full PNL, RSSI proximity,
   GPS+time tag (Plus) → karma+wardriving fusion.
Bonus: context-aware crack seeding (target+harvested SSIDs into `ws` wordlist) — v2.

## SD `/apps/karma/`
`pnl.csv`(time,mac,ssid,rssi,lat,lon) · `devices.csv`(id,pnl_sig,ssids,macs,vendor,type) ·
`connects.csv`(intel cards — who took bait) · `creds.csv` · `<BSSID>.cap`+`cracked.csv` ·
`wordlist.txt`. Add `SD_DIR_KARMA`+consts to `sdcard_manager.h`, register in
`ensureTreeStructure()`.

## Degraded operation — MUST work with no SD and no GPS (both boards)
Karma is fully functional headless. SD + GPS are enrichment only, never required.
- **No SD card** (or `!canAccessSD()`): everything runs RAM/PSRAM-only, session-scoped
  (mirror espchat contacts / trackme RAM fallback). Harvest tables, fingerprints, intel
  cards, captured creds, captured handshakes all live in PSRAM. Skip every CSV/.cap/.pcap
  write silently (no error spam) and show a one-line "No SD — session only" notice like
  lockscreen/eviltwin do. On-device crack STILL works (handshake held in RAM; `ws`
  built-in 100-pw wordlist when `/apps/karma/wordlist.txt` absent). Portal uses
  eviltwin's 2 built-in templates when no SD `/portal/*.html`. Cred capture is RAM-only
  by design anyway (eviltwin pattern). Only loss without SD = no persistence after reboot.
- **No GPS** (T-Deck base board, or Plus with no fix): all GPS tagging is
  `#ifdef BOARD_TDECK_PLUS` AND gated on a valid fix (`GpsManager` fixed state). Without
  it, intel cards/`connects.csv`/`pnl.csv` just omit lat/lon (write blank or skip the
  column) — fingerprinting + portal + handshake are unaffected. T-Deck base never compiles
  GPS code paths. Do NOT hard-depend on GpsManager being present.

## Constraints
GDMA: open SD before AP/promisc; RAM-buffer; flush via `ScopedPromiscPause` or post-teardown;
exit `WiFi.mode(WIFI_STA)`/`disconnect(false)`. Poll `q` everywhere; output via
`displayManager`; lockscreen `consumeJustUnlocked()` redraws.

## Build order (test each on hardware — user wants manual test per phase)
1. ✅ DONE — Harvest + live table (`wifi/attacks/karma/karma.cpp`+`.h`, cmd `karma`/`km`,
   WiFi cat). Promiscuous MGMT sniff, hop 1→13, parse probe-req SA+SSID, two PSRAM tables
   (`KmProbeRec` per-(MAC,SSID) foundation for fingerprinting + `KmNet` per-SSID view).
   Table cols SSID/DEV(distinct MACs)/HIT/RSSI, sorted by devs. `[a/l]`page `[c]`clear `[q]`stop.
   Lazy ps_malloc + free on exit. ISR ring in DRAM, tables in PSRAM. Field-tested working.
2. ✅ DONE — PNL fingerprint + intel cards (🥇🥉). `[v]` toggles HARV(nets)↔DEVS view.
   DEVS clusters MACs into physical devices via union-find: link if share ≥2 distinctive
   SSIDs (distinctive = wanted by ≤4 MACs) or one PNL⊆other +≥1 distinctive. Cols
   VENDOR/TYPE(oui_lookup)/PNL/MAC(green if >1 = randomization defeated)/RSSI; trackball
   selects, detail pane shows device's full PNL. Rebuild throttled 1.5s, only while DEVS
   open. Fixed-grid rows (LINE_HEIGHT pitch) so highlight bar (0x0841 @ ry-1) hugs one row.
   Honest limit: per-network MAC rand (iOS14+/Android10+) → single-MAC devices; GrapheneOS
   sends no directed probes → invisible (expected, verified on hw). Field-tested working.
3. ✅ DONE — WPA2 handshake bait, SAVE-ONLY (user's call: no on-device crack — crack offline
   w/ aircrack/hashcat or a future CLI tool; keeps `ws` cracker untouched). `km hs <ssid> [ch]`
   standalone + interactive `[h]` from HARV(selected SSID)/DEVS(selected device's top PNL).
   WPA2 softAP clones SSID w/ throwaway PSK "trexkarma"; promiscuous MGMT+DATA sniffs our
   beacon + EAPOL (filter on our softAP MAC); M2 (ack=0,mic=1,secure=0) flagged live w/ STA
   MAC. Capture buf in INTERNAL DRAM (heap_caps_malloc MALLOC_CAP_INTERNAL — ISR writes, NOT
   PSRAM), freed on exit. Writes /apps/karma/<ssid>.cap (libpcap lt 105) AFTER teardown
   (GDMA). No-SD→RAM count only. **HW-VERIFIED: our own softAP's M2 DOES reach promiscuous cb
   on ESP32-S3 → deauth-free attack works, no fallback needed.** Interactive [h] pauses harvest
   (harvestStop), runs bait, resumes (harvestStart), [any key] back to list. Bait ch=6 (clients
   scan all). HARV got selection cursor + fixed-grid rows. SD_DIR_KARMA added + ensureTree.
   Field-tested: M2 captured ✅.
4. ✅ DONE — open soft-AP + captive portal (cred capture). `km portal <ssid>` + interactive
   `[p]`. Field-agnostic capture, creds→/apps/karma/creds.csv (ssid,user,pass) on exit, RAM
   fallback no-SD. Field-tested working.
   man page + autocomplete (`km`→`hs portal`) added.
=== REFACTOR (build-verified, T-Deck-Plus clean) — shared utils in wifi/core ===
Stopped karma copy-pasting. Extracted 3 shared modules (precedent: oui_lookup/wifi_sd_guard):
- `wifi/core/dot11.h` (hdr-only): fType/fSubtype/toDS, ST_* subtypes, extractSSID(), parseEapol()
  (LLC 0x888E + M1-4 classify), dataBssid(). Karma harvest+hs callbacks now use it.
- `wifi/core/pcap_writer.h` (hdr-only): pcap::writeGlobalHeader/writeRecord (linktype 105).
- `wifi/core/captive_portal.h/.cpp`: CaptivePortal class (open/WPA2 AP+DNS+probe-redirects+
  field-agnostic capture+templates); karma portal is now a ~30-line wrapper (new/delete per use).
  Has 3 SHARED built-in templates (Generic/Google/Router) + SD .html loading + a shared paginated
  picker `cpPickTemplate(dir,&choice)` (built-ins + SD files). karma shows it before AP launch
  (`[p]`/`km portal`); cancel aborts. Templates in /apps/karma/portal/. eviltwin still has its own
  HTML_GOOGLE/HTML_ROUTER copies (migrate later → delete them, use cpBuiltins + cpPickTemplate).
Karma's duplicated portal/classifier/pcap-structs/EAPOL+SSID parse all deleted. eviltwin/
handshake/pmkid/wifimon NOT migrated yet (opt-in, migrate when next touched — same policy as
ScopedPromiscPause). Future responder/apbridge/lanmitm should use captive_portal + dot11.
+ `wifi/core/wpa_crack.h/.cpp`: derivePMK (PBKDF2), verifyHandshake (4-way MIC), verifyPMKID,
  shared built-in 100-pw list. ws + pm tryPassword now delegate to it (crypto + list byte-identical
  → zero behavior change; ws/pm builtin arrays were verbatim equal). karma got ON-DEVICE crack:
  `[c]` on the WPA2 capture screen → karmaCrack() extracts M1 aNonce + M2 sNonce/MIC/EAPOL from the
  captured frames (dot11::parseEapol), runs SD /apps/karma/wordlist.txt then built-in via
  verifyHandshake, saves to /apps/karma/cracked.csv. (Karma is no longer save-only — has both .cap
  AND on-device crack now.)
Also migrated to shared pcap_writer/dot11: pcap in handshake/pmkid/wifimon; EAPOL parse in
handshake/pmkid; SSID extract in wifimon (delegate). FIXED a real bug while extracting: dot11::
parseEapol guard was >=4 EAPOL bytes but reads e[5]/e[6] → tightened to >=8 (matches old ws/pm,
removes a 1-3 byte over-read). hidden_ssid/wguard SSID + eviltwin portal NOT migrated yet.
Still TODO: dot11_tx.h (beacon/deauth builders) + promisc.h (start/stop+hop) — deferred.
   → 5. Auto/Interactive top-level mode menu + connects.csv intel logging
(validate local-M2; fallback ready) → 4. Mode-A portal (shared helper) → 5. Interactive
flow → 6. Auto mode (security auto-detect + reactive karma + lures) → 7. docs/man/README/memory.

Related: [[next_steps]] (item 1), [[project_eviltwin_overhaul]], `handshake_capture`/`ws`,
`wifimon`/`wm` probe parser, [[project_macwatch_idea]] (proximity overlap).
