---
name: wpa3down plan (WPA3 transition-mode downgrade)
description: NOT YET BUILT — wpa3down/w3d: detect WPA3 transition-mode APs + PMF, force WPA2 downgrade, capture handshake/PMKID
type: project
---

**Command: `wpa3down` / `w3d`** (WiFi). NOT YET BUILT. Full plan in repo:
`.claude/memory/TREX_WPA3_DOWNGRADE_PLAN.md`. Dragonblood CVE-2019-9494..9499 (Vanhoef/Ronen);
refs: TrustedSec Jul-2024 writeup, RedLegg Jun-2025 eaphammer, VSMtripathi GitHub.

## Attack in one line
Deauth victim off the real transition-mode AP → stand up a **WPA2-PSK-ONLY** rogue AP (same
SSID/channel, no SAE in AKM, PMF bits clear) → WPA3-capable victim falls back to WPA2 → capture
EAPOL handshake (+ PMKID from M1) → crack offline (hashcat -m 2500 / -m 22000).

## Part 1 — PMF + transition-mode detection (extends the `sw`/`scanwifi` path)
Parse the **RSN IE (element ID 0x30)** out of beacons in promiscuous (frame subtype 8):
- **RSN Capabilities (2B)**: bit7 MFPC (PMF capable), bit6 MFPR (PMF required).
  - MFPR=0,MFPC=0 → PMF off → deauth works `[OPEN]`
  - MFPR=0,MFPC=1 → PMF optional → test first `[PMF?]`
  - MFPR=1,MFPC=1 → PMF required → deauth blocked `[PMF!]`
- **AKM suite list**: `0x000FAC02`=WPA2-PSK, `0x000FAC08`=SAE(WPA3). Both present = transition
  mode `[TD]` (downgradeable); SAE only = pure WPA3 (not downgradeable); PSK only = pure WPA2.
- New scan columns/flags: `[TD] [PMF!] [PMF?] [OPEN] [WPA3]`.

## Part 2 — PMF probe (empirical, beats trusting flags)
Send 3 deauths to an associated client; if it re-associates within 3s → PMF not enforced → proceed.
No re-assoc → PMF enforced → fall back to **pre-association flooding** (inject deauths during the
auth→assoc window, before PMF activates — slower, works on PMF-required).

## Part 3-6 — rogue AP + capture
WPA2-only rogue AP (variant of evil twin that forces PSK-only AKM, PMF disabled). Capture EAPOL
(ethertype 0x888E, src==victim) → need M1+M2 min; extract **PMKID from M1** (no client needed).
Save PCAP + HCCAPX (mode 2500, 392-byte record struct in plan) + HC22000 (mode 22000).

## Reuse map in the CURRENT repo (plan's paths are from an older/foreign layout — DO NOT follow them)
- Plan says `shell.cpp`/`attacks/`/`scanwifi.cpp`/`wifi_tools.cpp`. Current repo:
  - Register command in `core/cli/command_manager.cpp` `setupCommands()` (one-liner, cap now 58/64).
  - **Reuse, don't reimplement**: `wifi/attacks/eviltwin/` (rogue AP), `wifi/.../handshake_capture.cpp`
    (`ws` — EAPOL M1/M2 sniff + HCCAPX-ish save + on-device crack), `wifi/.../pmkid_attack.cpp`
    (`pm` — PMKID from M1, already writes `.cap`), the `sw` scan beacon path for RSN-IE parsing,
    `oui_lookup.h`. Deauth infra already exists (`da`, wifimon directed deauth).
  - SD: follow v2 layout → `/apps/wpa3down/` (not `/logs/`), and the **GDMA rule** (open files before
    APSTA/promiscuous, write after teardown; or `ScopedPromiscPause`).
- Much of this is assembling existing `ws`+`pm`+`eviltwin`+`sw` pieces behind one guided flow + the
  new RSN-IE/PMF parser. The genuinely new code = RSN-IE MFPC/MFPR+AKM parse, WPA2-only AP config,
  pre-assoc flood, HCCAPX/HC22000 writers (if not already covered by ws/pm cap output).

## Limits (honest)
Works on transition mode w/ PMF optional/off. Does NOT work on pure WPA3 or correctly-implemented
PMF-required. No forward-secrecy break (past SAE traffic stays safe). Authorization disclaimer in plan.
