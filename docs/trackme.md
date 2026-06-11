---
title: Tracking Detection
parent: Bluetooth
nav_order: 3
---

# Tracking Detection (`trackme`) — Full Guide

> **⚠️ Experimental:** `trackme` is a best-effort tool and may produce false positives. Radio signals alone cannot prove physical tracking — use results as a general indicator, not as conclusive evidence. GPS movement data (T-Deck Plus only) significantly improves accuracy.

```
CMD> trackme          # with speaker alerts
CMD> trackme silent   # silent mode — no beeps
CMD> tm [silent]
```

`trackme` is a passive tracking detector. It uses BLE scanning and (on T-Deck Plus) WiFi probe sniffing to detect devices that may be physically following you. The core challenge: your own phone, smartwatch, car Bluetooth, and passengers' phones all look identical to a tracker — they follow you perfectly because they are with you. The algorithm is specifically designed to handle this.

**T-Deck Plus tip:** Run `gps on` before starting `trackme`. If the GPS background task already has a fix, `trackme` skips its 90-second GPS warm-up and starts scanning immediately with location data ready.

---

## Step 1 — Baseline (first 60 seconds)

When you start `trackme`, the first 60 seconds are a **learning period**. Every device detected during this window — your watch, your phone, car Bluetooth, passengers' phones — is marked as a **companion** and shown in dark grey. Companions are never scored or alerted. Only devices that appear *after* the baseline ends are treated as potential trackers.

The display shows a countdown: `BASELINE 47s — learning your devices...`

**Tip:** Start `trackme` *before* you leave home or get in your car so all your own devices are baselined first.

---

## Step 2 — Detection pipeline

After baseline, every new device is processed through a 3-gate pipeline. RSSI is smoothed with a Kalman filter before any decision is made.

### Gate 1 — Signature check

BLE advertisements are matched against known tracker signatures using **two methods**, mirroring how real trackers actually advertise in "separated from owner" (finding) mode:

