# Plan: `cast` / `ca` — Chromecast / Google Cast control

## Context
Roadmap item #21 (`cast/ca`) — control Google Cast devices (Chromecast, Google TV, Nest
displays, cast-enabled TVs) on the local network: discover them, launch a video
("rickroll"), and control playback. Own-network use. This is a **plan only — no firmware
code is written here**; execution stays on `feature/pentest-enhancements`, the user
compiles/flashes, CI is the compile-gate.

**Decisions (from the user):** DIAL base **+ Cast v2 stretch**. So Phase 1 ships the light,
reliable DIAL launcher; Phase 2 adds the full TLS/protobuf Cast v2 channel for real playback
control (play/pause/volume/status).

## Feasibility — two protocols, both live (verified 2026-08-24)
Every Cast device exposes two control surfaces:
- **DIAL (port 8008, plain HTTP, no auth)** — the "DIscovery And Launch" protocol. A simple
  HTTP POST launches an app (incl. YouTube by video id). **Light, no TLS — the realistic base
  for the T-Deck.** Still functional on current firmware.
- **Cast v2 (port 8009, TLS)** — length-prefixed **protobuf `CastMessage`** channel for real
  media control. Heavier: hits the ~30–40KB TLS-handshake DRAM cost the project already knows
  ([[weather]] was moved HTTPS→HTTP because TLS failed *under the undercover PSRAM sprite+fonts*
  — from the plain CLI it worked). `cast` runs from the CLI, so Cast v2 TLS is expected to fit;
  flag DRAM as the risk to watch.

Both are **plain STA sockets** (no promiscuous, no softAP) → **no GDMA concern**, SD writes are
safe anytime. Requires `cw` (WL_CONNECTED), like `netspy`/`dpwo`/`arpspoof`.

## Prior art — Cast v2 on ESP32 is PROVEN (not speculative)
Web research (2026-08-24) turned up working ESP32/Arduino implementations, so feasibility +
RAM are already demonstrated:
- **`andrasbiro/ArduCastControl`** — a Cast v2 control library for Arduino/PlatformIO. Deps =
  **ArduinoJson + nanopb**. Does GET_STATUS (artist/title now-playing) + pause/prev/next/seek/
  volume. This is the closest match and the primary **decision point** (reuse vs hand-roll, below).
- **`amitn/ESPCaster`** — ESP32 Cast v2 volume controller (uses `protoc-c`/`cast_channel.proto`).
- **`ivan-krukov/chromecast_dial`** — posts YouTube videos to a Chromecast via **DIAL** (validates
  the Phase-1 path). **`ivynya/ESP32-Rick-Roller`** — compact ESP32 rickroll.

**Protobuf decision (Phase 2):** two viable routes —
- **(A) Hand-roll the `CastMessage`** (6 trivial fields, JSON in `payload_utf8`) — no new
  dependency, matches the project's hand-built-frame ethos (`dpwo`/`isoscan` MQTT/SNMP/DHCP).
  **Recommended** (keeps deps lean; nanopb pulls a codegen toolchain into the build).
- **(B) Reuse ArduCastControl (nanopb)** — faster to working, but adds nanopb + its generated
  `cast_channel.pb.*`. Good **reference to crib the exact message flow from** even if we hand-roll.
Recommend **A**, using ArduCastControl/ESPCaster as the reference implementations to validate
against.

## Discovery
- **mDNS**: Cast devices advertise `_googlecast._tcp.local`. Use ESP32 `ESPmDNS`
  (`MDNS.queryService("googlecast","tcp")`) → each result gives host/IP/port + TXT records
  (`fn`=friendly name, `md`=model, `id`=uuid). Cleanest path; the firmware already parses mDNS
  in `netspy` (reference, not reused directly — the query API is simpler here).
- **Manual target**: `ca <ip>` via the shared `resolveNetTarget()` (from `network_scanner.cpp`,
  already exposed for `ps`/`pg`/`dpwo`/`as`) so `ip` / `nd#` / `ns#` all work.
- Discovered devices → `/apps/cast/devices.csv` (`ip,name,model,uuid`); a `favorites.csv` for
  quick re-targeting.

## Phase 1 — DIAL launcher (light, HTTP:8008) — the base
Raw `WiFiClient` (the `dpwo` socket pattern, no HTTP lib):
- **Launch YouTube**: `POST http://<ip>:8008/apps/YouTube` body `v=<videoId>` → plays that video.
  A built-in **`ca rickroll <ip|#>`** preset ships a known video id.
