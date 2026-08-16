# AL-ANQA `pwn` — Autonomous Capture→Crack→PWNED WiFi Pet

**Status:** v1 FIRST CUT — ✅ COMPILES CLEAN on T-Deck-Plus (2026-08-10; RAM 63.6%,
Flash 37.5%). NOT yet HW-tested; base T-Deck env not yet built locally (CI covers it).
Module `wifi/attacks/pwn/pwn.{h,cpp}` + shared parser `wifi/core/cap_parse.h`
(extracted from capcrack, DRY). Registered `pwn`/`pw` (WiFi). `-I` added to
platformio.ini (new-module dir needs it). See "Build status" (§10a).

### Decisions locked (2026-08-10)
- **Q1 — DRY core extraction: YES.** Factor reusable capture/crack cores out of
  `ws`/`pm`/`cc` **only if it does not break them** (retest via CI compile-gate +
  behavior unchanged). If a clean extraction can't be done without risking those
  commands, fall back to a minimal isolated path for that piece and note why.
- **Q2 — Standalone command `pwn`/`pw`** (WiFi). Not folded under `ws`.
- **Q3 — Cracking source: Option B (session + backlog) with a per-hash RESUME
  CURSOR** (`progress.csv`: each cap remembers its wordlist position, resumes across
  slices/reboots — never restarts at word 0). Default ON, `[k]` → session-only. §6a.
- **Q4 — Both modes shipped in v1:** `pwn` (active: deauth+assoc) and
  `pwn passive` (sniff-only, zero TX). Runtime `[m]` toggle too.
- **§6c smart crack ordering — v1:** full priority queue (prior related passwords by
  SSID or BSSID → default-WiFi list → wordlist), every candidate verified.
- **`.cap` naming — MAC+name:** `<BSSID>_<SSID>.cap` (sanitized), one file per
  (BSSID,SSID) instance; reset/renamed AP keeps both captures.
- **No hop mid-capture:** capture-in-progress extends the channel dwell.
- **Whitelist (v1):** `/apps/pwn/whitelist.csv`, **precise BSSID matching by
  default** (add via `pwn wl add <idx>` from last `sw` scan, or raw MAC). Name-based
  matching (`add ssid <name>`) is an explicit, warned opt-in only — never default,
  since it skips ALL same-name APs. Whitelisted = fully ignored. §6d.
**Command idea:** `pwn` / `pw` (Category: WiFi)
**One-line pitch:** An unattended, channel-roaming "pet" that captures WPA/WPA2 key
material **and cracks it on-device** during idle epochs — showing a triumphant
state only when it has recovered a **real password**, not merely a capture.

---

## 1. Why this is worth building (the novelty)

Every pwnagotchi-class device on ESP32 (Hash Monster, minigotchi, ESP32 WiFi
Penetration Tool) and the original Pwnagotchi on Pi are **capture-only** — they
harvest handshakes/PMKIDs to disk and crack later on a laptop/GPU. The pet's
"happy" moment is triggered by *capturing key material*, never by *recovering a
password*.

AL-ANQA already has **HW-verified on-device WPA cracking** (`ws` = PBKDF2→PRF-512→
MIC verify; `pm` = PBKDF2→HMAC-SHA1 PMKID; `cc` = standalone offline cracker). No
other pwnagotchi has that primitive sitting in the same firmware. So AL-ANQA can
close the loop no one else has: **capture AND crack, autonomously, on the device.**

**Honesty (load-bearing — per user prefs):** the pet only claims `PWNED` when it
has actually cracked a key against its on-SD / built-in dictionary. It is NOT a
GPU cracker; it will only break weak/common/wordlist-present passwords. Everything
else is captured for offline cracking, exactly like the others. The UI must state
this plainly.

### Explicitly NOT claimed as novel
- **AI / reinforcement learning** — Pwnagotchi's A2C/LSTM is theater on an MCU and
  nobody ports it. We use a deterministic heuristic roamer (Hash Monster-style) and
  say so. "Moods" are derived from real capture/crack rate, not a policy network.
- **GPS geotagging** — Pwnagotchi already has a GPS plugin. We MAY geotag (Plus
  only, cheap since `wardrive` infra exists) but it is not the headline.
- **Peer grid** — minigotchi already does pwngrid peer detection on ESP32. Out of
  scope for v1.

---

## 2. Scope

### v1 (this plan)
- Channel-roaming autonomous loop, foreground UI, `[q]` to quit.
- Capture: full/half 4-way handshakes (deauth-assisted) + PMKID (assoc-triggered).
- Opportunistic on-device dictionary crack during idle/bored epochs.
- Pet face + moods driven by **real** events (target seen / captured / cracked).
- SD output: per-(BSSID,SSID) `<BSSID>_<SSID>.cap` + `captured.csv` + `cracked.csv`
  + `progress.csv` + `whitelist.csv`.
- **Three modes** (§7a): `pwn` (active — deauth+assoc), `pwn stealth` (low-signature),
  `pwn passive` (sniff-only, zero TX). Runtime `[m]` cycles them.
- **Whitelist** (§6d) — never-touch APs, add by `sw` index / MAC / SSID name.
- **GPS geotag** of captures (Plus only) — `lat,lon` columns in `captured.csv`.
- Session stats (APs seen, handshakes, PMKIDs, cracked count, uptime).

### v1 explicit non-goals
- No RL/AI. No ESP-NOW grid. No WPA3 handling (see v2). No background/headless-lid
  mode in v1 (foreground only — simpler; background is v2). No web UI.

### v2 / later (park in `next_steps`, do not build yet)
- **WPA3-transition awareness** — auto-invoke `wpa3down` engine on TD APs. Gated by
  `wpa3down` becoming HW-tested first (currently NOT HW-tested → shaky foundation).
- Background mode (`pwn bg`) with a status-bar badge, like `wg bg` / `mw bg`.
- **WiGLE-format export** of geotagged captures (basic `lat,lon` logging IS v1; the
  full WiGLE-1.4 CSV export like `wardrive` is the v2 part).
- SSID→password auto-fill across same-SSID siblings (verified, not assumed).
- ESP-NOW "greet" beacon so two decks show each other's pwn counts.
- Pwnagotchi DETECTION (see §11 bonus) — v1-or-fast-follow, decision pending.

---

## 3. Reuse map (this is ~90% orchestration — rule 5b)

| Need | Existing module / file | Notes |
|---|---|---|
| Scan / target list | `wifi_functions` (`sw` scanner) | AP list: BSSID/ch/ssid/sec/rssi |
| Full/half handshake + deauth | `HandshakeCapture` (`wifi/attacks/handshake/`) | deauth + EAPOL sniff, `g_whs` M1/M2, pcap lt105 |
| PMKID capture (passive M1) | `PmkidAttack` (`wifi/attacks/pmkid/`) | assoc-trigger, PMKID KDE parse |
| On-device crack | `HandshakeCapture::crack()` / `PmkidAttack` crack / `cc` (`capcrack`) | PBKDF2/PRF/HMAC already HW-verified |
| Channel roam + radio-idle discipline | `wardrive` roam pattern | scan-then-idle-before-SD, GDMA discipline |
| Pet sprite + moods + popup | `buddy` (`fun/buddy/`) | reuse sprite engine + `petTick`/mood; `g_covert`-aware |
| Notifications / sound | `NotificationManager` (`ui/notifications/`) | gated by `g_covert` already |
| GDMA-safe SD writes | `ScopedPromiscPause`, existing pcap writers | open files before WiFi, write after teardown/pause |
| Undercover silence | `g_covert` flag | pet audio/visual tells must honor it |

