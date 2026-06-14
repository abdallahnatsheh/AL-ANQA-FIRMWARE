# Regression Test Plan — wifi/core dedup refactor + Karma

Manual on-device checklist to confirm the shared-module refactor did **not** change
any existing app behaviour, and that the new Karma features work.

**How to use:** flash to a T-Deck Plus, run each section, tick the box if it matches
the **Expected** result. Anything that differs from before the refactor = a regression.
You need: a phone/laptop you control with both a **WPA2** and an **open** saved
network, and an SD card.

---

## 0. What changed (context)

New shared modules in `wifi/core/`:
- `dot11.h` — 802.11 parse (SSID IE, EAPOL detect/classify)
- `pcap_writer.h` — libpcap writer (linktype 105)
- `captive_portal.h/.cpp` — open/WPA2 AP + DNS + field-agnostic capture + built-in templates + SD templates + picker
- `wpa_crack.h/.cpp` — PBKDF2 / handshake-MIC / PMKID crack + shared built-in wordlist

Migrated onto them (should be **behaviour-identical**): `ws`, `pm`, `wm`.
New feature work: `karma`. **Not touched:** `eviltwin`, `deauth`, `beaconflood`, `hiddenssid`.

---

## 1. Build

- [ ] `pio run -e T-Deck-Plus` succeeds (no errors/warnings about wifi/core).
- [ ] Flashes and boots to the CLI normally.

---

## 2. ws — WPA2 handshake (migrated: pcap + EAPOL parse + crack + wordlist)

- [ ] `sw` lists networks; note a **WPA2** index.
- [ ] `ws <idx>` → screen shows AP/CH/SD, "Waiting…".
- [ ] Reconnect a client to that AP → **EAPOL counter rises**, `[CAPTURED!]` appears.
- [ ] `[c]` crack → with the real password in `/apps/wpasniff/wordlist.txt`, shows **KEY FOUND**.
- [ ] Without SD wordlist → falls back to **built-in (100)** list, runs, ends "not found" if absent.
- [ ] `.cap` written to `/apps/wpasniff/<BSSID>.cap`.
- [ ] On PC: `aircrack-ng <BSSID>.cap` shows **(1 handshake)**; the `.cap` opens in Wireshark.
- [ ] `/apps/wpasniff/cracked.csv` gets a row when cracked.
- **Expected:** identical to pre-refactor behaviour.

## 3. pm — PMKID (migrated: pcap + M1 parse + crack + wordlist)

- [ ] `pm <idx>` on a WPA2 AP → waits for M1.
- [ ] On M1 with PMKID → shows PMKID captured; `.cap` in `/apps/pmkid/`.
- [ ] `[c]` crack → built-in or `/apps/pmkid/wordlist.txt`; **KEY FOUND** with real pw present.
- [ ] `/apps/pmkid/cracked.csv` row on success.
- [ ] "M1 seen — no PMKID" path still shown when AP gives no PMKID.
- **Expected:** identical to pre-refactor.

## 4. wm — WiFi monitor (migrated: extractSSID + pcap, 3 sites)

- [ ] `wm` → **Nets** view populates with SSIDs (confirms shared `extractSSID`).
- [ ] `[v]` → Clients view works; client counts update.
- [ ] `[s]` PCAP on → `frm` counter rises; `.cap` in `/apps/wifimon/`.
- [ ] That `.cap` opens in Wireshark and decodes 802.11 frames (confirms shared pcap header).
- [ ] `[p]` probe log → `/apps/wifimon/probes.csv` rows.
- [ ] `[d]` directed deauth from Clients view still works.
- [ ] `[q]` exits cleanly.
- **Expected:** identical to pre-refactor.

---

## 5. karma — NEW (harvest + fingerprint)

- [ ] `km` → `[KRMA::HARV]`, channel hops, SSIDs appear with DEV/HIT/RSSI.
- [ ] Trackball moves the `>` cursor; highlight covers **exactly one row** (no bleed).
- [ ] `[a]/[l]` page; `[c]` clears.
- [ ] `[v]` → **DEVS** view: VENDOR/TYPE · PNL · MAC · RSSI; trackball selects; PNL pane updates.
- [ ] A laptop/older phone with 2+ saved nets shows **MAC count > 1 in green** (randomization defeated).
- [ ] GrapheneOS / hardened phone does **not** appear (expected).

## 6. karma — WPA2 handshake bait + on-device crack (NEW)

- [ ] `km hs <yourSSID> 6` (or `[h]` on a selected HARV/DEVS row).
- [ ] On a client with that WPA2 net saved (real AP off/out of range) → **`[M2]` lights green**.
- [ ] `.cap` saved to `/apps/karma/<ssid>.cap`; opens/validates on PC.
- [ ] Press **`[c]`** after M2 → `[KRMA::CRACK]`, "Tried: N" climbs.
- [ ] With real pw in `/apps/karma/wordlist.txt` (or built-in) → **FOUND: \<pw\>**.
- [ ] `/apps/karma/cracked.csv` gets `ssid,password`.
- [ ] No-SD: still captures + shows M2/FOUND on screen, just no file (no crash).

## 7. karma — open portal + template picker (NEW)

- [ ] `km portal <openSSID>` (or `[p]` on a selected row) → **`[PORTAL::PICK]`** appears.
- [ ] Lists built-ins: **Generic WiFi / Google / Router Update**, plus any `/apps/karma/portal/*.html` tagged `(SD)`.
- [ ] Choose **Google** → join the open AP on a phone → captive portal shows the **Google** page.
- [ ] Submit any email/password → **Captured** rises, last cred shown; `Page Google` on screen.
- [ ] Drop a custom `.html` in `/apps/karma/portal/` → appears in picker as `(SD)` and serves.
- [ ] `q` in picker cancels cleanly (portal not launched).
- [ ] On exit, creds appended to `/apps/karma/creds.csv` (`ssid,user,pass`); no-SD = RAM only.

## 8. karma — interactive return-to-list

- [ ] From `km`, select a row → `[h]` or `[p]` → run bait → result → **any key returns to live harvest** (still sniffing).

---

## 9. Untouched apps — sanity (should be unchanged)

- [ ] `eviltwin` (`et`): scan/custom modes, its own template picker `[p]`, captures creds. (Not migrated — must work as before.)
- [ ] `deauth` (`da`): deauths a target.
- [ ] `beaconflood` (`bf`): list/clone modes inject.
- [ ] `hiddenssid` (`hs`): reveals a hidden SSID.

## 10. Cross-cutting

- [ ] **GDMA:** after running each of ws/pm/wm/karma, run `sw` again → WiFi still scans (bus not corrupted).
- [ ] **Lock screen:** lock during `km`/`ws` → screen blocks; unlock → redraws correctly.
- [ ] **Memory:** run `km` and `ws` several times back-to-back → no crash/reboot (buffers freed each exit).
- [ ] **No-SD:** pull SD, reboot → `km`, `ws`, `pm` all run (degraded, RAM-only), no crash.

---

## Sign-off

- Tester: ____________   Date: __________   Firmware commit: __________
- Result: ☐ all pass   ☐ regressions found (list below)

Notes / failures:
```
```
