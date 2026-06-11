# TrackMe service-UUID detection + signature format + notif test

## Status
Implemented, builds-pending-flash (2026-06-11). Uncommitted at time of writing.
Files: `bluetooth/tools/trackme/trackme.cpp/.h`, `ui/notifications/notification_manager.cpp/.h`,
`core/cli/man_pages.cpp`, `core/cli/command_manager.cpp`, docs.

## The core upgrade — match trackers by SERVICE-DATA UUID, not company ID
Verified from seemoo-lab/AirGuard source (the authoritative open-source tracker
detector). Only Apple is detected by manufacturer/company ID; EVERY other tracker
is detected by a 16-bit service-data UUID in "separated from owner" (finding) mode:

| Tracker | Match | Constant |
|---|---|---|
| Apple Find My / AirTag | mfr company `0x004C` + type `0x12` | (unchanged) |
| Tile | service-data UUID | `0xFEED` |
| Samsung SmartTag | service-data UUID | `0xFD5A` |
| Chipolo | service-data UUID | `0xFE33` |
| Pebblebee | service-data UUID | `0xFA25` |
| Google Find My Device | service-data UUID + frame byte `0x40` | `0xFEAA` |

Eufy/Motorola/Hama/Jio/Rolling Square ride Google FMDN → caught by `0xFEAA`.
The OLD built-in company-ID rows for Tile/Samsung/Chipolo/Google were REMOVED:
they rarely matched real tags, and Samsung `0x0075`/Google `0x00E0` also matched
ordinary phones (false positives). Net: actually detects the tags now + fewer
false positives. Google `0x40` byte requirement avoids Eddystone-beacon false
positives (0xFEAA is shared with Eddystone) — UNVERIFIED exact byte, field-test it.

## Implementation
- `TrackerSig` gained `uint16_t svcUuid; uint8_t svcByte;` (0 = company-ID sig).
- `TmBleEntry` (scan ring) gained `svcUuid`/`svcByte`.
- `onResult()` extracts the tracker service UUID via `dev->getServiceData(NimBLEUUID((uint16_t)x))`
  (same API bmon uses for Eddystone 0xFEAA) against a hardcoded set `{FEED,FD5A,FE33,FEAA,FA25}`.
- `matchSig(companyId,mfrType,mfrLen,svcUuid,svcByte)` checks service-UUID sigs FIRST, then mfr.
- svcUuid/svcByte threaded through `processDevice`/`initDev`/`drainBleRing`.

## Signature CSV (still company-ID format, MERGES with built-ins)
`loadSignatures()`: `fillBuiltinSigs()` ALWAYS loads first, then SD file APPENDS
(dedup on companyId+payloadByte). Format: `name,companyId,payloadByte,minLen,level`
(only name+companyId required; level via `tmParseLevel()`, blank=WARNING; NONE=benign).
SD rows are company-ID only (svcUuid=0) — service-UUID trackers are built-in, not
CSV-addable. Provided extras file `sd_dropins/apps/trackme/signatures.csv` = 6 extra
Apple message types (0x03/0x0A/0x0B/0x0C/0x0D/0x0E → NONE) to suppress more iPhone/
Watch chatter. See [[project_eviltwin_overhaul]] for the sd_dropins convention.

## notif test (notification_manager.cpp)
`notify(NotifLevel, bool force=false)` — force ignores per-level enable.
`nf test` → interactive picker (1-5 play a level, [a] all, [q] quit, green `>` on last);
`nf test <level>` → direct play. Uses force=true so it plays even if level is off.
NOTE: `spktest` keys a/w/c/i/p ALREADY trigger NotificationManager (respecting on/off);
`nf test` is the force-play + in-command-help version.

## Audio = WAV not MP3 (corrected stale docs)
`playWav()` is a raw RIFF/WAVE PCM parser — NOT MP3/ESP8266Audio. Custom sounds must
be 16-bit PCM, 22050 Hz, mono, in `/config/notification/*.wav`. `_mp3File[]` member is
a misnomer. `docs/audio.md` was fully stale (said MP3, /notification/, ESP8266Audio) — fixed.
