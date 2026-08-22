# AL-ANQA `spydetect` — Hidden Spy-Device Detector (WiFi + BLE)

**Status:** 📋 **PLANNED — not built.** Design captured 2026-08-18. Greenfield command
`spydetect`/`spy` (Category: Diagnostics or a new "Counter-surveillance"). Detects hidden
surveillance devices (WiFi cameras, BLE trackers, drones, ALPR/sensor nodes) using TWO
complementary detection axes. The second axis (motion-correlation) is the genuinely-novel,
first-on-ESP32 part.

## 1. Why — the two-axis idea

Every existing ESP32 spy detector (OUI-SPY, Eye Spy, nyanBOX, Sneak32, Flock-You) is a
pure **signature matcher**: fingerprint a device by MAC OUI / BLE service-UUID / device
name / manufacturer data. That is table-stakes and fails on (a) randomized MACs, (b)
unknown brands, and it can never confirm a device *is* a camera or that it is *pointed at
the operator*.

AL-ANQA adds a second, behavioral axis no ESP32 tool has: **motion-correlated
traffic analysis** — confirm a device is a live camera watching you, regardless of brand
or MAC, by correlating its encrypted WiFi bitrate with induced motion.

The two axes cover each other's blind spots:
- **Signature** answers *"what brand/type is this device?"* (fast, passive, but brand-limited).
- **Motion-correlation** answers *"is this thing a camera aimed at me RIGHT NOW?"*
  (brand-agnostic, defeats MAC randomization, but needs a live over-air stream).

## 2. Axis A — signature matcher (the boring, necessary half)

Passive WiFi + BLE sniff, fingerprint against a device-signature list. ~80% reuse of
existing modules:
- **Reuse `trackme`'s two-mode signature engine** (`TrackerSig`: manufacturer-data match
  AND 16-bit service-UUID match) — already HW-verified against seemoo-lab/AirGuard. Just
  extend the signature set from *trackers* to *cameras + sensors*.
- **Reuse `oui_lookup.h`** (~350-entry vendor/type table) for WiFi camera-brand OUIs.
- **Reuse `wm`/`bmon` sniff paths** (promiscuous WiFi frame accounting; NimBLE passive
  adv scan + decode).
- **New signature CSV** `/apps/spydetect/signatures.csv` — camera-brand OUI prefixes
  (Ring/Blink/Nest/Arlo/Wyze/Reolink/Eufy/Hikvision/Dahua/Axis/…) + BLE camera/sensor
  UUIDs + device-name substrings ("IPC", "GoPro", camera SSID patterns). Built-in list +
  SD merge (same pattern as `trackme` signatures: built-ins always load, SD appended).
- Output: live table (WiFi cams / BLE devices), color-coded by confidence, `[s]` save to
  `/apps/spydetect/NNN.csv`.

## 3. Axis B — motion-correlated camera detection (the novel first-on-ESP32 part)  [EXP]

### 3.1 The physics it exploits
WiFi cameras stream **variable-bitrate (VBR) H.264/H.265**. The encoder transmits only
what changed between frames:
- Still scene → tiny delta frames → low bitrate (~50–200 KB/s).
- Motion in the field of view → many pixels change → large delta frames → bitrate spikes
  (~1–4 MB/s). Many cameras also do motion-triggered recording (near-idle → burst).

So a camera's uplink data-rate is effectively a live readout of how much motion its lens
sees. That is the exploit.

