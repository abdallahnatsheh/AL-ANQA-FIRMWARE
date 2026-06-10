# SD Card Layout v2 — /apps/<tool>/ + /config/

## Status
Done, committed (`a022850`, branch `feature/pentest-enhancements`), pushed.
Not yet built/flashed/tested on hardware — pure path/define/doc changes plus
the new eager directory-creation code in `SDCardManager`. Build both
`T-Deck` and `T-Deck-Plus` envs before relying on it.

## What changed
Every tool now owns a self-contained `/apps/<tool>/` folder holding its logs,
captures, wordlists, and tool-specific config together. `/config/` is reserved
for device-wide settings only (pwrsave, macchanger, lockscreen, notif, clock +
`/config/notification/*.mp3`). The old `/logs/` and `/captures/` trees are
gone entirely. Fresh-start reorg — no rename-on-boot code; files at old paths
are orphaned, not migrated.

### New layout (see CLAUDE.md "## SD Layout" for the full canonical table)
```
/wpa_supplicant.conf, /wpa_supplicant.bak        (root, unchanged)

/config/
  pwrsave.conf, macchanger.conf, lockscreen.conf, notif.conf, clock.conf
  notification/*.mp3

/apps/
  README.txt                  (auto-generated folder->command map)
  trackme/      session.csv, known.csv, signatures.csv
  eviltwin/     creds.csv, portal/*.html
  hiddenssid/   found.csv
  wpasniff/     wordlist.txt, <BSSID>.cap, cracked.csv
  pmkid/        wordlist.txt, <BSSID>.cap, cracked.csv   (no more pm_ prefix)
  wifimon/      NNN.cap, probes.csv
  wguard/       NNN.csv
  beaconflood/  wordlist.txt
  bmon/         NNN.csv
  i2cscan/      results.csv
  fastpair/     keys.csv, paired.csv, sniff.csv
  espsniff/     NNN.csv + NNN.pcap
  bleinfo/      <mac>*.{txt,ble}
  espchat/      contacts.csv, config.conf, pub/, prv/
  badusb/       scripts/*
```

## Key design decisions (from user)
1. Split handshake captures into separate `/apps/wpasniff/` (`ws`) and
   `/apps/pmkid/` (`pm`) — each with its own wordlist/cracked.csv/cap, dropped
   the `pm_` filename prefix since the folder disambiguates.
2. Dropped `/captures/` entirely (was unused).
3. Shared notification MP3s moved to `/config/notification/`; `notif.conf`
   itself stays in `/config/`.
4. ESPChat folded into `/apps/espchat/` (kept `pub/`/`prv/` subfolders).
5. Folder name is `/apps/` (not `/tools/`).
6. **SD init must EAGERLY create the FULL `/config` + `/apps/<tool>` tree on
   first boot/format** — not lazily on first use.

## Implementation — `t-rex-firmware/hardware/sdcard/sdcard_manager.h/.cpp`
- All path defines (`SD_DIR_*` / `SD_LOG_*` / `SD_CFG_*`) centralized in the
  header, e.g. `SD_DIR_TRACKME`, `SD_CFG_WORDLIST_WS`/`SD_CFG_WORDLIST_PM`,
  `SD_DIR_WPASNIFF`/`SD_DIR_PMKID`, `SD_LOG_CRACKED_WS`/`SD_LOG_CRACKED_PM`,
  `SD_DIR_CONFIG_NOTIF`, `SD_DIR_ESPCHAT`/`_PUB`/`_PRV`, `SD_DIR_BADUSB`/`_SCRIPTS`.
- `ensureTreeStructure()` — creates `/config`, `/config/notification`, `/apps`,
  and all 21 `/apps/<tool>` dirs via `ensureDir()` (idempotent), then calls
  `ensureAppsReadme()`. Called from `begin()` (every boot) and `performFormat()`
  (after `sdf` format).
- `ensureAppsReadme()` — writes `/apps/README.txt` once (folder -> owning
  command map), never overwritten.
- `initializeTDeckStructure()` (`sdf init`) — loops the same 22-dir array via
  `ensureDir()`, calls `ensureAppsReadme()`, and seeds
  `/config/pwrsave.conf`, `/config/macchanger.conf`, `/apps/trackme/signatures.csv`.

## Files touched (firmware)
trackme.cpp, eviltwin.h/.cpp, hidden_ssid.cpp, handshake_capture.cpp,
pmkid_attack.cpp, wguard.cpp/.h, bmon.cpp, i2cscan.cpp, fast_pair.cpp,
espsniff.cpp, ble_info.cpp, espchat_core.cpp/.h, espchat_ui.cpp, espchat_bg.cpp,
bad_usb.cpp, beacon_flood.cpp, notification_manager.cpp, wifimon_functions.cpp,
clock_manager.cpp/.h, powersave_manager.cpp, lockscreen_manager.cpp,
mac_changer.cpp, man_pages.cpp — all updated to the new defines/paths.

## Docs
CLAUDE.md "## SD Layout" fully rewritten; `docs/sdcard.md` rewritten
(directories table, files table, "create manually" section); bulk path
updates across `docs/*.md`, `README.md` (file-tree block), `NEXT_STEPS.md`.

## Verification done
Repo-wide grep for `/logs/`, `/config/wordlist`, `pm_<BSSID>`, `/badusb/payload`,
`/evilportal`, `/captures` across `*.md`/`*.cpp`/`*.h` returns zero matches.

## Known stale references NOT yet fixed
`.claude/memory/project_wguard_feature.md` and `.claude/memory/next_steps.md`
still mention old `/logs/...` paths (`/logs/wguard/NNN.csv`, `/logs/probes.csv`,
`/logs/lora.csv`, `/logs/rec_<ts>.wav`) — cosmetic only, update opportunistically.

## Next step
Build both PlatformIO envs (`env:T-Deck`, `env:T-Deck-Plus`), flash, run `sdf init`
and confirm `/apps/README.txt` + full tree appear, then spot-check a few tools
(`tm`, `wg`, `ws`, `pm`, `bm`) write to their new `/apps/<tool>/` paths correctly.