**Key implementation question to resolve before coding (Section 8):** `ws` and `pm`
today are *interactive, blocking* commands that own the screen and their own WiFi
setup/teardown. `pwn` needs their *capture + crack cores* driven **headlessly** in a
roam loop. We must factor out reusable cores (`hsCaptureOnce(bssid,ch,...)`,
`pmCaptureOnce(...)`, `crackCap(path,wordlist)`) without breaking the existing
`ws`/`pm`/`cc` commands. This is the main refactor cost.

---

## 4. State machine (deterministic — the "AI" replacement)

Epoch loop (each epoch = one channel dwell). Timings are heuristic constants,
tunable, NOT learned:

```
STATE: ROAM
  - hop to next channel in the plan (default 1,6,11 ; optional full 1..13)
  - passive scan the dwell window (RECON_MS, default ~4000ms)
  - DROP any whitelisted AP here (§6d) — never enters the target list
  - if new APs/clients seen -> note targets, stay "active"
  - transition -> TARGETING if any un-pwned target on this channel
                 -> IDLE if nothing new for N consecutive epochs

STATE: TARGETING (active mode only; passive skips deauth/assoc)
  - for each un-captured target on current channel (RSSI above CUTOFF):
      * send assoc frame -> try to grab PMKID (M1)
      * deauth its clients briefly -> catch handshake (M1..M4)
  - budget-limited (MAX_ATTACK_MS per epoch) so roam keeps moving
  - captures -> RAM -> flush to SD .cap AFTER radio idle (GDMA rule)
  - transition -> ROAM

STATE: IDLE / "bored"  (no new activity for BORED_EPOCHS)
  - **the novel bit**: spend the dead air cracking.
  - pick the next un-cracked capture from this session (or SD backlog)
  - run K wordlist candidates (time-boxed CRACK_SLICE_MS) then yield to roam
  - on success -> record cracked.csv, mood=PWNED, notify (if !covert)
  - transition -> ROAM (keep roaming; cracking is interleaved, never blocks capture)
```

Mood derivation (real signals only):
- `SLEEP/bored` — nothing in range.
- `hunting` — targets visible, attacking.
- `excited` — just captured a handshake/PMKID.
- `PWNED` — just cracked a password (the triumphant face; the differentiator).
- `lonely/lost` — long stretch with zero APs.

---

## 5. Roaming & timing constants (draft — tune later)

