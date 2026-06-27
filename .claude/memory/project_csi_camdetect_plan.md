---
name: csidetect + camdetect plan (CSI presence + hidden-camera scan)
description: csidetect/csi BUILT + HW-VERIFIED — pro WiFi motion detector [EXP] (sprite+8 bands, honest no-bearing) 2026-06-26; camdetect DROPPED. Next = SD log/alert.
type: project
---

**Commands: `csidetect`/`csi` (BUILT) + ~~camdetect~~ (dropped).** Full original plan in repo:
`.claude/memory/TREX_CSI_CAMDETECT_PLAN.md`. ESP32-S3 built-in WiFi only — no extra sensors. Ref:
skizzophrenic/Cardputer-CSI-Human-Detector (**MIT**) + official espressif/esp-csi (Apache-2.0).

## IMPLEMENTATION STATUS (2026-06-26)
- **`csidetect`/`csi` — BUILT + HW-VERIFIED, now a PRO "WiFi motion detector" `[EXP]`.** Alias `csi`
  (NOT `hd`/`cd` — `cd` is change-dir). Category **Diagnostics**. Cmd count 58→**59/64**. **Reframed
  honestly: NOT a radar** (single antenna = motion energy only, no bearing) — in-app `[CSI::MOTION]`,
  user text says "sweep-style display, not an actual radar". Tagged [EXPERIMENTAL] (env-dependent).
- **`camdetect` NOT built — DROPPED from this effort**: it's not CSI at all, just a promiscuous
  camera-OUI sniffer that shared the plan doc. Revisit separately if wanted; same for the trackme
  cam-OUI integration.
