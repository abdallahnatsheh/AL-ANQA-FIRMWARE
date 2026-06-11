# EvilTwin Overhaul + CI + GDMA Guard

## Status
Implemented + field-tested working (2026-06-11). Committed on
`feature/pentest-enhancements`: `8bb0dee` (overhaul + CI + guard) and
`a6abdfd` (Utils.h case fix). Built green on both T-Deck and T-Deck-Plus.

## EvilTwin fixes (`wifi/attacks/eviltwin/eviltwin.cpp/.h`)

### 1. Portals captured no creds — THE big bug
Root cause: the 28 SD portals in `/apps/eviltwin/portal/` submit to
`action="/get"` (mostly GET) with field names `email`/`password` (some
`uname`/`psw`). Old code registered only `POST /post` reading args `user`/`pass`,
so every SD portal fell through to the redirect handler → creds discarded. Only
the 2 built-in templates worked (they use `/post` + `user`/`pass`).

Fix — path- and field-agnostic capture:
- `setupRoutes()`: `/post` AND `/get` both registered `HTTP_ANY`; `onNotFound`
  also routes to `handleCapture()`.
- `captureArgs()`: iterates `server.args()`, classifies each by case-insensitive
  substring. `etIsUserField` (email/user/login/uname/account/phone/identifier/
  id/name/tel), `etIsPassField` (pass/pwd/psw/passcode/pin), `etIsIgnoreField`
  (remember/token/csrf/captcha/submit/viewport/layer — never used as username in
  fallback). Fallback: pass found but no recognized user field → first non-junk arg.
- `handleCapture()` = `captureArgs()` + `handleRedirect()` (reload looks like a
  failed login → victim re-enters → captured again).
- Built-ins still work (`user`/`pass` match the same classifier).

### 2. Template picker only showed first 8
Old picker collected `while (count < ET_PER_PAGE=8)` with no pagination — 28
templates, only 8 reachable. Now `ET_TEMPLATE_MAX=64` collection + paginated
`pickTemplate()` listing built-ins + all SD files, current selection green `>`,
`n`/`p` paging. Replaced the old split `[t]tmpl`/`[p]portal` keys with one `[p]`.

### 3. Switching portals broke the captive popup + no feedback
`[p]` did `server.stop(); dns.stop();` but never `dns.start()` again — DNS hijack
dead, so new clients got no "Sign in to network" popup. Added `dns.start()` after
restart. Added transient notice `_uiNoticeMs`/`_uiNoticeText`/`_uiNoticeColor`
(1.5s, green) confirming each switch; red "Portal file missing — fell back" in
`handleRoot()` if a custom file disappears.

### 4. Save was destructive
Old `saveCredsToSD()` did `SD.remove()` + rewrote ≤20 creds WITHOUT timestamps —
lost timestamps and any creds beyond 20 already logged live. Replaced with
RAM-buffered capture + incremental flush:
- `handlePost`→`captureArgs` stores `{user,pass,ts}` in `_creds[ET_MAX_CREDS=30]`,
  NO SD write on the hot path (GDMA rule — the form-submit moment is the worst
  time to corrupt FatFS while AP/promiscuous DMA is live).
- `flushCredsToSD()` appends only `_creds[_savedCount..total)`, never removes,
  preserves timestamps, tracks `_savedCount`. `[s]` checkpoints; exit auto-flushes
  in the fully-safe window (after AP down + promiscuous off + WIFI_STA).
- `[c]` creds table now services `dns`/`server` so the portal stays live.

## GDMA guard (`wifi/core/wifi_sd_guard.h`) — NEW
`ScopedPromiscPause` RAII: reads current promiscuous state in ctor, pauses if on,
restores in dtor. Self-correcting → safe no-op when promiscuous is off, so it can
be dropped anywhere. Encodes the "pause promiscuous around SD writes" rule as a
type instead of a convention. eviltwin `flushCredsToSD()` is the reference user.
Other modules (wguard doAutoSave, wifimon pcap flush, handshake/pmkid) still use
the hand-rolled pattern — opt-in incremental migration, NOT a forced refactor.

## CI compile-gate (`.github/workflows/build.yml`) — NEW
Builds both `T-Deck` + `T-Deck-Plus` via `pio run` on every push/PR to
`main`/`feature/pentest-enhancements` (paths-filtered to firmware/lib/
platformio.ini). Matrix, fail-fast off, caches `~/.platformio`. No hardware /
no runtime test — pure compile gate. Local equivalent: `pio run -e T-Deck -e T-Deck-Plus`.
README has a build badge (repo: `abdallahnatsheh/T-REX-FIRMWARE`).
`convert_splash.py` is stdlib-only (no Pillow) and `lib/` is vendored (no
submodules) → CI needs only python + platformio.

## Bug CI caught immediately
`core/system/utils.cpp` included `"Utils.h"` but file is `utils.h` — compiled on
case-insensitive Windows, failed on Linux/CI. Fixed (`a6abdfd`). Verified it was
the ONLY case-mismatched include project-wide. Lesson: match `#include` case to
filename exactly; CI now enforces it.

## Docs updated
`docs/eviltwin.md` (portal compat + RAM/flush save + keys), `CLAUDE.md` (EvilTwin
section + GDMA guard + CI lines), `man_pages.cpp` (et keys), README badge.
