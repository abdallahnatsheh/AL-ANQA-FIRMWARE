---
name: wpa3down plan (WPA3 transition-mode downgrade)
description: NOT YET BUILT — wpa3down/w3d: detect WPA3 transition-mode APs + PMF, force WPA2 downgrade, capture handshake/PMKID
type: project
---

**Command: `wpa3down` / `w3d`** (WiFi). ✅ **PHASE 1 DONE (2026-07-08) + PHASE 2 BUILT & FIRST-HW-TESTED (2026-07-09) — attack works mechanically, CAPTURE blocked by client PMF on the test target. Phase 3/4 not built.**

## WEB-VERIFIED 2026-07-12 (no HW — research + static review)
Method is CORRECT and matches current working tooling — DragonShift (jabbaw0nky), VSMtripathi's tool, TrustedSec Jul-2024 (captured across Aruba/Ubiquiti/MikroTik/Meraki), RedLegg Jun-2025 (eaphammer). Point-by-point match: TD target (`WIFI_AUTH_WPA2_WPA3_PSK`→`WSEC_TD`) · WPA2-PSK-only rogue same SSID/ch · deauth · forge M1 (unauth) → sniff victim M2 (auth) → crackable half-handshake → hashcat -m 22000. The M1+M2 crack is sound (all inputs known: SSID for PBKDF2, our rogue BSSID, our ANonce, victim SNonce+MIC) and roguehs is ALREADY HW-verified end-to-end in karma — so the capture engine is proven, w3d just adds TD-targeting+deauth. **Nothing in the code blocks a capture; success is target-gated.**
**Two silent-failure causes (both client-side, undetectable by us):** (1) PMF-required → deauth rejected (the 2026-07-09 test wall). (2) **WPA3 Transition Disable** — Wi-Fi Alliance, mandatory in WPA3-certified clients since Dec-2020: real AP signals a protected KDE in its 4-way → client stores "WPA3-only" in its saved profile → refuses our WPA2 rogue for that SSID. BUT enforcement is inconsistent in practice (cyber-fi.net testing: iPhone/Android/Win11 all still downgraded) → POSSIBLE not certain blocker. **Added notes for it 2026-07-12**: file SCOPE comment, header warn ("PMF/transition-disable may block drop"), and the STOPPED screen now enumerates PMF / transition-disable / no-WPA2-client as likely causes. Sources in progress_log.

## HW-TEST FINDINGS (2026-07-09) — read before resuming
**Result: `w3d` is mechanically functional (rogue WPA2 AP beacons, victim probes it once deauth lands), but did NOT capture on the test target because the victim client uses PMF. A WPA3-transition client with PMF active is NOT downgradeable by deauth — by design.** This is the honest documented limit, not a bug.

**Test rig (for repeating):** target = an Android **phone hotspot** `TESTNET`, **WPA2/WPA3 transition, ch6 (2.4GHz)**; victim = the dev's **Linux laptop** (Intel `iwlwifi`, iface `wlp0s20f3`, MAC **`xx:xx:xx:xx:xx:xx`**). The laptop is the machine Claude runs on → victim state is directly inspectable (`iw dev wlp0s20f3 link`, `nmcli`, `journalctl`).

**Key facts learned:**
- AP RSN = `PSK SAE` (transition), **`MFP-capable (0x008c)` → MFPC=1, MFPR=0 = PMF OPTIONAL (not required).** So a PMF-OFF client *can* join.
- The laptop connects **WPA2-PSK, not SAE** (journal: `associating→4way_handshake`, no SAE step) — BUT with `pmf=default` against an MFP-capable AP, **modern wpa_supplicant negotiates PMF anyway** → forged deauths (broadcast AND directed) are rejected → client never drops. That's the wall.
- **Phone-hotspot BSSID changes ONLY on hotspot off/on (stable while up).** Saw 4 different LA-MAC BSSIDs across toggles. → re-`sw` right before `w3d`, don't toggle after; live-BSSID tracking is NOT needed for this target.
- Real AP was **−32…−39 dBm (phone on top of the laptop), un-movable** → even when the victim probes the rogue it re-picks the stronger real AP. One PMF-off-ish run = 63 probes/600s (background rate), 0 assoc.
- Forcing client PMF off via `nmcli ... pmf 1` **broke the NM profile** (lost secrets → jumped to SSID `TESTNET2`); fragile, couldn't get a clean PMF-off capture. Restore TESTNET: `nmcli device wifi connect TESTNET password <pw>`.

**Code changes made this session (UNCOMMITTED, capture NOT yet verified):**
- **Directed deauth**: `w3d [idx] [victim-mac]` → `w3dDeauthDir` (AP→STA + STA→AP pair) + broadcast. Directed is the effective form (iwlwifi ignores broadcast deauth). Chrome shows the deauth target.
- **Continuous deauth flood**: interval 500ms → **20ms (~50/s)** to beat a strong un-movable AP.
- Compile fix: added `extern SDCardManager sdCardManager;`.