| Const | Draft | Meaning |
|---|---|---|
| `PWN_CHANNELS` | {1,6,11} | default hop set; `pwn full` = 1..13 |
| `PWN_RECON_MS` | 4000 | passive dwell per channel |
| `PWN_ATTACK_MS` | 3000 | max deauth/assoc budget per epoch |
| `PWN_BORED_EPOCHS` | 3 | consecutive quiet epochs → start cracking |
| `PWN_CRACK_SLICE_MS` | 1500 | crack time per idle slice before yielding to roam |
| `PWN_RSSI_CUTOFF` | -75 dBm | ignore too-weak APs (don't waste attack budget) |
| `PWN_MAX_TARGETS` | 64 | session target table cap |

All `millis()`-based, no `delay()` in loops (coding rule). Poll
`getKeyboardInput()` for `q` every iteration.

---

## 6. SD layout

`/apps/pwn/` (new folder, add to `ensureTreeStructure()` + `/apps/README.txt`):
- `/apps/pwn/<BSSID>_<SSID>.cap` — captures (libpcap lt105, reuse pcap writer). Named
  by **MAC + name** so a reset/renamed AP (same MAC, new SSID) keeps BOTH captures
  instead of overwriting. SSID portion is **sanitized** for FAT (illegal chars
  `/\:*?"<>|`+space → `_`, empty/hidden → `hidden`, truncated to fit); the REAL SSID
  stays intact inside the `.cap` beacon (that's the PBKDF2 salt). Never overwrite.
- `/apps/pwn/captured.csv` — `time,bssid,ssid,ch,type(HS|PMKID),rssi[,lat,lon]`
- `/apps/pwn/cracked.csv` — `time,bssid,ssid,password` (the payoff file)
- `/apps/pwn/progress.csv` — `bssid,ssid,wordlist_id,next_index` resume-cursor ledger
  (§6a). Keyed by the **(bssid,ssid) pair** since one BSSID can have two caps.
- `/apps/pwn/wordlist.txt` — optional user dictionary; else built-in ~100 list
  (same convention as `ws`/`pm`)
- `/apps/pwn/whitelist.csv` — typed rows `type,value,label` (`bssid,…` precise /
  `ssid,…` broad) — APs to NEVER touch. See §6d.

GDMA: all `.cap`/CSV writes happen with promiscuous paused (`ScopedPromiscPause`)
or after `WiFi.mode(WIFI_STA)` — never during APSTA/promiscuous DMA.

### 6a. Cracking source + resume-cursor ledger (Q3 = Option B)

When idle, the pet cracks from **both** this session's captures **and** the backlog
of un-cracked `.cap` files already in `/apps/pwn/`. This makes it a *persistent*
cracker — old loot is worked during future idle time, not forgotten.

**The load-bearing part: per-hash resume cursor.** Cracking is time-sliced
(`PWN_CRACK_SLICE_MS`, ~1.5s) and interleaved with roam, so a slice only tries a
chunk of the wordlist before yielding. We MUST remember *where each cap stopped* in
the wordlist, or every slice restarts at word 0 and never progresses. The cursor
persists across slices AND across sessions/reboots — resume, never restart.

**One ledger — `/apps/pwn/progress.csv`** (replaces the earlier `tried.csv` idea):
```
bssid,ssid,wordlist_id,next_index
```
- Keyed by the **(bssid,ssid) pair** — one line per `.cap`, since a reset/renamed AP
  can produce two caps for one BSSID (§6b).
- `next_index` = the wordlist line to resume at for this cap. A slice starts there,
  tries words until `PWN_CRACK_SLICE_MS` elapses (or a chunk cap), then writes the
  advanced `next_index` back.
- `wordlist_id` = cheap hash of the active wordlist (size+mtime or CRC). If it does
  NOT match a cap's stored id, treat `next_index` as 0 (wordlist changed/extended →
  re-arm from the top). Correct-but-conservative; a "wordlist only grew, resume at
  old count" optimization can come later.
- A cap is a **crack candidate** iff: not in `cracked.csv` AND
  `next_index < wordlist_count` (for the current `wordlist_id`).
- **`cracked.csv`** — cap solved; always skipped; its progress line is dropped.
- "Exhausted" = `next_index >= wordlist_count` → no separate `tried.csv` needed.

**Persistence cadence:** cursor lives in RAM during the session; flushed to
`progress.csv` (a) at each slice yield when the cursor advanced, (b) on every crack
hit, (c) on `[q]` exit. Worst case a yanked power loses ≤1 slice of progress. All
`progress.csv` writes obey the GDMA rule (`ScopedPromiscPause`, since roam keeps
promiscuous live between slices; the crack math itself is CPU-only, no radio).

- Startup reads `progress.csv` + `cracked.csv` (SD reads, WiFi not up yet → no GDMA
  concern) to build the candidate queue with each cap's resume point.
- **`[k]`** toggles backlog OFF → session-only (ignores old caps, but still uses the
  cursor within the session).

Cracking NEVER blocks capture — a new target in range always preempts the slice.

### 6b. Deduplication — don't re-capture an AP we already own

**Dedup key = the (BSSID, SSID) pair.** The `.cap` filename encodes both
(`<BSSID>_<SSID>.cap`), so the filesystem enforces one file per distinct network
instance. A renamed/reset AP (same MAC, new SSID) is a NEW instance → new file, old
capture kept.

TARGETING decides per (BSSID,SSID):
- **In `cracked.csv`** → skip entirely, never re-attack (we have the password).
- **Has a usable `.cap`, not cracked** → do NOT re-attack (crack material is
  identical); instead spend idle time cracking the existing cap via §6a.
- **Partial capture only** (M1 without M2, or PMKID KDE absent — the capture core
  reports completeness) → re-attack to complete it.
- **Never seen** → capture.

`captured.csv` logs the capture EVENT once per BSSID (first time usable material is
obtained), not every re-sighting.

Edge cases:
- **Same SSID, different MAC** → two distinct APs, two `.cap` files (mesh nodes,
  multi-AP networks, or evil twins — correct to separate).
- **Same MAC, SSID changed** (factory reset / rename) → NEW `.cap` (name is in the
  filename), OLD capture kept too. `cracked.csv` is append-only, so the OLD password
  resurfaces as a same-BSSID priority candidate for the new capture (§6c step 1).

### 6c. Smart crack ordering — priority candidates (verify, never assume)

Same-SSID/different-MAC APs *may* be the same network (mesh) OR two unrelated
networks that just share a common default SSID (`NETGEAR`, `linksys`,
`TP-Link_A2F1`…). So we must NOT auto-copy a sibling's password — we TRY it and let
the crack verify it. (This supersedes an earlier, unsafe "auto-mark solved" idea,
which could have written a wrong password to `cracked.csv`.)

Per-cap crack order is a priority queue; every candidate is verified by the real
PBKDF2→MIC / PMKID check, so a hit is always a genuine password:
1. **Prior RELATED passwords** — union of passwords already in `cracked.csv` that are
   related to this AP by **either key**:
   - **same SSID** (same-network mesh / shared name / owner reuse), and
   - **same BSSID** (same physical radio whose SSID changed — factory reset or
     rename; owners often keep the password through a rename).
   Dedup the union; usually 0–3 candidates, nearly free. NOTE: the PMK is
   `PBKDF2(password, SSID)`, so we reuse only the old *password string* and re-derive
   + verify it against the CURRENT capture's SSID — correct even when the SSID
   changed. Verified, never assumed.
2. **Common-default WiFi list** — small built-in high-probability list (default
   router PSKs / common patterns), tried before the big grind.
3. **Full wordlist** — from `next_index` onward (the §6a resume cursor).

Priority lists (1+2) are re-run at the start of each SESSION's crack pass for a cap
(cheap, and `cracked.csv` grows over time so yesterday's crack helps today). The
big-wordlist cursor is unaffected — priority candidates are tried *before* it, not
counted into `next_index`.

**DECIDED (2026-08-10): §6c is v1** — full priority queue (steps 1+2+3) ships in v1.
Prior-related passwords (both keys) + default list + wordlist, all verified.

---

### 6d. Whitelist — never touch these APs (parity + authorization)

Pwnagotchi-style whitelist: APs that `pwn` must **fully ignore** — never deauth,
never assoc, never capture, never crack. Serves feature parity AND the "own networks
only / authorized use" honesty rule. Dropped in ROAM before TARGETING sees them.

**Default is precise BSSID matching** — matching by SSID name alone is a trap: it
would skip EVERY AP sharing that name (e.g. all `NETGEAR`/`linksys` in range),
including ones you ARE authorized to test. So:
- Adding by `sw` index or MAC creates a **precise BSSID entry** (one exact AP).
- Whitelisting a whole network by NAME is a **separate, explicitly-typed, warned**
  option — never the default.

**File `/apps/pwn/whitelist.csv`** — typed rows `type,value,label`:
```
bssid,AA:BB:CC:11:22:33,MyHomeNet     # matches ONLY this exact AP; label is cosmetic
ssid,MyHomeNet,                        # matches ALL APs named MyHomeNet (broad, opt-in)
```
**Match rule:** `bssid` rows match by exact BSSID; `ssid` rows match by exact name.
An AP is whitelisted if it matches any row of either type.

**CLI subcommands** (at the prompt; `wl` resolves an `sw`-scan index like `ws`/`pm`
via `wifiFunctions.getNetworkBSSID/SSID(idx)`, needs a prior `sw` scan):
- `pwn wl` / `pwn wl list` — show the whitelist
- `pwn wl add <idx>` — **add the exact AP #idx from the last `sw` scan** → a precise
  `bssid` row (its SSID stored only as a cosmetic label) ← the requested flow
- `pwn wl add <bssid>` — add a raw MAC → precise `bssid` row
- `pwn wl add ssid <name>` — whitelist ALL APs with that name → `ssid` row; prints a
  warning ("will skip every AP named <name>") since it's the broad one
- `pwn wl rm <idx>` — remove whitelist row #idx
- `pwn wl clear` — empty it

**Mesh / whole own-network:** add each of its APs by BSSID from the scan (precise),
or knowingly use `add ssid` if you accept the broad match. Precise is the default so
you never silently skip an authorized target.

## 7. UI (foreground)

- Pet sprite (reuse buddy engine) + mood label.
- Stat block: `Ch  APs  HS  PMKID  PWNED  up:HH:MM:SS`.
- Ticker line: last event (`+HS TP-Link_A2`, `PWNED "Home5G" = ******`, `cracking 41/100…`).
- Controls: `[m]` cycle mode (active→stealth→passive) · `[c]` channel plan (1/6/11 ↔
  full) · `[k]` backlog crack on/off · `[s]` force-save · `[q]` quit.
- Mode + stealth state shown in the stat block (e.g. `mode:STEALTH`).
- Lock-aware (`consumeJustUnlocked()` redraw) + `displayManager.isBlocked()` guards
  (buddy already does this).
- `g_covert`: silence sound + suppress popups, keep capturing/cracking (undercover
  passive-tools pattern).

---

## 7a. Stealth — evading pwnagotchi / deauth detectors

Detectors (nzyme, Kismet, ESP32Marauder "Detect Pwnagotchi", and AL-ANQA's own
`wguard`) catch pwnagotchis two ways: (1) the **pwngrid advertisement beacon**
(source MAC `de:ad:be:ef:de:ad`, JSON in the ESSID) — their highest-confidence,
zero-false-positive signature; and (2) **attack behavior** (broadcast/flood deauth,
active-scan probes).

**We are invisible to (1) by design** — `pwn` never advertises, no pwngrid, no
beacons. HARD RULE: never emit the `de:ad:be:ef:de:ad` beacon (and if an ESP-NOW
grid is ever added, keep it off the beacon channel, never in that format).

Stealth is therefore about (2) — a three-level ladder, selectable at launch
(`pwn` / `pwn stealth` / `pwn passive`) or via `[m]`:

**Level 0 — ACTIVE (default `pwn`):** best capture, loud. Aggressive deauth. For
authorized / own-network use where detection doesn't matter.

**Level 1 — STEALTH (`pwn stealth`):** blend into normal traffic —
- **Passive scanning only** — listen for beacons; NEVER send probe requests (active
  `WiFi.scanNetworks()` transmits probes = a presence tell). Cheap, big win.
- **PMKID-first** — a single **association** request (looks like an ordinary client
  joining) grabs the PMKID with NO deauth on APs that leak it. Deauth is fallback-only.
- **No broadcast deauth, ever** — it's the #1 IDS trigger (`wguard` flags `BCAST
  DEAUTH` specifically). Only **directed** deauth to one specific STA, 1–2 frames,
  when a client is actually present.
- **Rate-limit + jitter** — cap attacks/minute; randomize timing so there's no
  rhythmic ~50 fps deauth signature.
- **Randomized LA-MAC** per attack burst (via `mac_util.h randomLaMac`) — don't tie
  activity to one identity.
- **Jittered dwell / hop order** — break the rigid 1/6/11-every-Ns fingerprint.

**Level 2 — PASSIVE (`pwn passive`):** zero TX. Undetectable by any RF-signature IDS
(only physical/visual detection possible). Captures only what natural client activity
leaks — but the **CPU-only cracker emits zero RF**, so the pet can sit silent,
harvest a trickle, and spend its time invisibly grinding existing loot.

**HONESTY (per user prefs): stealth ↔ yield tradeoff.** Level 1 captures noticeably
less than Level 0 (PMKID-mostly, no broadcast deauth); Level 2 far less again. The UI
must state this (`stealth: lower capture rate, minimal RF signature`).

**Decision pending:** is STEALTH (Level 1) a v1 feature, or v1-lite = just ACTIVE +
PASSIVE with the full stealth ladder in a fast follow? Passive scan + PMKID-first +
no-broadcast-deauth are cheap and high-value → lean **STEALTH in v1**.

---

## 8. Open questions

**RESOLVED (2026-08-10):**
- Q1 core extraction → DRY extract, non-breaking (see Decisions block).
- Q2 command slot → standalone `pwn`/`pw`.
- Q3 cracking source → Option B, session+backlog+ledger (§6a).
- Q4 modes → both active & passive in v1.

**RESOLVED (2026-08-10 cont.):**
- A. `.cap` granularity → **per (BSSID,SSID)**, filename `<BSSID>_<SSID>.cap`
  (sanitized), one file per network instance. §6/§6b.
- B. Channel dwell vs. capture race → **do NOT hop mid-capture**: a
  capture-in-progress extends the dwell (`capture in progress → hold channel until
  done or CAP_MAX_MS timeout`). §4 TARGETING gets this guard.
- §6c smart ordering → **v1** (full priority queue).

**Still to confirm during build:**
- **C. WPA3 v2 dependency** — do not start v2 WPA3-awareness until `wpa3down` is
  HW-tested.

---

## 9. Build order (once plan is agreed)

1. Read `handshake_capture.cpp` + `pmkid_attack.cpp` + `capcrack.cpp` internals;
   decide core-extraction boundaries (Q1).
2. Factor `hsCaptureOnce()` / `pmCaptureOnce()` / `crackCapFile()` cores; retest
   `ws`/`pm`/`cc` unchanged (CI compile-gate).
3. New module `wifi/attacks/pwn/pwn.{cpp,h}` — free fn `runPwn(char*)` (wardrive/
   karma pattern). State machine + roam + target table.
4. Wire capture cores into TARGETING; wire crack core into IDLE.
5. SD layout + `ensureTreeStructure()` + README map.
6. UI (buddy sprite reuse + stat/ticker + controls + lock/covert guards).
7. Register `pwn`/`pw` (WiFi), man page, docs EN+AR, NOTICES (credit Pwnagotchi
   concept + Hash Monster prior art; no code copied).
8. Static-review, hand off to CI + user HW test (no `pio` in-session — user pref).

---

## 10. Prior art / attribution (for NOTICES)
- **Pwnagotchi** (evilsocket) — concept, mood/pet framing, capture algorithm.
- **ESP32-WiFi-Hash-Monster** (G4lile0) — ESP32 pwnagotchi prior art, channel-hop
  strategy (1/6/11 + smart-hop).
- **minigotchi-ESP32** (dj1ch) — ESP32 pwngrid prior art (noted, not used in v1).
- Method only; no code copied. AL-ANQA's capture+crack cores are its own (`ws`/`pm`/`cc`).

**The genuinely-first claim to stand behind:** *autonomous on-device capture→crack
→PWNED loop* — no other pwnagotchi (any platform) recovers passwords on the device
unattended.

---

## 10a. Build status (2026-08-10 first cut)

**DONE (static-reviewed, needs compile + HW test):**
- `wifi/core/cap_parse.h` — extracted `CrackJob`+`parseCap` from capcrack (DRY);
  capcrack refactored to use it (retest `cc` compiles).
- `SD_DIR_PWN` + `ensureTreeStructure()` entry + README map (TODO README text).
- `wifi/attacks/pwn/pwn.{h,cpp}`, `runPwn()`, registered `pwn`/`pw` (WiFi).
- Ledgers: cracked.csv / captured.csv / progress.csv / whitelist.csv — full I/O.
- Whitelist: RAM-cached hot-path check + `pwn wl list|add <idx|bssid>|add ssid <name>
  |rm <n>|clear` (add-by-`sw`-index works).
- **Crack subsystem (the headline):** `pwnCrackCap()` — resume cursor (progress.csv),
  priority ordering (prior related passwords by SSID/BSSID → built-in defaults →
  wordlist), verified via `wpacrack::verify*`; idle loop rotates past solved/exhausted.
- Capture: roaming promiscuous EAPOL(M1/M2)+PMKID+beacon sniffer, per-(BSSID,SSID)
  `<BSSID>_<SSID>.cap` (synth beacon + M1 [+M2]), dedup, GDMA-safe writes.
- Modes active/stealth/passive (+`[m]` cycle, `full` chans). Deauth TX for active/
  stealth. Pet UI (text faces + stats + ticker), lock-aware.

**UI polish DONE (2026-08-10, compiles):** animated blinking mood-face (mood-coloured,
size-2), signal bar from real per-frame RSSI, channel/mode/uptime line, HS/PMKID/PWNED
counters, event ticker, live-state controls footer, **PWNED green celebration flash**,
action toasts. **All controls wired:** `[m]` mode · `[c]` channels 1/6/11↔1-13 · `[k]`
backlog↔session-only crack · `[s]` stats snapshot · `[q]` quit. Per-frame RSSI now
carried through the ring → real signal + `PWN_RSSI_CUTOFF` applied; `[k]` session-only
uses `s_sessionCaps`.

**DONE this batch (2026-08-10 cont.):**
- **man page** `man pwn`/`pw` (man_pages.cpp) — syntax, modes, whitelist, keys, files.
- **NOTICES #23** — Pwnagotchi / Hash-Monster / minigotchi concept+prior-art credit
  (no code copied); the on-device-crack differentiator stated.
- **GPS geotag** — `captured.csv` now `...,rssi,lat,lon`; Plus-gated, READ-ONLY (only if
  the GPS task is already running — never auto-starts it, avoiding the first-fix NVS
  flash-write vs WiFi-DMA hazard). Empty lat/lon on base board / no fix.
- **/apps/README.txt** — `pwn/` folder→command map entry added.
- Exhausted-cap fast-skip: already handled (pwnCrackCap early-returns -1 on exhausted
  cursor before re-reading the wordlist).

**DONE this batch (2026-08-10 cont. 2):**
- **Stealth directed deauth** — cb samples 1/8 non-EAPOL DATA frames (EAPOL prioritised)
  → per-AP client table `s_cli[]`; stealth sends ONE directed deauth to a discovered
  client (no broadcast storm), stays quiet if none seen. Active still broadcasts.
- **Byte-offset wordlist cursor** — SD wordlist resume is now an O(1) `seek(offset)`
  (no per-slice line re-skip); exhausted = offset >= file size.
- **Web docs** — `docs/pwn.md` (EN) + `docs/ar/pwn.md` + index/wifi-attacks/AR-index.
- **CLAUDE.md** — full module section + command-list entry.
- **Bug-review pass + comma-SSID fix** — full read-through; found the ledger CSVs
  (cracked/captured/progress) key on SSID via comma-split, so a comma in an SSID would
  break dedup/priority/cursor. Fixed with `csvSsid()` normalising SSID → CSV-safe for
  ledger keys + logs ONLY (the real SSID is still used for the PBKDF2 crypto). Verify
  path untouched. (Remaining edge: `wl add ssid <name>` with a comma in the name — a
  triple-edge case, left as-is; the broad-match is already warned.)

**CRITICAL CAPTURE FIX (2026-08-15) — "runs fine but never catches a handshake":**
- **Root cause:** `runPwnSession` set `WiFi.mode(WIFI_STA)` (bare STA, UNassociated) and injected
  deauth + grid via `esp_wifi_80211_tx(WIFI_IF_STA,…)`. On ESP32-S3 a raw 80211_tx only actually
  hits the air when the **AP interface is up** (or the STA is associated). Roaming pwn is neither →
  **the deauth frames never transmitted** → no client was ever kicked → no reconnect → no EAPOL →
  HS counter stuck at 0. Passive natural capture is rare, so it looked like "nothing works".
- Confirmed against **our own proven `ws`/`da`/`karma`** (all `WIFI_MODE_APSTA` + hidden `softAP` +
  inject on `WIFI_IF_AP`; `deauth_functions.cpp:457` literally comments "AP interface needed for
  80211_tx injection"), **Bruce** (`wifi_atks.cpp`: APSTA + `softAP(WIFI_ATK_NAME,…,1,4,false)`),
  **Marauder** (`esp_wifi_set_mode(WIFI_MODE_AP)`, `ssid_hidden=1`), and the ESP-IDF API doc
  (ifx must match mode). pwn was the lone STA-only outlier.
- **Fix:** `WiFi.mode(WIFI_MODE_APSTA)` + `WiFi.softAP("x",nullptr,1,1,0,false)` (hidden, 0 clients =
  minimal tell); capture our own AP MAC (`s_apMac`) and ignore its beacons in `drainOne` so we never
  target ourselves; teardown adds `WiFi.softAPdisconnect(true)` and moves `learnSave()` to after the
  radio is fully idle. Roam still hops via `esp_wifi_set_channel()` each dwell (softAP home channel is
  cosmetic). SD flushes keep `ScopedPromiscPause` (a client-less hidden softAP is lighter DMA than an
  associated STA — matches the arpspoof/isoscan mid-session-write precedent).
- **Reuse over reimplement (rule 5b, user's steer):** pwn's hand-rolled single-frame `sendDeauth` was
  DELETED and replaced with the **proven `DeauthAttack` primitive** that `da`/`ws` use (HW-verified
  100%). It sends **both deauth (0xC0) and disassoc (0xA0)** with randomised seq, 3× each with 1 ms
  gaps — far more reliable at kicking a client than the old one deauth-only frame with seq=0. Exposed
  a new public `DeauthAttack::sendDirectedBurst(bssid,client)` (mirrors `sendBroadcastBurst`, both
  directions AP↔STA) for the stealth/targeted path; active uses `sendBroadcastBurst`. Grid advert TX
  also moved to `WIFI_IF_AP`. So even beyond the interface fix, the deauth frames themselves are now
  the proven ones — this is the bigger reason capture should now work.
- **▶ HW-TEST WATCH ITEM:** channel-hop-while-softAP-up is new to our repo (ws/da/karma all use a
  FIXED channel). Marauder/Bruce hop with an AP up fine, and our STA is idle + AP has 0 clients so
  `esp_wifi_set_channel` should hold — but confirm on HW that it still hops 1/6/11 while capturing.

**CRACK SCHEDULING REWORK + GDMA read fix (2026-08-15):**
- **Fix #2 (GDMA read hole):** the idle cracker reads the SD (dir list, `.cap`, wordlist) while
  promiscuous + softAP were live — writes were `ScopedPromiscPause`-guarded but the READS weren't.
  Now the whole crack batch (dir scan + `pwnCrackCap` calls) runs inside a `ScopedPromiscPause` (RX
  firehose paused; softAP stays up but near-idle). Cracking needs no radio, so the deaf window only
  ever occurs when there's nothing worth hearing.
- **"Crack only in free time" (user's model, replaces the old trickle throttle):** cracking is now
  gated on `preyInRange==0 && !s_cap.active` — **zero cracking while any un-captured AP is in range or
  a capture is in flight** (capture is always the priority). When the air is quiet it cracks ONE short
  **batch** (`PWN_CRACK_SLICE_MS` 500→300ms ≈ a handful of candidates), then drops back to the loop to
  drain + re-assess the air before the next batch — so the instant a target appears it bails to the
  deauth/capture phase. `crackAt` backoff: `didWork ? 0 : 12s` (crack back-to-back while there's
  progress; back off once the backlog is solved/exhausted so it can't re-scan SD every loop).
- **Startup warm-up:** cracking is suppressed for the first 12s (`now-t0Session>12000` ≈ one 1/6/11
  sweep) so a fresh session with a backlog spends its first sweep DISCOVERING nearby APs (it's deaf
  while cracking) instead of immediately going heads-down on old loot.

**WHITELIST PARSE BUG — a whitelisted AP got captured (2026-08-15, HW-found):** user's
`whitelist.csv` had MACs written with COMMAS between octets (`bssid,80,C5,48,21,55,C3,label`) —
a natural hand-edit mistake since the file is itself CSV. `whitelistLoad` split on commas and took
the field between the 1st/2nd comma as the BSSID (`"80"`), so `whitelisted()` never matched the real
MAC → the "protected" AP (`BeSpot32F3_2.4`) was deauthed + captured. A silently-failing whitelist is
the worst outcome for an authorization-safety feature. **Fix:** `wlNormalizeMac()` — tolerant parse
that accepts `:`/`,`/`-`/space separators and missing leading zeros, reads exactly 6 octets, ignores
the trailing label, emits canonical `XX:XX:XX:XX:XX:XX`. Canonical colon files (what `wl add` writes)
still work. `ssid` rows still take the name up to the next comma.

**Exhausted-cap skip-set (2026-08-15, user ask):** correctness was already there (the resume
cursor persists `next_index >= wordlist_count` in `progress.csv`, so a wordlist that can't crack a cap
returns -1 and is skipped this session AND across reboots). Added `s_crackSkip` — an in-RAM set of
caps that returned -1 this session (solved/exhausted/unparseable) so they're not even re-opened, and a
wordlist with no password for a cap can't keep stealing crack cycles. NOTE re the paired "keep hunting
while looking for other APs" idea: already the behavior — cracking is gated on `preyInRange==0` (never
runs while any un-captured AP is in range), the roam keeps HOPPING every dwell during idle so new APs
are discovered, and any un-captured AP is deauthed the instant it's seen (pass-1 broadcast if no client
known). Blind deauth during true idle would only re-hit captured APs (no new HS) = pure RF noise, so it
was intentionally not added.

**TODO / follow-ups:**
- Stealth PMKID-first-via-ASSOC — actively associate to solicit a PMKID without any
  deauth (quietest). Not yet: PMKID currently comes from natural/deauth-forced M1.
- buddy-sprite UI (text-face is serviceable; real sprite is the eventual upgrade).
- `g_covert` gate: N/A for now — `pwn` is a foreground command, not run under the cover,
  and emits no sound; revisit only if a `pwn bg` mode is added.
- Base **T-Deck** env build (CI) + **HW test** — Plus compiles; base + hardware pending.

## 11. Feature parity checklist (vs Pwnagotchi + Hash Monster + minigotchi)

Goal: `pwn` does everything an existing pwnagotchi does, unless deliberately excluded.

**Covered (v1):**
- Passive handshake (EAPOL) capture · deauth-forced handshake · assoc→PMKID
- Channel hop (auto 1/6/11 + smart-hop) · signal cutoff · save pcap to SD
- Pet face + moods · unattended/auto mode · session stats
- **Whitelist** (§6d) — never-touch APs, add by `sw` index / MAC / SSID
- GPS geotag of captures (Plus) — parity item, pulled to v1 (`captured.csv` lat/lon)

**Better than any existing ESP32 pwnagotchi:**
- On-device cracking (crack loop + resume cursor §6a) — they capture-only
- Smart password guessing (prior related passwords §6c) — nobody has this
- Stealth ladder (§7a: active/stealth/passive) — vs basic "incognito"
- No wpa-sec upload needed — we crack locally

**Deliberately EXCLUDED (with reason):**
- pwngrid peer advertising — the `de:ad:be:ef:de:ad` beacon detectors catch (breaks
  stealth). HARD RULE: never emit it.
- RL "AI" auto-tuning — theater on MCU; honest heuristics instead
- Beacon spam (minigotchi) — not a capture feature; `bf`/`bs` already exist

**Bonus (turns stealth research into a feature):**
- Pwnagotchi DETECTION — spot other pwnagotchis by their pwngrid beacon (Hash
  Monster & minigotchi both do this). Fits `wguard` or a passive readout in `pwn`.
  Candidate for v1 or a fast follow.

---

## 12. pwn-grid — the social pack (private AL-ANQA peer greeting)

**Status:** ✅ v1 BUILT (2026-08-11) — presence + scoreboard. NOT HW-tested (needs 2
decks; user has 1 for now). Prefix `A2:9A:0A`; TX gated active/passive, stealth dark;
RX all modes. `GridAdv{magic"ANQG",ver,name[12],pwned,hs,pmkid,uptimeMin}` in a
beacon-format frame at offset 36; detected in the sniffer cb (kind 3) by the SA
prefix. UI: `g<N>` peer count on the mode line + "met <name>" ticker + SOCIAL phoenix
pose. Name override `/apps/pwn/grid.conf` (`name=`). v2 (dedup/handshake-swap) later.
**Idea:** two+ AL-ANQA decks running `pwn` recognise each other on the air, show each
other's stats, and (v2) split the work. The multiplayer/"make friends" side of a
pwnagotchi, done AL-ANQA's way.

### 12a. Identity — branded but unique (user's idea)
- **Grid MAC = fixed AL-ANQA prefix (3 bytes, LA-bit set) + this device's own last 3
  MAC bytes.** e.g. `A2:9A:0A:XX:XX:XX`. First half = "an AL-ANQA pwn pet" (peers filter
  on it), second half = unique per device. (Can't literally spell "ANQA" — N/Q aren't
  hex, like Pwnagotchi's all-hex `de:ad:be:ef`.) Final prefix bytes TBD at build.
- **Human name** auto-derived from the suffix: `ANQA-<last2 MAC hex>` (e.g. `ANQA-7F3A`),
  shown on screen. Optional override in `/apps/pwn/grid.conf`.

### 12b. Grid FOLLOWS THE MODE — final table (user decision)
A fixed recognisable prefix IS a detectable signature (same as pwngrid's
`de:ad:be:ef`), so grid **broadcast** must never happen in the go-dark mode. Grid is
automatic (follows the mode; no separate command / `[g]` toggle — `[m]` cycles it):

| Mode | Capture (WiFi) | Grid broadcast | Grid listen |
|---|---|---|---|
| **active**  | broadcast deauth (loud)     | ✅ ON  | ✅ |
| **passive** | sniff-only, NO attack       | ✅ ON  | ✅ |
| **stealth** | quiet DIRECTED deauth       | ❌ OFF | ✅ |

- **active + passive broadcast the grid** (both are "social/visible"). NOTE: passive
  therefore gives up its old *zero-TX* property — it now emits the ANQA beacon.
- **stealth is the single go-dark mode**: no ANQA prefix ever on the air → undetectable
  as an AL-ANQA pet; it still captures via *quiet directed* deauth (user's pick: quiet
  capture, not a total ghost) and still LISTENS to the grid.
- **RX in ALL modes** — receiving is passive (parse frames pwn already sniffs), so a
  stealth deck SEES other pets one-way **without revealing itself**.
HARD RULE: our own private prefix, NEVER the `de:ad:be:ef:de:ad` pwngrid format.

### 12c. Transport — reuses pwn's existing radio path (no ESP-NOW API, no channel-sync)
- **TX:** broadcast a small AL-ANQA grid frame (ANQA-prefixed SA + payload) on the
  current roam channel via `esp_wifi_80211_tx` — same primitive as the deauth. ~every 3s.
- **RX:** detect a peer's frame in pwn's EXISTING promiscuous cb (match the ANQA prefix)
  → ring → peers table. No `esp_now_*` (avoids the ESP-NOW-recv-vs-promiscuous conflict).
- **No channel sync needed:** both hop 1/6/11 → coincide ~1/3 of the time → a greeting
  lands every few seconds. Good enough for presence.

### 12d. Wire format (draft)
`GridAdv{ magic[4]="ANQG", ver, name[12], pwned, hs, pmkid, uptime_min }` (~26 B),
carried in a vendor-specific frame. v2 adds a compact captured-BSSID digest (bloom or
truncated-hash list) for shared dedup.

### 12e. v1 scope — presence + scoreboard
- Peers table `GridPeer{ name, pwned, hs, rssi, lastSeenMs }` (drop after ~30s silent).
- UI: a `grid: N peers` readout on the pwn screen (below-box strip or right column) +
  cycle peer names/scores; **phoenix does a brief social pose** when a NEW peer appears.
- Grid is automatic (follows the mode, §12b) — no `pwn grid` command, no `[g]` toggle.
  Broadcast only in active; RX (peer detection + display) in every mode.

### 12f. v2 — cooperation (after v1 proven)
- **Shared dedup:** advert carries a digest of captured BSSIDs → skip APs a peer already
  owns (two pets divide the area instead of double-cracking).
- **Handshake swap:** hand a `.cap` to a peer (multi-frame transfer) so loot is pooled.

### 12g. Files / config
- `/apps/pwn/grid.conf` — `name=` override, `grid=on|off` default.

### 12h. Open questions
- Final ANQA prefix bytes (LA-bit set; memorable).
- Frame type: vendor action frame vs a data frame w/ our SNAP — pick what promiscuous
  reliably delivers on this stack.
- Does `esp_wifi_80211_tx` of our advert interfere with the concurrent deauth cadence?
  (Both are just TX bursts; expected fine — verify.)

## 13. Adaptive roaming — tabular Discounted-UCB learner (DEFAULT roam; `pwn basic` disables it) — ✅ HW-verified

Optional, gated behind the `ai` keyword (`pwn ai [debug] [full] [stealth|passive]`),
toggle live with `[a]`. Brings the Raspberry-Pi Pwnagotchi's *learned* channel behavior
to the ESP32 with a lightweight bandit instead of a neural net.

### 13a. Why (research, 2026-08-12)
- Real Pwnagotchi = A2C neural net, but its action space is just timing/selection knobs
  (`recon_time`, `hop_recon_time`, `_channel_N` on/off bits) and its reward = handshakes
  captured per epoch (`evilsocket/pwnagotchi/ai/gym.py`).
- Every ESP32 pwnagotchi hops **blindly**: minigotchi-ESP32 `Channel::cycle()` =
  `random(numChannels)`; ESP32 Marauder = fixed 1s timer; Hash Monster = manual button.
  **None adapt.** So a learned channel selector is a genuine ESP32 first (stacks on the
  on-device-crack first).
- Non-stationary bandit theory: **Discounted-UCB** (discount old rewards by γ) is the
  resource-efficient answer for a slowly-changing single area; our EMA *is* D-UCB.
  Sources in NOTICES #23.

### 13b. Model
- State = channel (the meaningful axis, mirrors Pwnagotchi's per-channel bits).
- Per channel: discounted reward sum `value` + discounted visit `count` (γ=0.95 →
  early hits persist ~20 visits). `mean = value/count`.
- **Reward** per dwell: capture +10, M1 +3, new client +2, new AP +1, plus a **standing
  presence reward** +0.25 per un-captured AP on the channel (cap 6) so a channel that
  *hosts* prey keeps out-scoring an empty one every visit — not just on first discovery
  (the discovery-only reward decays to 0 once an area is mapped → flat, nothing to learn).

### 13c. Selection — weighted-random, NOT argmax
- Pick next channel with probability ∝ `mean + floor`. Argmax-UCB was tried and **camps
  100% on one channel** (a bounded exploration bonus can never catch a high mean) — HW
  observed 87/87 picks on one channel. Weighted-random (probability matching) gives the
  productive channel most visits while a **floor** guarantees every channel a nonzero
  share → no lone/new AP is ever starved.
- **Scout floor**: channels that currently host un-captured prey get the full `floor=0.5`;
  **empty** channels get a tiny `scout=0.1` so full-13 stops wasting ~⅓ of its time on
  dead air, while still periodically peeking for a new/odd-channel AP. Cold start (nothing
  discovered yet) → all scout → uniform scan → discover → concentrate. Self-correcting:
  once every AP on a channel is captured it reverts to scout.
- **Adaptive dwell**: a productive channel earns up to ~2× the base dwell (Pwnagotchi's
  `recon_time` lever).

### 13d. Persistence & area changes
- `/apps/pwn/learn.csv` (`channel,value,count`), **loaded ×0.5-decayed** so old-area
  knowledge is a weak prior that washes out fast if you've moved (cross-session D-UCB
  discount). **2-min autosave** so a crash/power-off can't cost a session; also saved on
  quit. `[a]` toggle keeps the in-RAM table (never reloads the stale file mid-session).

### 13e. Debug trace — `pwn ai debug`
- `/apps/pwn/ai_debug.log`: session header (all params) + one line per hop showing the
  leaving channel's reward, every channel's `mean` + pick-`prob%`, the pick, and the
  dwell. Per-hop flush (survives power-off). The instrument used to tune all of the above.

### 13f. Capture-completion fixes (found via the learner runs)
The learner exposed that **full-13 captured slowly not because of channel choice but
because the 4-way rarely completes in a short window**. Fixes (help both modes):
- **M1→M2 window 3s→6s** — 3s dropped handshakes that the sparse full-13 revisit couldn't
  re-try in time.
- **Hot-channel hold** (`PWN_HOT_MS`=12s, cap `PWN_HOT_MAX`=30s) — on an M1 (a client
  handshaking *now*), STOP roaming and hold that channel, re-deauthing to force the 4-way,
  until captured or the window lapses. Turns "saw M1 5× over 21 min, caught it on the 6th"
  into catching it on the first/second. Released instantly on capture.
- **Cracking yields to the hunt** — while un-captured prey is in range, crack only a
  trickle (one ~500ms slice / 5s ≈ 10% CPU); crack hard (every 0.7s) only when the area is
  quiet. Capture > crack (captures are always crackable later with `cc`/PC).

### 13g. HW test status (2026-08-12)
- ✅ **Learner proven**: weighted+scout-floor concentrates on productive channels, keeps
  coverage, self-corrects after a capture; `learn.csv` shows correct per-channel means.
- ✅ **Full-13 capture works** (~14–26 min for a single on-band target; high stochastic
  variance). **1/6/11 default ≈ 2 min** — stays the fast mode; **full is thorough/discovery**
  and inherently trails it for a target on the common channels.
- ⏳ **Hot-channel hold + crack-yield: built, NOT yet HW-timed** — next run measures whether
  hot-hold lands the handshake on the 1st/2nd M1 (target: cut full-13 capture time hard).
- Honest ceiling: no tuning makes 13 channels as fast as 3 for an on-band target; the AI's
  real payoff is making `full` mode *viable/thorough* + learning a fixed area over sessions.

### 13h. Tunables (all in pwn.cpp)
`PWN_LEARN_GAMMA`=0.95 · `PWN_LEARN_FLOOR`=0.5 · `PWN_SCOUT_FLOOR`=0.1 ·
`PWN_PRESENCE_W`=0.25 (`PWN_PRESENCE_CAP`=6) · `PWN_REWARD_CAP`=4 (dwell) ·
`PWN_HOT_MS`=12000 · `PWN_HOT_MAX`=30000 · crack trickle 5s / hard 0.7s.

## 14. Maximizing handshake/PMKID capture (research 2026-08-15, NOT yet built)

Research against the reference tools (**hcxdumptool** — the gold standard for capture; **risinek
esp32-wifi-penetration-tool**; ESP-IDF WiFi docs). Goal: capture as much crackable key material as
possible. Ranked by impact × ESP32 feasibility. Sources for NOTICES: HackingArticles PMKID writeup,
risinek tool docs, ESP-IDF `esp_wifi` reference — methodology only, no code copied.

**Context from HW runs:** a residential-AP run got **1 passive capture in 16 min** — most residential
APs had no active client during the dwell, so there was nothing to deauth and nothing to passively
catch. The BeSpot run saw an M1 flood but few completed captures (single-buffer collision). Both point
straight at the two top items below.

### 14a. #1 lever — ACTIVE PMKID solicitation (clientless) — biggest gap
- **Why:** hcxdumptool's whole edge over passive tools is that it *actively solicits* PMKID/EAPOL
  instead of waiting. **PMKID is clientless** — it rides in **M1 (AP→client)**, needs no connected
  client and no 4-way completion; most modern routers leak it. Crack = hashcat **mode 22000** (same as
  handshakes).
- **How:** initiate a connection to the AP (even with a dummy/wrong PSK) → AP replies M1 w/ PMKID.
  One association attempt (~1-2 s), no waiting for a human.
- **pwn today:** only parses PMKID *passively* if an M1 happens to fly by (`drainOne`, the KDE walk).
  It never SOLICITS one → clientless APs (most of a residential area) are currently uncapturable.
- **Why it matters HERE (HW-confirmed 2026-08-15):** the diag runs proved the deauth path works
  (`txf=0`) but is starved — `dD=0 dB=60 m1=0` in a uniformly weak area (every AP -87…-95, all below
  the -80 deauth cutoff, the only 7 clients on those weak APs). Deauth has NO valid target here.
  PMKID solicitation is **clientless and not gated by the deauth cutoff**, so it's the one path that
  could capture this environment.

- **CHOSEN APPROACH — raw-inject assoc + promiscuous M1 capture (Approach B).** Research settled it:
  **Sablina-Tamagotchi-ESP32 does exactly this** ("native promiscuous mode + raw frame injection" for
  clientless PMKID), **HaleHound-CYD** "extracts PMKID from M1 RSN IE", and `esp32free80211` proves
  arbitrary 802.11 injection — so it's a PROVEN ESP32 route, not a guess. It also **reuses the most
  pwn code**: the M1/PMKID KDE parser already lives in `drainOne`, plus the `.cap` writer + synth
  beacon + crack loop. And it **stays in APSTA+promiscuous** (no radio teardown → fits the roam,
  GDMA unchanged). Method/prior-art credit → NOTICES (hcxdumptool concept; Sablina/HaleHound ESP32
  prior art; no code copied).

- **Frame sequence (per target AP, on its channel):**
  1. Pick a spoofed client MAC (LA-bit, random via `mac_util randomLaMac`) and **`esp_wifi_set_mac(WIFI_IF_STA, mac)`** — this is the load-bearing bit: the HW auto-ACKs frames addressed to our own STA MAC, so the AP will proceed past auth/assoc to M1 (the ACK subtlety `karma roguehs` also solves via set-mac).
  2. Inject **Authentication** (open, seq 1) via `esp_wifi_80211_tx(WIFI_IF_AP,…)` (AP iface = reliable TX, per the capture-fix finding). AP → Auth-Resp to our STA MAC (HW ACKs).
  3. Inject **Association Request** carrying a valid **RSN IE** advertising PMKID-capable (AKM PSK, RSN caps). AP → Assoc-Resp (HW ACKs) → then **EAPOL-Key M1 with the PMKID KDE** (`DD…000FAC04 <16B>`).
  4. **Promiscuous captures M1** → existing `drainOne` PMKID walk extracts it → `flushCapture` writes `<BSSID>_<SSID>.cap` (PMKID type) → same crack pipeline. STA MAC for the hc22000 = our spoofed MAC (known).
  5. Restore the STA MAC (or rotate next target). Timeout ~300–500 ms waiting for M1; give up + move on if none.

- **Phasing in the roam — a "PMKID sweep":** target selection = uncaptured APs that are **clientless
  or below the deauth cutoff** (exactly what deauth can't get). A few solicits per dwell, then resume
  roaming. Runs in ACTIVE + a new gating so PASSIVE/stealth stay quiet (assoc-inject IS a TX tell).

- **Fallback if B is unreliable on real APs:** real-stack connect (`WiFi.begin(ssid,dummy)`) + read
  PMKID from `gWpaSm` (netspy's technique; framework-pinned offset) — reliable auth/assoc but tears
  down promiscuous per AP and the offset is fragile. Only if the RSN-IE/ACK dance in B doesn't pan out.

- **▶ SPIKE FIRST (make-or-break, before the full sweep):** prove that ONE injected association to a
  known AP yields a captured **M1-with-PMKID** in promiscuous on the actual T-Deck. If that single
  round-trip works, the sweep + targeting is straightforward orchestration; if it doesn't (RSN-IE
  rejected / no ACK / no PMKID), fall back to the connect approach. Don't build the whole phase until
  the spike lands.

- **HONESTY:** only APs that actually **leak** PMKID (most WPA2-PSK, but not all); **WPA3-SAE has no
  PMKID** via this path; **PMF does NOT block it** (association isn't a protected mgmt frame); a very
  weak AP (-90) may not answer the assoc at all (same physics ceiling as deauth). So PMKID widens
  coverage massively but isn't universal.

- **Verdict:** highest-ROI capture change. Converts the whole clientless-AP population from
  "uncapturable" to "one association away." Supersedes the old "Stealth PMKID-first-via-ASSOC" TODO
  in §10a.

### 14b. #2 — Rogue-AP capture for deauth-resistant / clientless-reconnect clients
- risinek notes broadcast deauth is unreliable ("some devices ignore broadcast deauths"). Its most
  ROBUST method is the **rogue AP**: clone the target SSID+BSSID+channel; a client reaching for the
  real AP associates to US and completes **M1+M2 keyed by the real PSK** → crackable half-handshake.
- **pwn's sibling `karma` already has this engine (`roguehs`, HW-verified)** — reuse it (rule 5b) for a
  stubborn uncaptured client-AP. Also beats PMF APs that ignore deauth. Cost: one AP at a time, noisy.

### 14c. #3 — Multi-slot capture buffer (fix the single-buffer collision)
- pwn has ONE `s_cap`; a different BSSID's M1 clobbers a half-captured one (`drainOne`). On a busy
  channel → many M1s, few completed captures (the BeSpot symptom). Replace with a **small array of
  in-progress captures keyed by BSSID** so concurrent 4-ways complete independently. Direct yield win
  on crowded channels.

### 14d. Already done / lower-effort
- Directed deauth both directions (AP↔STA) + disassoc in bursts — ✅ (the `DeauthAttack` reuse).
- Correct injection iface `WIFI_IF_AP` + softAP — ✅ (2026-08-15 capture fix).
- Hot-hold after M1 to catch M2 — ✅. M1→M2 window 6s — ✅.
- Broadcast deauth to kick ALL clients periodically (catch un-sampled clients) — partial (pass-1).
- **Recon-driven targeting:** confirmed-client AP → directed deauth; no-client AP → PMKID solicit
  (don't waste deauth budget on a clientless AP — it can't hand over a 4-way, only a PMKID).
- **Diag counters (2026-08-15):** each `ai_debug.log` hop line tails `dD=<directed bursts> dB=<broadcast
  bursts> txf=<driver TX rejects> m1=<M1 seen> m2=<M2 seen> ap=<#APs> cli=<#clients>`. **Run as
  `pwn ai debug`** (plain `pwn` writes no log). Reading it when m1=0 (no captures):
  - `dD=0` (all broadcast) → no client ever sat on a deauthable AP → the clients are on WEAK APs below
    the -80 cutoff (environment). Fix = PMKID solicit (§14a) or a lower cutoff.
  - `dD>0 & txf>0` → the driver is rejecting frames → real TX bug (softAP/channel).
  - `dD>0 & txf=0 & m1=0` → directed deauth queued fine but no reconnect handshake → frame not landing
    on the victim's channel (softAP-vs-hop mismatch, the watch item) or PMF.
  - **First diag run (session @70817, ~40 min):** `dtx=39 m1=0 m2=0 ap=28 cli=7` — deauth fires at the
    call level, but ZERO EAPOL forced (matches the soak's 4 captures all being weak passive). Leading
    theory: weak environment, clients on sub-cutoff APs. The dD/dB/txf split (added after) will confirm.
  - **✅ HEADLINE (same run): first on-device crack of a real WPA2 AP** written to `cracked.csv`. The
    autonomous capture→crack→PWNED loop is HW-proven end-to-end (a ~-95 dBm passive catch, weak password).

### 14e. Honest ESP32 constraints
- Single radio, half-duplex → PMKID-via-connect must be a phased sweep, not continuous.
- PMF-protected APs resist deauth (can't force reassoc) — but PMKID-via-association still works if they
  leak it.
- Weak APs (≈-90 dBm, e.g. a -92 dBm capture) may not receive our frames at all — physics.

### 14f. Recommended capture strategy (recon → per-AP method)
| AP state | Best method | pwn status |
|---|---|---|
| Has active client | directed deauth → M1+M2 | ✅ (add 14c multi-slot) |
| **No client** | **active PMKID solicitation (14a)** | ❌ biggest gap |
| Client ignores deauth / PMF | rogue-AP half-handshake (14b, reuse karma) | reuse existing engine |