- **Apple Find My / AirTag** — by manufacturer data: company ID `0x004C` + payload type `0x12`. Apple non-trackers (iPhones, Macs, AirPods doing handoff/tethering/etc.) are excluded by payload type, so matching the Apple company ID alone never flags them.
- **Tile, Samsung SmartTag, Chipolo, Pebblebee, Google Find My Device** — by **16-bit service-data UUID** (`0xFEED`, `0xFD5A`, `0xFE33`, `0xFA25`, `0xFEAA` respectively), which is how these tags advertise when separated from their owner. (Google's `0xFEAA` additionally requires the FMDN frame byte `0x40` so ordinary Eddystone beacons aren't flagged.) These constants are verified against the [AirGuard](https://github.com/seemoo-lab/AirGuard) reference implementation. Other Google Find My Device brands (Eufy, Motorola, Hama, Jio, Rolling Square) ride the `0xFEAA` network and are caught by that row.

### Gate 2 — Behaviour scoring *(unknown BLE devices only)*

Unknown devices accumulate a suspicion score based on: time seen (+25), sighting count (+20), RSSI consistency (+35), and gap-and-return events (+25). A gap only counts if the device was absent for **at least 30 seconds** — a single missed BLE advertisement does not trigger a false gap-return.

| Score | Result |
|-------|--------|
| < 60 | NONE |
| ≥ 60 | NOTICE (if Gate 3 not yet passed) |
| 60–79 + Gate 3 | WARNING |
| ≥ 80 + Gate 3 | ALERT |

### Gate 3 — Confirmation *(required for WARNING or ALERT)*

No alert can sound before Gate 3 passes. Two paths to confirmation:

- **GPS path** (T-Deck Plus): you have moved 200m+ and the device was present throughout with at least one real gap-and-return. Physical movement is direct proof — no time minimum.
- **Time path**: device seen 5+ minutes, RSSI > −80 dBm, 3+ gap-returns or sightings, not explained by a crowd at arrival.

---

## WiFi probe detection *(T-Deck Plus only)*

WiFi probe sniffing detects phones and laptops that broadcast probe requests. Because routers and access points also probe, a WiFi-only device (never seen via BLE) is capped at **NOTICE** and cannot trigger a beep. It can only reach NOTICE if you have moved 100m+ this session and the device followed — a stationary router stays at THREAT_NONE permanently.

WiFi probe sniffing is disabled on the standard T-Deck (no GPS) because without movement data every WiFi detection would be a false positive.

> **wguard compatibility** — If `wguard` is running in background mode, the WiFi probe-sniff phase is automatically skipped for that `trackme` session. BLE scanning continues normally. Run `wg stop` first if you need full `trackme` WiFi detection.

---

## GPS status

| Status | Meaning |
|--------|---------|
| `GPS:none` | No GPS module (standard T-Deck) |
| `GPS:srch` | Module responding, waiting for satellite fix |
| `GPS:142m` | Fix acquired, 142 m moved this session |

---

## Device tiers

| Tier | Capacity | Criteria |
|------|----------|---------|
| Tier 1 | 20 devices | RSSI > −70 dBm — full 3-gate analysis |
| Tier 2 | 100 devices | RSSI −70 to −85 dBm or Apple non-tracker — lightweight tracking only |

Devices below −85 dBm are dropped immediately. Tier 2 devices can be promoted to Tier 1 if RSSI improves or sightings reach 3+.

Press **`v`** to switch between the Tier 1 table (full score/alert columns) and the Tier 2 table (background pool — name/type, RSSI, last seen, sighting count). Each view keeps its own page position.

---

## Alert levels

| Level | Trigger | Speaker | Display |
|-------|---------|---------|---------|
| NONE | Not suspicious | Silent | Grey or white row |
| NOTICE | Known tracker seen 60s + 2 sightings, or score ≥ 60 before Gate 3 | Silent | Yellow row |
| WARNING | Gate 3 confirmed, score 60–79 | 1 beep every 30s | Orange row |
| ALERT | Gate 3 confirmed, known tracker or score ≥ 80 | 3 beeps every 30s | Red row |

**WARNING and ALERT can only fire after Gate 3.** Score alone cannot trigger a beep.

---

## Keys

| Key | Action |
|-----|--------|
| `a` / `l` | Previous / next page |
| `v` | Toggle Tier 1 / Tier 2 view |
| `o` | Cycle sort order: none → score → RSSI → none (Tier 2 always sorts by RSSI) |
| `f` | Toggle alert-only filter on Tier 1 (status line shows `[ALERTS]` when active) |
| `h` | Show help overlay (color legend, columns, all keys) — any key dismisses |
| `w` | Whitelist highest-threat non-companion device (saves to SD) |
| `s` | Save current session log to SD |
| `c` | Clear current session data |
| `q` | Quit |

The active sort column is highlighted yellow in the table header, and a brief on-screen notice confirms each `v`/`o`/`f` toggle so you always know the current view/sort/filter state.

---

## Permanent whitelist

If a device slips through (e.g. your phone connected after baseline), press **`w`** to whitelist it permanently. The MAC is saved to `/apps/trackme/known.csv` on the SD card and recognized on all future sessions.

---

## Custom tracker signatures

The built-in tracker list is **always active**. Drop `/apps/trackme/signatures.csv` on the SD card to **add your own** signatures on top — the file is merged with the built-ins (it does not replace them), so you only list your extras. One signature per line:

```
name , companyId , payloadByte , minLen , level
```

| Field | Meaning |
|-------|---------|
| `name` | Label shown in the table (≤23 chars) |
| `companyId` | BLE manufacturer ID, hex — e.g. `0x004C` (**required**) |
| `payloadByte` | Require `mfr-data[2]` to equal this hex byte; blank/`any` = match any |
| `minLen` | Minimum manufacturer-data length; blank/`0` = no check |
| `level` | `NONE` \| `NOTICE` \| `WARNING` \| `ALERT` (blank = `WARNING`). `NONE` = benign/suppressed |

Only `name` and `companyId` are required; trailing columns are optional. Lines starting with `#` are comments.

```
# extra Apple message types → benign (NONE) so phones/watches don't false-alarm
Apple Handoff,0x004C,0x0C,,NONE
# flag a specific vendor's gadget by company ID
Suspicious Tag,0x1234,,,ALERT
```

> The CSV is **manufacturer/company-ID** based. Service-UUID trackers (Tile, Samsung SmartTag, Chipolo, Google Find My) are detected by the firmware automatically and don't need — and can't be added via — this file.

The file is **merged on top of the built-ins** — the built-in Apple handling and tracker brands stay active, and your lines are appended (exact duplicates of a built-in are ignored). So you only list extras: additional tracker brands, or extra Apple message types to mark `NONE`. A ready-made extras `signatures.csv` is provided in the project's `sd_dropins/` folder. No SD card / no file → the built-ins run exactly as before.

---

## SD layout

| Path | Contents |
|------|---------|
| `/apps/trackme/session.csv` | Session alert log |
| `/apps/trackme/known.csv` | Permanent device whitelist |
| `/apps/trackme/signatures.csv` | Custom tracker signatures |

---

## Known limitations

- **MAC randomization**: iPhones and Android phones rotate BLE MACs every ~15 minutes. Commercial trackers (AirTag, Tile) use stable MACs and are reliably detected. Randomized MACs reset their history on rotation — this is expected behavior, not a bug.
- **Shared radio**: BLE and WiFi share one antenna and cannot scan simultaneously. A brief advertisement may occasionally be missed — the 30-second gap minimum prevents this from counting as a gap-and-return.
- **Cold GPS start**: The GPS module needs ~4 minutes for a cold fix outdoors. Run `gps on` in advance to have a fix ready before starting `trackme`. The `GPS:srch` status with a rising chars count confirms the module is alive during the wait.

---

## Resources

### Tracker Hardware Protocols

- [Apple AirTag — Find My network specification](https://developer.apple.com/find-my/) — Apple's official Find My accessory spec; AirTags use rotating cryptographic identifiers over BLE (company ID `0x004C`, payload byte `0x12`)
- [Tile BLE protocol reverse-engineering](https://github.com/seemoo-lab/openhaystack) — OpenHaystack project documents Tile and AirTag BLE advertisement formats; basis for trackme's signature matching
- [Samsung SmartTag2 — BLE advertisement analysis](https://csa-iot.org/developer-resource/specifications-download-request/) — Samsung uses company ID `0x0075` with SmartThings Find network beacons
- [Chipolo — BLE tracker protocol](https://www.chipolo.net) — company ID `0x0003`, uses Nordic Semiconductor UART service
- [Pebblebee — BLE advertisement format](https://www.pebblebee.com) — Google Find My Device network participant

### Tracking Detection Research

- [**AirGuard** — "Who Can Find My Devices?" (2022, PETS)](https://dl.acm.org/doi/10.1145/3517745.3561428) — Alexander Heinrich et al., TU Darmstadt; foundational paper on passive BLE tracker detection using gap-and-return analysis. trackme's 3-gate pipeline is directly inspired by this work.
- [**"Stalkerware and the AirTag Problem"** — Princeton CITP (2022)](https://freedom-to-tinker.com/2022/05/11/airtag-privacy/) — analysis of AirTag stalking cases; motivates the 200m GPS movement threshold in Gate 3
- [**"Tag, You Can See Me!"** (2023, IEEE S&P)](https://ieeexplore.ieee.org/document/10179390) — Crossley et al.; evaluates detection accuracy of gap-and-return algorithms against real-world stalking scenarios
- [**Apple's AirTag anti-stalking design**](https://support.apple.com/en-us/HT212227) — Apple's own documentation on the 8–24 hour unknown-tracker alert; trackme detects in minutes using movement correlation

### RSSI & Kalman Filtering

- [**Kalman Filter for BLE RSSI smoothing**](https://arxiv.org/abs/1204.0375) — Welch & Bishop, "An Introduction to the Kalman Filter"; the discrete-time formulation used in trackme's RSSI smoothing stage
- [**BLE RSSI distance estimation**](https://www.bluetooth.com/blog/proximity-and-rssi/) — Bluetooth SIG blog on RSSI-based proximity; explains why raw RSSI is noisy and requires filtering (log-distance path loss model)
- [**"Indoor Localization using BLE RSSI"** (2019)](https://ieeexplore.ieee.org/document/8734838) — empirical study on RSSI variance and Kalman filter effectiveness; informs the RSSI consistency scoring (+35 pts in Gate 2)

### MAC Address Randomization

- [**IEEE 802.11-2020 §9.4.1.7**](https://standards.ieee.org/ieee/802.11/7028/) — locally administered address bit definition (bit 1 of byte 0 set = random/LA MAC)
- [**"A Study of MAC Address Randomization in Mobile Devices"** (2017, PETS)](https://petsymposium.org/2017/papers/issue4/paper86-2017-4-source.pdf) — Vanhoef et al.; documents how iOS, Android, and Windows implement MAC rotation and what information still leaks
- [**Android MAC randomization**](https://source.android.com/docs/core/connect/wifi-mac-randomization) — Google's per-network randomization implementation (Android 10+); explains why the same physical phone appears as new device every ~15 minutes
- [**Apple privacy MAC randomization**](https://support.apple.com/guide/security/wi-fi-privacy-secb9cb3140c/web) — Apple's randomization implementation for iOS 14+

### WiFi Probe Request Tracking

- [**"Probe Request Tracking"** — Wireshark wiki](https://wiki.wireshark.org/Wi-Fi) — probe request frame format; contains the SSID list a device is searching for (used by trackme's WiFi sniff on T-Deck Plus)
- [**"Why MAC Randomization is Not Enough"** (2016)](https://dl.acm.org/doi/10.1145/2939918.2939930) — Cunche et al.; shows probe request timing and IE fields still allow device fingerprinting even with random MACs; informs the 100m movement gate for WiFi-only devices

### Related Open-Source Tools

- [**AirGuard** (iOS/Android app)](https://github.com/seemoo-lab/AirGuard) — open-source phone app with similar gap-and-return detection; good reference for expected false-positive rates in real environments
- [**OpenHaystack**](https://github.com/seemoo-lab/openhaystack) — TU Darmstadt; reverse-engineered Apple Find My network; source of AirTag BLE advertisement format used in trackme's signature database
- [**nRF Connect** (Nordic Semiconductor)](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) — BLE scanner app; useful for manually verifying what trackme is seeing