- **Files**: `t-rex-firmware/wifi/sensing/csidetect.cpp` + `.h`; fwd-decl + `registerCommand` in
  `core/cli/command_manager.cpp`; `-I .../wifi/sensing` in `platformio.ini`; credited in `NOTICES`
  (#12 Cardputer-CSI MIT, #13 esp-csi). File-header `Sources:` block present.
- **What the MVP has**: CSI enable (promiscuous MGMT|DATA + `esp_wifi_set_csi`); IRAM `csiCb` with the
  reference's EXACT math — per-subcarrier amplitude `sqrt(r²+im²)` + mean sin(phase), 50-frame window,
  amp+phase variance, **asymmetric-EMA** self-cal (rates 0.1/0.002/0.005), blend `0.6·amp+0.4·pha`;
  hold/coast presence (thresh **0.15**, hold 150 @ ~15 Hz, graceful fade); **honest phosphor radar**
  (sweep angle = time, blip radius = motion intensity, **NO bearing**); `[`/`]` threshold, `c`
  calibrate, `q` quit; on-screen bring-up diag line (`fr:<n>` + `CSI live|CSI ERR n|no frames`).
- **DONE in the pro pass (2026-06-26)**: sprite double-buffer (PSRAM `LGFX_Sprite`, ~30fps, sweep
  cone, pulsing reticle); per-subcarrier 8-band sectors (gated behind global motion + above-average
  highlight; strong→centre); snappy ~1.2s coast; activity word; `h` help overlay; clears on exit;
  full docs (man/README/diagnostics.md/CLAUDE.md); [EXPERIMENTAL] tag; honest reframe (not a radar).
- **What it STILL does NOT have — next phases**:
  1. **SD presence log + alert** → `/apps/csidetect/` (use `ScopedPromiscPause`, GDMA). Turns it from
     a toy into an unattended recon tool ("motion at 14:32"). Highest value next.
  2. **Field-tune the sectors** — confirm whether moving in different spots lights different sectors
     consistently in real rooms; if not, the sectors are honest decoration only.
  3. Coexistence: currently **requires WiFi connected** (`WL_CONNECTED`) and **bails if `wg bg`**
     owns promiscuous. Make it save/restore the promisc rx cb to run alongside.
- **Gotchas for the next PC**:
  - CSI needs WiFi **connected** (frames come from the AP's channel) — `cw` first; it stays on that channel.
  - `wifi_csi_config_t` uses **IDF-4.4 field names** (lltf_en/htltf_en/stbc_htltf2_en/ltf_merge_en/
    channel_filter_en/manu_scale/shift) — correct for `platform = espressif32` 6.x. A core bump to
    IDF 5.2+ renames the struct → guard then.
  - Detection scales with ambient WiFi traffic; quiet net → low `fr:` → sluggish. Self-ping gateway
    is an optional future nicety (the reference also relies on ambient traffic).
  - **Single antenna = ONE motion signal, no direction/count.** The radar is deliberately honest
    (sweep=time, not bearing). Do NOT add fake per-human positions — the reference fakes blip angles
    too and is explicit it's "motion-over-time, not direction finding."
  - The reference's "Monster C5" is just an OPTIONAL external display, NOT a sensor — sensing is 100%
    on one S3 (this is the `enableCsi`/`csiCallback` path in its `src/main.cpp`).

## ⚠️ ALIAS COLLISION — fix before building
Plan proposes `csidetect`/**`cd`** but **`cd` is TAKEN** (change-directory, SD). Use a free alias:
**`hd`** (human-detect) or **`csi`**. `camdetect`/`cm` is free. (`ns`/`is`/`w3d` for the other plans
are free too.) Verified against `setupCommands()` 2026-06-26.

## csidetect (`hd`/`csi`) — WiFi CSI human presence detector
Reads Channel State Information from received WiFi frames to sense body motion — no camera/radar.
Requires WiFi **connected** (more nearby traffic = more frames = better). 2.4GHz any channel.
- APIs: `esp_wifi_set_csi_config()`, `esp_wifi_set_csi_rx_cb()` (**IRAM_ATTR**), `esp_wifi_set_csi(true)`,
  promiscuous on.
- **Callback** (per frame): I/Q → amplitude `sqrt(r²+im²)`/subcarrier, mean amp, mean sin(phase)=im/amp;
  push to 50-frame sliding window; amp variance + phase variance; normalize via **asymmetric EMA**
  (floor: fast-decay 0.1 below / slow 0.002 above; max: slow 0.005); `motionScore = 0.6*amp + 0.4*phase`.
- **Main loop ~15Hz hold/coast**: score>thresh → hold 150 ticks (10s) then fade gracefully (no snap-off).
  Default threshold 0.35, `[`/`]` adjust, `[c]` calibrate/re-baseline. Presence box + motion bar +
  100-sample scrolling graph (trackme score-history style). Speaker: beep on presence / low beep on clear.
- Use case: leave it running as a motion sensor during a physical pentest.

## camdetect (`cm`) — hidden camera / spy-device scanner
Promiscuous sniff (no connection needed), channel-hop 1-13 (dwell 2s on a hit), match source-MAC OUI
against a camera/IoT vendor table → Wyze/Ring/Hikvision/Dahua/Nest/Arlo/Blink/Reolink/Amcrest/D-Link/
TP-Link + ESP32 DIY (Espressif OUIs). RSSI→distance + alert levels: >-65 ALERT(red, same room),
-65..-75 WARNING, <-75 NOTICE. Speaker beeps scale with proximity. `[c]` clear `[s]` save `[h]` hop on/off.

## trackme integration (bonus)
Add the camera-OUI match into trackme's existing WiFi probe sniff → tag matches as a `CAM` device at
THREAT_WARNING min. Then `tm` finds BLE trackers + following devices + nearby cameras in one command.

## Reuse map / current-repo mapping (plan paths are foreign — DO NOT follow verbatim)
- Plan: `shell.cpp`, `sensing/csidetect|camdetect|csi_common`. Current: register in
  `core/cli/command_manager.cpp` `setupCommands()`; modules under a new `sensing/` (or `wifi/sensing/`).
  SD → `/apps/csidetect/`, `/apps/camdetect/` (v2 layout, NOT `/logs/`).
- **Reuse `oui_lookup.h`** (already ~350 entries incl. Espressif/cameras) — extend it rather than ship
  a private `kCamOuis[]`; ouiLookup already returns `{vendor,type}`. Reuse promisc hop + IRAM_ATTR
  sniff (trackme/wifimon), trackme's motion-history graph idiom.
- **GDMA rule**: promisc + SD log → `ScopedPromiscPause` per write (same class as the macwatch fix).
- Radio conflict: both are promisc foreground cmds — abort if a conflicting WiFi mode is up; csidetect
  needs CSI+promisc, camdetect needs promisc hop. Disable CSI + promisc + restore WiFi on exit.

## Cmd-cap note
Adds 2 commands. With wpa3down(+1) + netspy/isoscan(+2) all five plans = +5 → 58→**63/64**. Tight;
may force more merges first (see [[project_improvement_backlog]] #4).

Coding standards (plan): no dynamic alloc / no STL / fixed buffers / IRAM_ATTR callbacks / dm. + inputHandler.