- **App status**: `GET /apps/<AppName>` → running/stopped + parse state.
- **Stop**: `DELETE /apps/<AppName>/run`.
- **Generic**: `ca launch <ip|#> <AppName> [param]` for other DIAL apps.
- Device info: `GET /setup/eureka_info` (JSON — name, model, build) for a richer device list.
- (SSDP discovery of DIAL uses `M-SEARCH` ST `urn:dial-multiscreen-org:service:dial:1`, but for
  Chromecast specifically mDNS `_googlecast._tcp` is the reliable discovery path — see Discovery.)
- Honesty: DIAL YouTube launching works today but Google has narrowed DIAL over the years —
  if a device refuses, the UI says so and points at the Cast v2 path (Phase 2).

## Phase 2 — Cast v2 channel (TLS:8009, hand-rolled protobuf) — the stretch
Real playback control via `WiFiClientSecure` (`setInsecure()` — Cast uses a self-signed cert)
+ `setBufferSizes()` to trim the TLS DRAM footprint.

**`CastMessage` is hand-encodable — no protobuf library needed** (same approach as the
hand-built MQTT/SNMP/DHCP frames in `dpwo`/`isoscan`, rule 5b). It has 6 fields, all trivial
wire types, and the actual commands are **JSON strings** inside `payload_utf8`:
```
CastMessage {
  protocol_version = 0   // field1 varint  → tag 0x08 00
  source_id       = "sender-0"    // field2 len-delim → 0x12 <len> …
  destination_id  = "receiver-0"  // field3          → 0x1A <len> …
  namespace       = "urn:x-cast:…"// field4          → 0x22 <len> …
  payload_type    = 0 (STRING)    // field5 varint   → 0x28 00
  payload_utf8    = "<json>"      // field6 len-delim → 0x32 <len> …
}
```
Framing = **4-byte big-endian length prefix + serialized CastMessage**. Build/parse JSON
payloads with **ArduinoJson** (already a `lib_deps` dependency).