**▶ RESUME PLAN (priority order):**
1. **Validate the pipeline with a NON-PMF victim** — older phone / IoT / ESP32 / older Android join a transition hotspot over plain WPA2 without PMF. `w3d <idx> <its-MAC>` → continuous deauth drops it → downgrade → M2 → `.cap`. (A modern PMF laptop is the WRONG victim to prove capture.)
2. Tune the **20ms flood** if a real test shows **Asc rising but no M2** (flood starving the rogue's responses) → add "pause deauth while victim is associating to OUR rogue"; else maybe ~50ms (20/s).
3. **Phase 3 (PMF probe + pre-assoc flood)** — NOTE: pre-assoc flood does NOT rescue an already-connected PMF client (can't force re-assoc without a drop PMF blocks); only helps NEW associations. Lower value than #1.
4. **Commit** the whole batch (sw manager + wpa3down + Phase-1 detection) once capture is HW-proven on a non-PMF client.

---

**Command: `wpa3down` / `w3d`** — original build note:

**PHASE 2 (2026-07-09) — the core downgrade attack, code-complete + static-reviewed, UNCOMMITTED, NOT HW-tested.**
New module `wifi/attacks/wpa3down/wpa3down.{cpp,h}`, free fn `runWpa3Down(char*)`, registered `wpa3down`/`w3d` [EXP]
WiFi (`stopEspchatBg()` prefix). **Almost pure ORCHESTRATION (rule 5b) of karma's `roguehs` engine** — its beacon is
ALREADY WPA2-PSK-only (RSN AKM `0x000FAC02`, no SAE = exactly the downgrade rogue AP), it injects its own M1 (known
ANonce) + sniffs the victim's M2 → crackable half-handshake, and keeps beacon+M1+M2 raw frames. Flow: require prior
`sw` scan → TD-filtered picker (`getNetworkSec==WSEC_TD`, trackball/Enter) or `w3d <idx>` → `roguehs::begin(ssid,ch)`
(WPA2 rogue on the target's channel) → loop: `roguehs::poll()` + broadcast-deauth the REAL AP every ~800ms (26-byte
frame, src=real BSSID, `WIFI_IF_STA`, same channel/no hop — copied from karma's `injectDeauth`) + live Prb/Ath/Asc/M2
counters (flicker-free: chrome-once + stats-in-place) → on `gotM2`: `roguehs::end()` (idle STA, GDMA-safe) THEN save
`/apps/wpa3down/<ssid>.cap` (beacon+M1+M2, `pcap::writeRecord` lt105, never-overwrite) → crack with `cc`. Wiring:
`SD_DIR_WPA3DOWN` + ensureDir + apps-README; platformio `-I .../wpa3down`; man/README/CLAUDE/NOTICES(#19 Dragonblood).
**HONEST SCOPE:** works on transition APs w/ PMF off/optional; **PMF-required blocks the deauth (victim won't drop)** —
empirical PMF probe + pre-assoc flood = Phase 3, NOT built (in-UI "PMF? may not drop" note). Output = `.cap` (crackable
via cc/hashcat), NOT a separate HCCAPX writer. `roguehs`'s own caveat applies: `esp_wifi_80211_tx` doesn't HW-ACK, so
association depends on the client tolerating missing ACKs (HW-proven in karma). **NEXT = flash + HW-test on a real
transition-mode AP; then Phase 3 (PMF probe + pre-assoc flood), Phase 4 (wguard downgrade-detect).**

---
**PHASE 1 (2026-07-08, HW-test pending) — detection:**
Phase 1 = Part-1 detection, done the EASY way (no raw RSN-IE parse needed): the ESP32 scan record
`wifi_ap_record_t.authmode` already gives transition mode directly. In `wifi/core/wifi_functions.{h,cpp}`:
`NetworkEntry.sec` + `enum WifiSec{OPEN,WEP,WPA,WPA2,WPA3,TD}`, `classifySec(authmode,isOpen)`
(`WIFI_AUTH_WPA2_WPA3_PSK`→`WSEC_TD`), set in `populateScanCache` (rec already read for wps), shown in
`renderScanPage` as tags (TD=yellow target, WPA3=green, OPEN=magenta, WEP=red) — so `sw`/`show` now flag
downgradeable APs. **`getNetworkSec(int)` getter exposed for Phase 2 to pick targets.** No transmit, one file.
Docs: man `scanwifi` SEC block + docs/wifi-scan.md security table. **NEXT = Phase 2 (attack: PMF probe →
WPA2-only rogue AP via eviltwin → capture via ws/pm → /apps/wpa3down/), Phase 3 pre-assoc flood, Phase 4
wguard downgrade-detection (defensive).**

Full plan in repo:
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
