# WGuard WiFi IDS

## Status
Implemented, compiled, field-tested pending.

## Detection Types (10)
1. BCAST DEAUTH — broadcast deauth burst (5/5s)
2. DEAUTH Storm — targeted deauth burst (15/5s)
3. EVIL TWIN — two-tier: INFO (foreign beacon) → WARNING (+ deauth)
4. HANDSHAKE Harvest — deauth burst followed by EAPOL M1+M2
5. BSSID CLONE — BSS timestamp backward jump (0xFD)
6. BEACON FLOOD — 100+ unique BSSIDs/30s
7. AUTH Flood — 32+ unique MACs authing/10s
8. PROBE Storm — 50+ probes/5s from same MAC
9. PMKID Grab — 5+ rapid assoc requests/5s
10. KARMA Attack — same BSSID answers 3+ different SSIDs/60s

## Clone Detection — Multi-Signal Logic (updated 2026-06-02)
Clone confirmation now requires 3 signals to reach CRITICAL:
- `0xFD` (BSS ts-jump) → WARNING + start watch window
- `0xFA` (BCN interval compression) alone → stays WARNING
  - With deauth seen in window → CRITICAL "CLONE CONFIRMED (deauth+ts-jump+bcn-compress)"
  - Without deauth → WARNING "BCN COMPRESS+TS-JUMP (no deauth, possible enterprise AP)"
- `0xF9` (clock skew) → INFO only, never upgrades (beatable + reboot ambiguity)

Reason: enterprise WiFi (Fortinet, Cisco mesh) with multiple APs on same BSSID
produces ts-jump + beacon compression legitimately but NEVER deauths its own clients.

New state: `_cloneDeauthSeen` bool + `_cloneDeauthTs` timestamp in WGuard class.
Set when `_deauthBurstCount` hits 3 AND `_cloneWarnActive` is true.
Reset in both `startSession()` and `startBg()`.

## Modes
- Interactive: `wg <idx|bssid> [ch]`
- Background: `wg <idx|bssid> [ch] bg` — shield icon in status bar
- View bg: `wg view`
- Stop bg: `wg stop`

## Session Logs
`/apps/wguard/NNN.csv` — never overwritten, new number each session.
Save types: AUTO-SAVE (ring full) / CHECKPOINT (2min) / MANUAL ([s]) / FINAL (quit).

## Known Bugs Fixed
- `0xF9` false-positive clone upgrade (reboot ambiguity) — does not upgrade
- `_lastBgHead` stale popup after auto-save — reset to 0 after doAutoSave()
- Enterprise false-positive CRITICAL — now requires deauth as 3rd signal (2026-06-02)
