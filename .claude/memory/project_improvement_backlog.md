---
name: Project improvement backlog (hardening, not features)
description: Prioritized project-wide improvements — testing/correctness, maintainability, structural. NOT new attacks. User will consider these.
type: project
---

# Project-wide improvement backlog (2026-06-15)

Assessment of the whole T-REX firmware (~60 commands, mature). Leverage now is in
**hardening + maintainability**, NOT more features. User asked to save these to consider.
(New attacks live in [[next_steps]] — deliberately NOT prioritized here.)

## Tier 1 — highest value (prove it works)
1. **Unit tests for the pure logic.** Hand-rolled crypto/parsers where a silent bug = wrong
   results: `wpa_crack` (PBKDF2 / 4-way MIC / PMKID), `dot11` (EAPOL byte-offsets), `pcap`
   reader/writer, `oui_lookup`. All pure + host-testable → add a `pio test` native env with
   known vectors (a real handshake, a PMKID, known passwords) and wire it into the existing
   compile-gate CI. We already hit one EAPOL offset bug this project — exactly what this catches.
   **Best long-term investment.**
2. **Close the "compiles ≠ works" gap.** Built-but-never-field-tested: `bleinfo`, `i2cscan`
   (experimental), `capcrack` external-`.cap` path (open a karma .cap in aircrack/hashcat on a
   PC — still unverified), `espvoice` private mode. A focused validation pass.

## Tier 2 — maintainability
3. **Finish shared-util extraction.** Frame injection is still copy-pasted across karma,
   beacon_flood, eviltwin, deauth, wifimon, hidden_ssid (each hand-rolls 802.11 builders +
   promiscuous start/stop/hop). Plan names the targets: `dot11_tx.h` (beacon/deauth/probe
   builders) + `promisc.h` (start/stop/hop). Kills the last big dup, centralizes the trickiest code.
4. **Migrate remaining modules to `ScopedPromiscPause`.** Only eviltwin + karma use the GDMA
   guard; wguard/wifimon/handshake/pmkid still hand-roll pause/resume — where GDMA corruption hides.

## Tier 3 — structural / housekeeping
5. **64-command cap: at 59/64.** Next few features hit the wall. Decide now: raise the cap, or
   move to sub-commands/categories (already done for `km auto`, `mc target`…). Will bite soon.
6. **Centralized WiFi state helper** ("enter sniff / enter inject / return idle"). Most hard bugs
   lived in per-module `WiFi.mode`/promiscuous/APSTA juggling (GDMA, the karma churn we fixed).
   Bigger refactor — only if it keeps biting.
7. **Memory/doc consolidation.** Some entries stale (`project_usb_gadget_plan` refs old branch/
   paths; some "pending" notes are done). Periodic prune.

## Recommended order
#1 (unit tests) + #2 (validate untested) first — convert "I think it works" → "I know it works"
(matters most for a security tool). Then #3/#4 (maintainability), #5 (command cap, forced soon).
Do NOT prioritize new attacks — prove + harden what exists.

Related: [[next_steps]] (new features, separate), [[project_karma_rogue_handshake]] (karma-specific
open items: external .cap verify + long soak test).
