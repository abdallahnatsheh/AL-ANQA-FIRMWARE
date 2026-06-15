---
name: capcrack — offline .cap cracker app (crack / cc)
description: New WiFi command `crack`/`cc` — parses a libpcap .cap (HS M1+M2 or PMKID) and dictionary-attacks it with SD wordlists / a dir of wordlists / built-in. cwd-relative paths.
type: project
---

# crack / cc — offline WPA/WPA2 .cap cracker (2026-06-15, UNCOMMITTED)

New module `wifi/tools/capcrack/capcrack.{h,cpp}` (own folder; `-I` added to
platformio.ini `[includes]`). Registered in command_manager `setupCommands()` as
`crack`/`cc`, WiFi category, `hasArgs=true`, **`COMP_ANY`** (autocompletes BOTH files and
dirs for either arg — needed since cap/wordlist can be a dir). 59/64 commands used.
Docs: man page (`man crack`); new `docs/capcrack.md` + `docs/karma.md` (karma was
undocumented — created it too) under WiFi Attacks (nav 8/7); added rows to
`docs/wifi-attacks.md` table, README command table + SD-layout, CLAUDE.md command list +
SD layout. (karma `[s]`-save + rogue-handshake/.cap also now in README/CLAUDE/karma.md.)

## What it does
Reads a classic libpcap `.cap`/`.pcap` (linktype 105) from SD, extracts EITHER a
4-way handshake (M1 ANonce + M2 SNonce/MIC) OR a PMKID (KDE in M1 key data), then
dictionary-attacks with the shared `wpacrack` engine (`verifyHandshake`/`verifyPMKID`).
Works on captures from karma, ws, pm, or external tools. Needs an **ESSID** (beacon/
probe-resp) in the cap to derive the PMK — errors clearly if missing.

## UX (the asks)
- **cwd-relative paths**: uses `sdCardManager.resolvePath()` + `getCwd()` — after
  `cd`-ing into the capture/wordlist folder you pass just filenames.
- `cc` (no args) → pick a `.cap` in cwd, then wordlist picker.
- `cc <cap> [wordlist]` → cap can be a file OR a dir (pick inside); wordlist can be a
  file OR a dir (runs every `*.txt`) OR omitted → picker.
- **Multiple wordlists**: pass a dir → all `*.txt` run in sequence; picker offers
  "ALL *.txt in this dir". The **built-in (100)** list is always tried last as fallback
  (unless the user explicitly picks built-in only). This is the "more than one wordlist".
- Live progress (tried, rate, src file, candidate), `q` aborts. Found → green +
  appended to `/apps/capcrack/cracked.csv` (`ssid,password,HS|PMKID`).

## Implementation notes
- Added a **pcap reader** to `wifi/core/pcap_writer.h` (`readGlobalHeader`/`readRecord`,
  handles LE + byte-swapped magic; classic pcap only, not pcapng).
- `parseCap()` walks records via `dot11::parseEapol`/`extractSSID`; M2 EAPOL stored with
  MIC field zeroed (for the HMAC), MACs from frame addrs (M2 toDS: bssid=addr1, sta=addr2).
- PMKID KDE scan: `DD <len> 00 0F AC 04 <16B>` in M1 key data (offset e+99, len at e+97/98).
- `SD_DIR_CAPCRACK "/apps/capcrack"` added to sdcard_manager.h + ensureDir + ensureTree.
- Pure SD + crypto, no WiFi → no GDMA concerns (runs from CLI with WiFi idle).

## Status
Implemented + self-reviewed; **user compiles** (see [[feedback-compile-builds]]). NOT hw-tested.
Test: `cd /apps/karma` then `cc MyNet.cap` (or `cc` to pick) → choose built-in or an SD
wordlist/dir → expect FOUND or "Not found (N tried)".

Related: [[project_karma_rogue_handshake]] (produces the .cap), `wpa_crack`, `dot11`,
`ws`/`pm` (same crypto), `pcap_writer`.