**Message flow (rickroll / play a media URL via the Default Media Receiver):**
1. TLS connect `:8009`.
2. `CONNECT` — ns `urn:x-cast:com.google.cast.tp.connection`, dest `receiver-0`, `{"type":"CONNECT"}`.
3. Heartbeat `PING`/`PONG` — ns `…tp.heartbeat` (keep-alive loop).
4. `LAUNCH` — ns `…receiver`, `{"type":"LAUNCH","appId":"CC1AD845","requestId":1}` (Default Media Receiver).
5. Read `RECEIVER_STATUS` → `status.applications[0].transportId` (e.g. `"web-5"`) + `sessionId`.
   (LAUNCH also accepts `appId":"YouTube"` if you'd rather drive the YouTube receiver via Cast v2.)
6. `CONNECT` to that `transportId` (virtual connection — reuse the CONNECT payload, new dest).
7. `LOAD` — ns `…media`, dest `transportId`,
   `{"type":"LOAD","requestId":2,"media":{"contentId":"<mediaURL>","streamType":"BUFFERED","contentType":"video/mp4"},"autoplay":true}`.

**Controls** (once a media session exists): `PLAY`/`PAUSE`/`STOP` on `…media` with the
`mediaSessionId` from status; `SET_VOLUME`/mute on `…receiver`; `GET_STATUS` for now-playing.

**Honest scope note:** the Default Media Receiver plays **direct media URLs** (mp4/HLS), *not*
YouTube page URLs — so the Cast v2 "rickroll" preset is a hosted MP4, while the DIAL path
rickrolls by YouTube video id. Both ship.

## Command surface
- Register `cast` / `ca`, category **Network**, in `setupCommands()` (command table now 128, room
  is fine). Needs `cw`.
- Sub-commands: `ca` (scan + interactive picker) · `ca <ip|#>` (target) · `ca rickroll <ip|#>` ·
  `ca launch <ip|#> <url|videoId>` · `ca stop <ip|#>` · `ca vol <ip|#> <0-100>` (Phase 2) ·
  `ca pause`/`ca play` (Phase 2).

## UI
Follow the `netspy`/`dpwo` interactive idiom (rule 5b — same look/feel):
- mDNS scan → trackball-selectable device list (name / model / IP), `[u]` rescan.
- Select a device → action menu (Rickroll / Load URL / Play / Pause / Stop / Volume / Status).
- **Confirm-before-fire**: echo the target name+IP before launching (it drives someone's TV).
- Live now-playing line from `GET_STATUS` (Phase 2). Lock-aware (`consumeJustUnlocked`).

## Reuse map (rule 5b — don't rebuild)
- `resolveNetTarget()` (`network_scanner.cpp`) for `ip`/`nd#`/`ns#`.
- `dpwo` raw-`WiFiClient` request/response pattern for DIAL.
- `ArduinoJson` (existing dep) for Cast v2 JSON payloads.
- `ESPmDNS` for discovery; `netspy`'s mDNS parsing as reference.
- The `dpwo`/`isoscan` hand-built-frame style for the `CastMessage` protobuf envelope.
- **`ArduCastControl` / `ESPCaster` as reference implementations** — crib the exact Cast v2 message
  sequence/timing from them even though we hand-roll (don't vendor nanopb unless route B is chosen).

## Module + build
- New module `wifi/tools/chromecast/chromecast.{cpp,h}` (free fn `runCast(char*)`, wardrive/dpwo
  pattern). Add its `-I` path to `[includes]` in `platformio.ini`.
- SD: `/apps/cast/` = `devices.csv` (discovered) + **`media.csv` saved content** (`name,url-or-youtube-id`,
  self-seeded with Rickroll + a sample MP4 on first run; http(s) = Cast v2 media, bare id = YouTube via DIAL)
  + **`share/` folder** — drop local photos/videos here to cast them (see below).

## Local file sharing (photos / videos off the SD)
A Cast device can't be *pushed* a file — it fetches media from a URL. So to cast a local SD file the
T-Deck briefly becomes an **HTTP file server** (`WiFiServer` on :8123) and hands the device a
`http://<t-deck-ip>:8123/<file>` URL via the Cast v2 LOAD (images: contentType `image/*`, streamType
`NONE`; video: `video/mp4`, `BUFFERED`). The request handler honours **Range** (206) so video seeking
works. `ca share <ip> [file]` or the **"Share photo/video (SD)…"** menu item → pick from `/apps/cast/share`
→ serve. SD **reads only** while serving (no writes) → GDMA-safe on plain STA.
**Honest limits:** the T-Deck stays a live server while a video plays (blocks on that screen until `[q]`;
images fetch once then it can stop); no transcoding — the device must natively support the codec
(mp4/H.264 and webm/VP8-9 play; mkv/avi/HEVC generally won't).
- NOTICES entry: Cast v2 / DIAL protocol references (node-castv2, HackerNoon protocol write-up) —
  methodology only, no code copied.

## Risks / honesty
- **TLS DRAM (Phase 2)** — the known ceiling; run only from the CLI, trim buffers, degrade to the
  DIAL path if the handshake fails. This is the main thing to verify on HW.
- **DIAL narrowing** — YouTube launch works today but is not guaranteed forever; Cast v2 is the
  durable path.
- **Own devices only** — this controls real TVs; the confirm-before-fire step is mandatory.
- Cast v2 media = direct URLs, not YouTube pages (see scope note).

## Verification (user, on hardware)
1. `cw` to the LAN, run `ca` → confirm mDNS lists the real Cast device(s).
2. Phase 1: `ca rickroll <#>` launches the YouTube video on the TV; `ca stop` stops it.
3. Phase 2: `ca launch <#> <mp4-url>` plays via Default Media Receiver; `ca pause/play/vol` work;
   watch free-DRAM during the TLS handshake (the risk metric).
4. No GDMA/SD issues (plain STA the whole time). *Do not run `pio` in-session — CI/user builds.*

## Sources (research 2026-08-24)
- Cast v2 protocol + `CastMessage` framing/namespaces/payloads — [thibauts/node-castv2](https://github.com/thibauts/node-castv2)
- ESP32 Cast v2 control lib (deps + proven feasibility) — [andrasbiro/ArduCastControl](https://github.com/andrasbiro/ArduCastControl), [amitn/ESPCaster](https://github.com/amitn/ESPCaster)
- DIAL rickroll on ESP32 / DIAL YouTube launch — [ivan-krukov/chromecast_dial](https://github.com/ivan-krukov/chromecast_dial), [ivynya/ESP32-Rick-Roller](https://github.com/ivynya/ESP32-Rick-Roller)
- DIAL protocol (SSDP/`M-SEARCH`, app-launch) — [Wikipedia: Discovery and Launch](https://en.wikipedia.org/wiki/Discovery_and_Launch)
- DIAL vs Cast v2 ports/overview — [The Chromecast Protocol (HackerNoon)](https://hackernoon.com/the-chromecast-protocol-a-brief-look)
