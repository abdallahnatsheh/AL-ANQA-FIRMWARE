---
name: csidetect + camdetect plan (CSI presence + hidden-camera scan)
description: NOT YET BUILT — csidetect (WiFi CSI human presence) + camdetect/cm (hidden camera OUI scanner). cd alias COLLIDES.
type: project
---

**Commands: `csidetect` + `camdetect`/`cm`** (new Sensing category). NOT YET BUILT. Full plan in
repo: `.claude/memory/TREX_CSI_CAMDETECT_PLAN.md`. Both use the ESP32-S3's built-in WiFi only — no
extra sensors. Ref: skizzophrenic/Cardputer-CSI-Human-Detector (**MIT**, attribution required in
adapted files) + official espressif/esp-csi (Apache-2.0, has S3 human-detect examples).

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
