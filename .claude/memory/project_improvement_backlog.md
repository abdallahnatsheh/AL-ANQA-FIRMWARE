---
name: Project improvement backlog (hardening, not features)
description: Prioritized project-wide improvements — testing/correctness, maintainability, structural. NOT new attacks. User will consider these.
type: project
---

# Project-wide improvement backlog (2026-06-15)

Assessment of the whole T-REX firmware (~60 commands, mature). Leverage now is in
**hardening + maintainability**, NOT more features. User asked to save these to consider.
(New attacks live in [[next_steps]] — deliberately NOT prioritized here.)

> **Unit tests dropped (2026-06-17, user decision).** The native `pio test` env for
> `wpa_crack`/`dot11`/`pcap`/`oui` is explicitly NOT pursued — do not re-suggest it.
> Validation is done empirically (real captures cracked on a PC, see below).

## Tier 1 — highest value (prove it works)  — ESSENTIALLY DONE
1. **Close the "compiles ≠ works" gap.** Validation pass — nearly complete:
   - DONE (2026-06-17): `bleinfo`, `i2cscan` (experimental), `espvoice` private mode —
     all manually tested on hardware by the user. No longer "untested".
   - DONE (2026-06-17): `capcrack` / karma `.cap` PC-crack path verified in Kali WSL —
     a rogue karma cap cracked by BOTH aircrack-ng and hashcat (-m 22000), matching the
     on-device result. See [[project-cap-validation-kali]] (user memory).
   - PENDING (2026-06-17, uncommitted): found `ws`/`pm` caps had no beacon/ESSID → not
     PC-crackable; added `wifi/core/beacon_build.h` so both prepend a synthesized beacon.
     Only remaining Tier-1 item: flash + re-validate (capture fresh `ws`/`pm`, confirm
     hashcat emits a hash).

## Tier 2 — maintainability
2. **Finish shared-util extraction.** Frame injection is still copy-pasted across karma,
   beacon_flood, eviltwin, deauth, wifimon, hidden_ssid (each hand-rolls 802.11 builders +
   promiscuous start/stop/hop). Plan names the targets: `dot11_tx.h` (beacon/deauth/probe
   builders) + `promisc.h` (start/stop/hop). Kills the last big dup, centralizes the trickiest code.
   - The new `wifi/core/beacon_build.h` (2026-06-17) is a first down-payment — `ws`/`pm`
     now share one beacon builder; `karma`/`beacon_flood` still have their own → fold in later.
3. **Migrate remaining modules to `ScopedPromiscPause`.** Only eviltwin + karma use the GDMA
   guard; wguard/wifimon/handshake/pmkid still hand-roll pause/resume — where GDMA corruption hides.

## Tier 2b — RAM/flash reclaim — DONE + HW-VERIFIED 2026-07-02
- **Bluedroid/BLE-Mesh accidentally linked in — FIXED.** `mac_changer.cpp`'s `applyBleMac()`
  called legacy Bluedroid instead of NimBLE (the ONLY place in the codebase that did),
  dragging in 150 extra objects from `libbt.a`. Swapped to `NimBLEDevice::setOwnAddrType`/
  `setOwnAddr`. Measured: **-19.7KB RAM, -428KB flash** on T-Deck-Plus; both envs build
  clean. Bonus finding: the old code was dead (Bluedroid never enabled elsewhere), so
  `mc target bt` never actually worked before this fix either — user confirmed on hardware
  that the BLE MAC now genuinely changes. See [[project_bluedroid_ram_flash_leak]] for full
  detail, including why `bk`/`bd` can't be used to verify it over the air.

## Tier 3 — structural / housekeeping
4. **64-command cap: at 59/64** (62 → 57 via the 2026-06-22 merges: `wp export/clear`, `ps top`,
   `test spk/mic/lora`; then `edit`/`ed` + `macwatch`/`mw` + `csidetect`/`csi` added 3 — net 59).
   5 slots left. Further merge candidates if needed:
   ESP-NOW `es/est/ec/ev` → `esp <sub>` (−3, but flagship apps), USB family → `usb <sub>` (−2).
5. **Centralized WiFi state helper** ("enter sniff / enter inject / return idle"). Most hard bugs
   lived in per-module `WiFi.mode`/promiscuous/APSTA juggling (GDMA, the karma churn we fixed).
   Bigger refactor — only if it keeps biting.
6. **Memory/doc consolidation.** Some entries stale (`project_usb_gadget_plan` refs old branch/
   paths; some "pending" notes are done). Periodic prune.

## Recommended order
Unit tests dropped. Finish #1 (validate untested — mostly done; flash + re-validate ws/pm
beacon fix, then bleinfo/i2cscan/espvoice). Then #2/#3 (maintainability), #4 (command cap,
forced soon). Do NOT prioritize new attacks — prove + harden what exists.

Related: [[next_steps]] (new features, separate), [[project_karma_rogue_handshake]] (karma-specific
open items: external .cap verify + long soak test).


