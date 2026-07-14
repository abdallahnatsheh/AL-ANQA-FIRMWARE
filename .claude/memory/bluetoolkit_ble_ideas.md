---
name: bluetoolkit-ble-ideas
description: BLE attack ideas inspired by sgxgsx/BlueToolkit that are actually implementable on the T-Deck (ESP32-S3, BLE-only, NimBLE) — IDEAS ONLY, no code (license clash)
metadata:
  type: reference
---

Researched **sgxgsx/BlueToolkit** 2026-07-12 (a Linux PC *orchestrator* framework, 43 public BT exploits,
Python/YAML, CC BY-NC-SA 4.0). **License is CC BY-NC-SA → incompatible with T-Rex AGPL-3.0: use IDEAS ONLY,
never its code.** Most of it does NOT apply here — captured so "port BlueToolkit" doesn't resurface.

## Hardware/stack reality that filters everything
- **ESP32-S3 = BLE-only, NO Bluetooth Classic (BR/EDR).** The whole T-Rex BT stack is NimBLE/BLE (sbl, bmon,
  trackme, ble_keyboard, bs, fp) — no Classic anywhere.
- **NimBLE exposes: GAP (adv/scan), GATT client (read/write/subscribe any characteristic), partial SMP.**
- **NimBLE does NOT expose raw link-layer (LL) PDU crafting** (unlike raw 802.11 via esp_wifi_80211_tx).

## ❌ NOT implementable (don't attempt — capture so it's settled)
- **All Bluetooth Classic attacks** — Braktooth, BIAS, KNOB(classic), BLUFFS, BLUR, BleedingTooth → no BR/EDR radio.
- **SweynTooth LL fuzzing (the bulk)** — needs raw link-layer packet crafting NimBLE can't do.
- **BLE-KNOB / short-key (7-byte) negotiation** — LOW/uncertain; NimBLE likely doesn't expose key-size forcing.

## ✅ Implementable ideas (inspired-by, build fresh)
1. **`gattfuzz` — GATT characteristic fuzzer/abuse** (from SweynTooth's ATT-layer subset + general GATT abuse).
   Connect → hammer every char: oversized/malformed/boundary writes, invalid/out-of-range handles, unreadable-handle
   reads, rapid write floods. **Key finding value = which reads/writes the device accepts WITHOUT pairing (broken
   BLE access control).** **Feasibility HIGH.** Reuses `bleinfo`/`bi` (already connects + enumerates + replays GATT).
   Already backlog #35. The standout — the "offensive `bleinfo`".
2. **`bleaudit` — BLE security auditor** (from BlueToolkit's Recon→Report methodology). Read-only: profile a target's
   security posture — requires pairing? encryption? MITM protection (Just Works vs passkey)? bonding? which chars are
   read/write without auth → one report. **Feasibility HIGH**, low risk, defensive/audit value.
3. **BLE DoS — connection-slot exhaustion + GATT write flood** (from the DoS V1–V16 family). Open many rapid
   connections to exhaust a peripheral's slots; or flood a char to hang it. **Feasibility MEDIUM** (device-dependent).
4. **Pairing-security *detector*** (from NINO / Method Confusion / SSP downgrade). Flag devices that accept Just Works
   / connect without encryption/bonding. **Feasibility MEDIUM as a detector**; active downgrade uncertain (SMP control).

## Bottom line
Entire usable yield from BlueToolkit = **`gattfuzz` (offensive) + `bleaudit` (defensive)**, both on `bleinfo`'s
GATT client, plus optional BLE-DoS. Everything else blocked by no-Classic-radio or no-raw-LL. See [[next_steps]] #35
(gattfuzz) and [[project_bleinfo]] (the GATT client to reuse).

## STATUS UPDATE 2026-07-14 — both folded INTO `bleinfo` (no new commands, per user)
- **`bleaudit` → DONE as `bi`'s `[b]` audit.** By user decision, NOT a separate command. Extended `runAudit()` in
  `ble_info.cpp` with a **security-posture** block on top of the existing value-leak scan: link encrypted? (from
  `client->getConnInfo()` → `isEncrypted/isAuthenticated/isBonded/getSecKeySize`), Just Works vs MITM, bonded, and
  **counts of chars readable/writable WITHOUT encryption** (`s_openReads`/`s_openWrites`, tallied in `enumerate()`
  since `bi` connects unpaired by default → any data returned over an OPEN link = broken access control). `[b]` is
  now **always available** (was gated on `s_hasRisk`); posture also written to the `/apps/bleinfo/<mac>.txt` report.
  UNCOMMITTED, not HW-tested. NimBLEConnInfo API verified present in pinned NimBLE-Arduino v2.x.
- **`gattfuzz` → already existed** as `bi`'s `[f]` fuzz (seq/random/boundary writes to writable chars) + `[w]` write.
  So the offensive GATT-abuse idea is largely covered by `bi` too; a dedicated `gattfuzz` command is redundant unless
  the user wants unpaired-read-hammering / handle-scan beyond what `[f]` does.
- Net: the usable BlueToolkit yield is now **inside `bi`**. Only BLE-DoS (connection-slot exhaustion) remains unbuilt.