### 3.2 Measuring bitrate WITHOUT decryption
Promiscuous mode exposes every data frame's **length + timestamp + MAC addresses** in
cleartext even on WPA2 (content stays encrypted — we don't need it). Per suspect MAC:
```
for each sniffed frame from/to <suspect MAC>:
    bytes_this_second += frame.length
→ one bitrate sample per second for that device
```
This is the same per-MAC frame accounting `wm` already does.

### 3.3 The motion "ground truth" — two sources (AL-ANQA can do both)
- **Operator-cued (simple, v1):** the T-Deck drives a rhythm — "MOVE (3s)… STILL (3s)…"
  repeated ~4–6 cycles (Snoopdog's "S5" stop-start motion). The firmware knows exactly
  which seconds are "move" windows because it set the schedule.
- **CSI-sensed (the unique bit, v2):** reuse `csidetect`'s per-second WiFi-CSI motion-energy
  signal so the deck *measures* motion itself instead of trusting the operator. This is the
  capability no other ESP32 detector has.

### 3.4 The correlation = the detection
Two time series over ~20–35 s:
```
motion(t):   0 0 0 1 1 1 0 0 0 1 1 1 0 0 0
bitrate(t): .2 .2 .2 1.9 2.1 1.8 .3 .2 .2 2.0 1.7 1.9 .2 .2 .2   (MB/s)
```
Compute correlation (v1: **Pearson**; rigorous later: **Granger causality** as in Snoopdog).
A device whose bitrate reliably rises in "move" windows and falls in "still" windows → high
correlation → **camera pointed at the operator**. Lamps/plugs/phones/thermostats have
traffic uncorrelated with the operator's motion → score ≈ 0 → cleared.

**One-line intuition:** a real camera watching you is the one device in the room whose
data-rate dances to your movement.

## 4. Prior art (this is proven research, not our theory)

Peer-reviewed, top-venue, independently reproduced:
- **Snoopdog** (USENIX Security 2021) — canonical; IMU-motion vs WiFi traffic via Granger
  causality; S5 motion; detect + localize.
- **DeWiCam** — Android; ML classifier on 802.11 video-stream traffic pattern.
- **SCamF (Spy Camera Finder)** — reconstructs encoded frame sizes from encrypted traffic;
  **0.98 classification accuracy, 0.97 TPR.**
- **Lumos / LocCams / CamLoPA** (2024–2025, incl. IEEE S&P) — detect + localize via the
  same traffic-vs-motion principle.

All proven on phones/laptops with a full WiFi NIC. **No ESP32 implementation exists** —
AL-ANQA would be first (the upside and the risk).

## 5. ESP32-specific risk (why Axis B is [EXP] until HW-tested)

- **Frame drops under load.** A 2–4 MB/s camera is thousands of frames/s; ESP32
  promiscuous drops frames under load (already tracked as `s_pcapDropped` in `wm`/`wifimon`).
  **Why it likely survives:** we need only the per-second byte *total* and a *relative*
  rise/fall correlated with motion. Drops are ~proportional to load, so the correlation
  signal is preserved even with lossy capture — the most forgiving thing you can ask a
  lossy sniffer to do. But "likely" is honest until measured on the board.
- **CBR cameras** don't modulate bitrate with motion → Axis B misses them → fall back to
  Axis A signature matching.
- **Granger causality is heavy for an MCU** → ship Pearson move/still correlation in v1;
  Granger is a later refinement.
- Needs a clean move/still contrast + a few cycles; busy RF or a camera also seeing other
  motion adds noise → repeat the cycle.

## 6. Honest limits (must be on-screen, house style)

- Axis B **only works if the camera is live-streaming over air we can hear** — offline
  SD-recording, 4G/LTE, or an out-of-range network → invisible.
- OUI fingerprinting (Axis A) is defeated by MAC randomization (partly mitigated by
  name/UUID/mfr-data heuristics + PNL fingerprinting à la `karma`).
- The tool says **"a camera is watching,"** NOT **"the room is clean."** A true sweep also
  wants an RF power meter / lens-glint detector (out of scope for WiFi/BT alone). Pairs well
  with backlog **#30 `locate`/`loc`** (RSSI-homing tone) as the "walk toward it" companion.
- Label `[EXP]` on-screen + in the man page.

## 7. Build phases (PoC-first — validate the hypothesis cheaply)

**Phase 0 — PoC / hypothesis test (do this FIRST, before any UI):**
minimal path — pick a suspect MAC, show a live per-MAC byte-rate meter + a manual
move/still cue + a live Pearson correlation number. Point it at ONE real WiFi camera on
HW and confirm the correlation actually rises during motion. This answers "does it work on
*our* hardware" for certain before investing in the full command. GDMA-safe: no SD writes
while promiscuous (buffer in RAM, or `ScopedPromiscPause`).

**Phase 1 — Axis A signature matcher:** extend `trackme`'s engine + `oui_lookup` + camera
signature CSV; live table + SD save.

**Phase 2 — Axis B operator-cued motion-correlation** wired into the same command
(select a device → run the move/still correlation → verdict).

**Phase 3 — Axis B CSI-sensed** (auto-motion via `csidetect`), so the operator doesn't have
to move on cue. The differentiator.

**Phase 4 (optional) — GPS-mapped survey** (Plus): log camera-signature devices with GPS →
WiGLE-style surveillance map. Reuses `wardrive` + `oui_lookup`.

## 8. Reuse map (rule 5b — orchestrate, don't reimplement)

| Need | Reuse |
|------|-------|
| BLE + mfr/UUID signature match | `trackme` `TrackerSig`/`matchSig` engine |
| WiFi/BLE vendor+type lookup | `oui_lookup.h` |
| Per-MAC WiFi frame accounting | `wm` promiscuous frame path |
| BLE passive adv decode | `bmon` decode path |
| Per-second motion energy (Phase 3) | `csidetect` CSI motion signal |
| GDMA-safe SD writes under promiscuous | `ScopedPromiscPause` |
| GPS geotag (Phase 4) | `GpsManager` (read-only if task already running) |

## 9. SD layout
`/apps/spydetect/signatures.csv` (built-in + SD merge) · `/apps/spydetect/NNN.csv`
(saved detections, sequential never-overwrite). `SD_DIR_SPYDETECT=/apps/spydetect`.

## 10. Cross-refs
Companion: backlog #30 `locate`/`loc` (physical homing). Related dropped idea:
`camdetect` (pure OUI sniffer — this plan supersedes it with the two-axis approach).
NOTICES: credit Snoopdog / DeWiCam / SCamF / CamLoPA (methodology, no code copied).
