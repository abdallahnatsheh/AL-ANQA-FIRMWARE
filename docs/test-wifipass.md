---
title: wifipass Test Cases
nav_order: 99
---

# wifipass / WiFi Credentials — Test Cases

Test on branch `feature/wpa-supplicant-sync` before merging into `feature/pentest-enhancements`.

---

## Phase 1 — No SD Card (NVS only)

- [ ] **wp with no credentials** — run `wp` on a fresh device, should show "No saved credentials"
- [ ] **Connect by index** — run `sw`, then `cw <index>`, enter password → connected
- [ ] **Password saved to NVS** — run `cw <same index>` again → shows "Using saved password", no prompt
- [ ] **Connect by SSID name** — run `cw <ssid>` without scanning → prompts for password → connects
- [ ] **Name → index** — after connecting by name, run `sw` then `cw <index>` → "Using saved password"
- [ ] **wp shows NVS networks** — run `wp` → NVS indicator is yellow, shows correct SSID + password
- [ ] **Wrong password** — enter wrong password on connect → times out → run `wp` → wrong password NOT saved
- [ ] **Open network** — connect to open network (`key_mgmt=NONE`) → no password prompt → connects → `wp` shows `[open]`
- [ ] **clearwifi** — run `clrw` → run `wp` → shows "No saved credentials"

---

## Phase 2 — With SD Card

- [ ] **wpa_supplicant.conf created** — connect to a new network → check SD root has `/wpa_supplicant.conf`
- [ ] **Backup created** — if a file already existed on SD, check `/wpa_supplicant.bak` was created
- [ ] **wp shows SD networks** — run `wp` → SD indicator is green
- [ ] **No duplicate entries** — connect to the same network twice → open SD file → only one entry for that SSID
- [ ] **Connect by SSID from SD** — clear NVS (`clrw`), keep SD card → `cw <ssid>` → connects using SD password, no prompt
- [ ] **Hidden network** — connect via `cw <hidden-ssid>` → check `scan_ssid=1` is in the saved entry

---

## Phase 3 — Linux Sync (when you get access to a Linux machine)

- [ ] **T-Deck → Linux** — copy `/wpa_supplicant.conf` from SD to Linux → Linux connects to T-Deck-added network
- [ ] **RPi / headless Linux → T-Deck** — copy Linux `/etc/wpa_supplicant/wpa_supplicant.conf` to SD → `wp` shows all networks → `cw <ssid>` connects
- [ ] **wpa_passphrase format** — generate entry with `wpa_passphrase "SSID" "pass"` on Linux → copy to SD → T-Rex reads `#psk=` comment → connects without prompt
- [ ] **hex-psk entry** — put an entry with only `psk=hexhash` (no comment) on SD → `wp` shows `[hex-psk]` → `cw <ssid>` prompts for password → connects → run `wp` again → now shows plain password (self-heal)
- [ ] **update_config=1 recovery** — let Linux rewrite the file (strip comments) → copy back to SD → `wp` shows `[hex-psk]` → enter password once → `wp` shows plain password

---

## Notes

- SD card must be FAT32, inserted before power-on
- NVS namespace is `wifi`, keyed by SSID
- Wrong password: 15 second timeout, then returns to prompt
- Minimum password length: 8 characters (shorter → rejected with error)
