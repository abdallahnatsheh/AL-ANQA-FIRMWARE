---
name: Next Steps
description: Ordered feature queue — priority order
type: project
---

## Already implemented (do NOT re-add)
`beaconflood/bf` · `bleinfo/bi` · `usbkbd/uk` · `usbexec/ux` (BadUSB) · `clock/ClockManager` · `buddy/bd` · `wguard/wg` · `hiddenssid/hs` · `blespam/bs` · `jiggle/jg` (mouse jiggler) · `fast_pair/fp` (Google Fast Pair scan/spam/hijack) · `show/sh` (last scan results) · `tz` (timezone config) · `volume/vol` (I2S volume) · `notif/nf` (per-level sound config) · `wifimon/wm` (airmon-ng rewrite: nets+clients views, targeted deauth, raw PCAP, probe logger `[p]` → `/apps/wifimon/probes.csv`) · `oui_lookup.h` (shared ~350-entry vendor+type table) · `pmkid/pm` (PMKID capture+crack, no client needed, passive M1 sniff) · `bmon/bm` (passive BLE adv sniffer — iBeacon/Eddystone/cleartext, PCAP) · `espvoice/ev` (ESP-NOW G.722 walkie-talkie) · `mictest/mt` (ES7210 mic test) · `trackme/tm` (anti-tracking, service-UUID sigs) · `ssh/sc` (interactive SSH client via LibSSH-ESP32) · `wardrive/wd` (WiFi+GPS → WiGLE 1.4 CSV, Plus only) · `karma/km` (Karma/MANA: harvest+PNL fingerprint, rogue-AP half-handshake, auto mode, captive portal — wifi/attacks/karma/) · `edit/ed` (nano-style on-device SD text editor — core/editor/, trackball cursor + click-menu) · `macwatch/mw` (WiFi+BLE MAC watchlist → proximity alert: beep+wake+popup, presence SM, hunt meter, BLE-only bg mode + MW badge — bluetooth/tools/macwatch/, built but NOT yet HW-tested) · `ble_ident.h` (header-only BLE device ID: SIG company IDs + Apple Continuity + AirPods/Beats models — used by bmon/sbl/mw). SD layout
is now `/apps/<tool>/` + `/config/` (v2 reorg) — see `project_sdcard_reorg_v2.md`.

---

## WiFi Pentest

1. **Karma / MANA** — ✅ DONE as `karma/km` (harvest + PNL union-find fingerprinting, rogue-AP half-handshake [`km hs`], captive portal [`km portal`/`[p]`], `km auto` hands-free sweep, never-overwrite `.cap`, GDMA-safe). Module `wifi/attacks/karma/`. HW-verified end-to-end (associate→M2→on-device crack).
2. **ARP Poisoning** — MITM via fake ARP replies. Command: `arpspoof/as <victim-ip> <gw-ip>`
3. **LAN MITM (Gateway Takeover)** — join real net as STA, ARP poison all devices + GW, relay traffic, sniff DNS/HTTP, `[b]` block / `[t]` throttle / `[d]` DNS spoof. Command: `lanmitm/lm`
5. **AP Bridge** — transparent AP+STA bridge; clients get real internet; sniff + redirect. Command: `apbridge/ab [ssid]`
6. **Responder** — LLMNR/NBT-NS/mDNS poisoner; capture NTLMv2 hashes. Command: `responder/rsp`
7. **SSH Connect** — ✅ DONE 2026-06-13 as `ssh/sc <ip> [user]` (LibSSH-ESP32, interactive PTY shell, colour terminal + trackpad scrollback). Remaining: `<#>` index from `nd`, host-key pinning + key auth on `/apps/ssh/`, Ctrl-C, fuller VT100.
8. **TCP Listener/Client** — catch reverse shells / forward input. `tcplisten/tl <port>` · `tcpclient/tc <ip> <port>`
9. **DPWO** — default credential checker against discovered hosts. Command: `dpwo/dw <ip|#>`
4. **WPA3 transition-mode downgrade** — `wpa3down`/`w3d`. Detect transition-mode APs (`[TD]`) +
   PMF (RSN-IE MFPC/MFPR + AKM SAE+PSK) in `sw`, deauth/pre-assoc-flood victim, WPA2-only rogue AP,
   capture EAPOL+PMKID → HCCAPX/HC22000. Mostly assembles existing `ws`+`pm`+`eviltwin`+`sw`. Full
   plan: `TREX_WPA3_DOWNGRADE_PLAN.md` · ref: [[project_wpa3down_plan]] (current-repo mapping + reuse).

## Bluetooth

10. **BadBLE** — ✅ **PHASE 1 DONE 2026-07-12 as `ux ble` (subcommand of existing BadUSB), code-complete + static-reviewed, UNCOMMITTED, NOT HW-tested.** Runs the SAME DuckyScript engine over BLE HID instead of USB. Implementation = a keystroke-sink abstraction (`usb/hid/hid_sink.{h,cpp}`: `HidSink` iface + `UsbHidSink`/`BleHidSink`); the whole `bad_usb.cpp` parser now drives `_sink->press/printChar/releaseAll` (zero DuckyScript logic duplicated). BLE side reuses the existing `btkbd` HID stack — added `BleKeyboard::badusbBegin/End/Connected` + `kbdHoldChar/kbdHoldUsage/kbdType/kbdReleaseAll` (live 8-byte report, reuses its `asciiToHid`). `BleHidSink` maps Arduino USBHIDKeyboard keycodes→HID usage (specials/mods) and routes printables through `kbdHoldChar`. `ux ble [demo|script]`: `handleUsbExecCmd` strips the `ble` prefix + `stopMacwatchBg()`, `badUsb.start(args, ble=true)` advertises "T-REX-KBD" + waits for the target to pair (q/hold cancels) then types. Man page + cmd desc updated. **NEXT (Phase 2, the novel BLESA part, NOT built):** clone a bonded keyboard's MAC (`setOwnAddr`, like `mc`) from last `sbl` + forced-disconnect flood → host auto-reconnects to the clone → inject before re-auth (unpatched Android/old iOS/some Windows). Phase 1 is a plain "pair the T-Deck as a keyboard" HID injector (many hosts auto-accept); Phase 2 adds the spoofing that makes it an attack.
    - **FAST-CONNECT (2026-07-12): `badusbBegin` → `beginHid(false)` = Just Works, NO bond** (btkbd stays bonded via default `beginHid(true)`). Fast connect, inject, disconnect, nothing left on host; sidesteps the Windows stale-LTK dance. HID-over-GATT still needs the link encrypted, so can't go fully plaintext. Only the initial host "connect/accept" is unavoidable (BLE peripheral role) — removing THAT is what Phase 2 clone is for.
    - ✅ **PHASE 2 DONE 2026-07-12 (BLESA MAC-clone), code-complete + static-reviewed, UNCOMMITTED, NOT HW-tested.** `ux ble clone <mac|#> <script>`: spoof a bonded keyboard's identity address (reuses the proven `setOwnAddr(NimBLEAddress(str,type))` primitive, added `cloneMacStr/cloneType/cloneName` params down `beginHid`←`badusbBegin`←`BadUsb::start`←`bleWaitForHost`) + mimic its advertised name, so a host bonded to it auto-reconnects → inject. Target from the shared `sbl` cache (`s_bleDevices[idx].addr/addrType/name`, `#idx`) or a raw MAC (assumes random-static). Screen shows `[BLE::CLONE]` + the spoofed MAC + "waiting host auto-reconnect (real device must be off)". **HONEST LIMITS (labeled [EXP] on-screen + man):** works only if (a) host is BLESA-vulnerable / doesn't re-auth on reconnect, (b) real device is absent, (c) target uses a random-static addr (public spoofing often refused by the controller; RPA/resolvable addrs rotate → can't clone stably). **No BLE jamming to force the real device off — ESP32 can't (out of scope, noted).** Files: `usb/hid/hid_sink.{h,cpp}` (new), `bad_usb.{h,cpp}`, `ble_keyboard.{h,cpp}`, `command_manager.cpp`, `man_pages.cpp`.
    - **Name-spoof option (2026-07-12): `ux ble name "<X>" <script>`** — fake advertised device name (evil-twin style), works in BOTH fresh + clone modes (overrides the cloned/default name). Reuses the same `cloneName` param that already reaches `adv->setName`; quotes needed for spaces. So the two modes are: **connect** (`ux ble` fresh kbd, victim connects) + **spoof** (`ux ble clone` impersonate bonded kbd), each with an optional custom `name`.
    - **INTERACTIVE MODE (2026-07-12): bare `ux ble` → guided menu** (`BadUsb::startInteractive` + `blePickMode/blePickTarget/blePickScript/blePromptName`, list-pickers modelled on wpa3down's): mode select (connect/spoof) → if spoof, target picker over the `sbl` cache (`s_bleDevices`, scrolling, shows name/rssi; empty → "run sbl first") → advertised-name prompt (prefilled with cloned/default name, editable) → script picker (lists `/apps/badusb/scripts/*` + built-in demo) → hands off to `start()`. CLI forms (`ux ble clone #n`, `ux ble name "X"`) still work as power-user shortcuts. Whole BadBLE feature (USB + BLE connect + BLESA clone + name-spoof + interactive) COMMITTED (1e8fa95).
    - **FIRST HW TEST 2026-07-12 → 3 bugs found + FIXED (uncommitted, needs recompile+retest):**
      **(bug2) clone spoofed the NAME but not the MAC + name reverted to "T-REX-KBD" on connect.** Root causes (from NimBLE source): `setOwnAddr`→`ble_hs_id_set_rnd` ONLY accepts a static-random addr (MSB top-bits 11) or non-resolvable — it REJECTS public + resolvable-private (RPA); we ignored its bool return. And we only set the *advertised* name, not the GAP device-name char 0x2A00 (what a connected host reads). **Fix:** check `setOwnAddr` return → `s_cloneMacOk` flag + honest "MAC spoof FAILED (RPA/public) → name-only" on the wait screen; `NimBLEDevice::setDeviceName(cloneName)` so the connected name matches; picker now marks `*`=clonable (static-random) rows so you don't pick an RPA/public target.
      **(bug3) after a clone, `bk` looped connect/disconnect.** **DIAGNOSED on the dev Linux laptop (host) via bluetoothctl 2026-07-12 — it was a HOST-SIDE STALE BOND, not a firmware bug.** `bluetoothctl info` showed `T-REX-KBD C2:65:99:BC:8D:CB Paired/Bonded/Trusted:yes`; journal showed it disconnecting in a tight loop — BlueZ auto-reconnects a Trusted device instantly, and the stored LTK no longer matched the T-Deck's (desynced by the pre-fix runs where bk+BadBLE both used …CB no-bond) → connect→encrypt-fail→disconnect→loop. **Recovery: `bluetoothctl remove C2:65:99:BC:8D:CB` (done) then re-pair `bk` fresh.** **Firmware fix = the PREVENTION:** btkbd keeps its stable bonded addr …CB; **BadBLE (`ux ble`, bond=false) now uses a FRESH RANDOM static-random MAC each session** (`esp_random()`, `r[0]|=0xC0`) so a no-bond session can never land on btkbd's identity. **Scoped to ux ble ONLY** via the `!bond` gate (bond=false is used exclusively by BadBLE; btkbd/buddy/jiggle use bond=true, `mc` is separate) — per user request "only for ux ble". A successful clone still uses the target's exact MAC. Diagnostic method (host is the Claude machine): `bluetoothctl devices Paired` / `info <mac>` / `journalctl | grep <mac>`. ▶ pending: user re-runs `bk` to confirm the loop is gone after the fresh pair.
      **(bug1) interactive target picker reworked to sbl-style paged table** (#/NAME/RSSI/AT/MAC, RSSI-sorted, a/l paging) with the clonable marker.
      **KEY LIMIT learned: only static-random-addressed targets are clonable on ESP32; RPA (Apple/privacy devices) + public addrs cannot be spoofed (NimBLE/controller rejects). The picker's `*` shows which.**
    Prior: **COMPILES CLEAN.** ▶ HW-TEST CHECKLIST: (1) `ux ble demo` — a phone/PC connects to T-REX-KBD (no PIN/bond) + types; (2) `ux ble clone #n` after `sbl` — spoofs a real kbd's MAC+name, host auto-reconnects (needs BLESA-vuln host + real kbd off + random-static addr); (3) `ux ble` interactive menu nav; (4) verify btkbd (`bk`) still bonds normally (shared beginHid).
11. **macwatch** — ✅ DONE + HW-VERIFIED 2026-06-28 as `macwatch/mw` (WiFi+BLE MAC watchlist → proximity alert; presence state machine, hunt meter, BLE-only bg mode). Module `bluetooth/tools/macwatch/`. Full ref: `project_macwatch_idea.md`.
12. **BT Command Relay** — send CLI command to remote T-Deck over BLE NUS; output streams back. `btcmd/btc <mac> <cmd>`

## GPS / T-Deck Plus Only

13. ~~**Wardriving**~~ — ✅ DONE 2026-06-20 as `wardrive/wd` (Plus only). Continuous async WiFi
    scan + GPS → WiGLE WiFi-1.4 CSV at `/apps/wardrive/NNN.csv` (never overwritten). Logs each
    BSSID once per session, only while a fix is valid; dedup table in PSRAM; verified against the
    official WiGLE 1.4 spec (api.wigle.net/csvFormat-1_4.html). Module `wifi/tools/wardrive/`.
14. **GPS Tracker** — log coords + timestamp every N seconds. Command: `gpstracker/gtr [interval_s]`

## Network Intelligence (client-isolation bypass — AirSnitch, NDSS 2026)  [NEW PLAN 2026-06-26]
Both require T-Deck connected to the target net (`cw`). Full plan: `TREX_NETSPY_ISOSCAN_PLAN.md` ·
ref: [[project_netspy_isoscan_plan]] (current-repo mapping + reuse + GDMA notes).
- **netspy** (`ns`) — discover devices despite client isolation via DHCP/mDNS/SSDP/IPv6-ND passive
  sniff + assoc frames + GTK broadcast-ARP (responders = `GTK-REACHABLE` targets) + OUI. Reuse `oui_lookup.h`.
- **isoscan** (`is` `[#]`) — bypass attacks on a target: GTK check/inject (ICMPv6-RA DNS poison),
  gateway bounce, broadcast reflect, downlink/uplink port stealing (MAC spoof — **must restore MAC on
  every exit path**). `isoscan auto` runs all. No AirSnitch code used (techniques from the paper).

## Sensing (WiFi CSI)  [2026-06-26]
ESP32-S3 WiFi only, no extra sensors. Full plan: `TREX_CSI_CAMDETECT_PLAN.md` · ref:
[[project_csi_camdetect_plan]] (implementation status + next phases live there).
- **csidetect** (`csi`) — ✅ DONE + HW-VERIFIED 2026-06-26; now a PRO **WiFi motion detector** `[EXP]`
  (sprite ~30fps, 8 subcarrier-band sectors, sweep cone, `h` help, full docs). **Honestly reframed:
  NOT a radar** (single antenna = motion energy, no bearing). Module `wifi/sensing/csidetect.cpp`.
  Remaining: (1) SD presence log + alert; (2) field-tune sector usefulness; (3) coexistence under `wg bg`.
- ~~**camdetect**~~ — DROPPED (not CSI — a promiscuous camera-OUI sniffer; revisit separately if wanted).

## USB Attacks

15. **Auto OS Detection** — detect Windows/macOS/Linux on USB connect; auto-select script folder for `ux`
16. **Remote BadUSB via WiFi** — `rbadusb/rb`; HTTP AP to trigger scripts from phone
17. **Keylogger Mode** — `keylog/kl`; USB HID host-direction capture to SD
24. **USB-LAN AdBlocker / DNS-MITM dongle** — T-Deck as a USB network gadget (TinyUSB
    **NCM/RNDIS**) bridging the host PC ↔ WiFi STA (lwIP NAPT + DHCP server on the USB side),
    with a DNS sinkhole in the middle. Adblock = benign mode; offensive mode = DNS **log +
    selective redirect** → feed an eviltwin/karma captive portal. Reuse **s60sc/ESP32_AdBlocker**
    (https://github.com/s60sc/ESP32_AdBlocker) for the sinkhole + PSRAM blocklist — it's
    **AGPL-3.0 (matches), ESP32-S3/8MB-PSRAM (= T-Deck Plus), <50µs lookups**. The adblocker is
    the easy/reusable half; the **USB-NCM↔WiFi bridge is the hard new half** (Espressif has a
    `usb_ncm` NAPT example as a starting point). Constraints: USB is Full-Speed (~few Mbps real);
    likely needs to be an **exclusive USB mode** (endpoint/RAM budget — can't co-run MSC+HID+NCM
    easily); blocklist in PSRAM (fine), USB stack/lwIP buffers hit internal DRAM (we're at 60.8%).
    **Do an enabling spike first** (prove USB-NCM gets the PC online via T-Deck WiFi) before any
    UI/integration — the feature lives or dies there. Command idea: `usblan/adblock`. Biggest
    feature discussed so far. Related: [[project_usb_gadget_plan]], eviltwin/karma portals, LAN MITM (#3).

## Other

18. **VNC Client** — connect to VNC server; 320×240 scaled. Library: `moononournation/ArduinoVNC`. Command: `vnc/vn <ip> [port] [pw]`
19. **QR Code** — render QR from text/URL/WiFi cred. Command: `qrcode/qr <text>`
20. **LoRa scanner** — SX1262 receive; 433/868/915 MHz; log RSSI/SNR/payload to `/apps/lorascan/lora.csv`. Command: `lorascan/ls`. (NOTE 2026-07-07: the LoRa radio is currently UNUSED by the firmware — the only LoRa code is the `test lora` HW ping. There is NO `lt` command; the old "`lt` has live RX display" note was stale/wrong. This is greenfield — needs a RadioLib SX1262 driver. See #25 for the bigger Meshtastic/comms idea.)
21. **Chromecast Control** — Cast API port 8009; `cast rickroll` for all Chromecasts on LAN. Command: `cast/ca`

## Low Priority

22. **Mic Record (WAV→SD)** — PARTIALLY DONE via `mictest/mt` (record 3s + replay, RAM-only). ES7210 mic, both boards. Remaining: WAV-to-SD save (`/apps/micrec/rec_<ts>.wav`).
23. **NES Emulator** *(Easter egg)* — Nofrendo; ROMs from `/roms/*.nes`

## Brainstorm — verified NOT implemented (added 2026-07-07)
Checked against the live `registerCommand` table AND grepped the source — all confirmed unbuilt as
of 2026-07-07 (63/64 commands registered). Ordered roughly by value-on-this-hardware. NOT yet
prioritized into the main queue above; pick as desired. **Command cap is `Command commands[64]` in
command_manager.h — arbitrary static array; raise to 128 (~2KB RAM) if working through this list.**

### RF / LoRa (SX1262 radio is completely unused — biggest capability gap)
25. **Meshtastic client / LoRa comms** ⭐ — full send/receive on the LoRa mesh (the T-Deck is a
    *flagship* Meshtastic device). Superset of #20 (which is RX-log only). Needs a RadioLib SX1262
    driver + Meshtastic protobuf/LoRa PHY params. Could also be an own-protocol encrypted long-range
    T-Deck↔T-Deck chat if full Meshtastic compat is too heavy. Command idea: `mesh/msh` or `lorachat/lc`.
26. **LoRa APRS / position beacon** — beacon GPS position (Plus) over LoRa; pairs with #14 tracker.
27. **LoRa jammer / spectrum** — offensive; RSSI sweep + carrier flood on a band.

### WiFi (offensive/recon — none of these exist)
28. **Pwnagotchi mode** ⭐ — unattended channel-roam + auto-harvest handshakes/PMKIDs → SD, with a
    stats/"face" screen. High reuse of `ws`/`pm`. Command: `pwn`.
29. **WPS Pixie-Dust / PIN attack** — real gap, no WPS attack anywhere. Command: `wps`.
30. **WiFi hot/cold locator** — RSSI meter + rising tone to physically FIND a hidden AP / spy camera
    by MAC or SSID. Practical field tool. Command: `locate/loc <bssid|ssid>`.
31. **Rogue-AP / evil-twin DETECTOR** (defensive) — dedicated alarm; deeper than `wg`'s pass.
32. **Channel/airtime utilization graph** — find the clearest channel.
33. **BSSID geolocation** — offline lookup of scanned BSSIDs vs a WiGLE dump on SD (ties to `wardrive`).

### BLE
34. **iBeacon / Eddystone broadcaster** — *advertise* as a beacon (today we only receive via `bmon`).
35. **GATT fuzzer** — offensive malformed reads/writes vs a target's services (extends `bleinfo`).
36. **BLE proximity finder** — ⚠ PARTIAL: `macwatch` already has an RSSI proximity gate + "hunt"
    meter (get-warmer/colder for a target MAC). A standalone app would just surface/polish `mw`'s
    hunt mode, not net-new. Consider promoting the hunt UI instead of a new command.

### Audio (mic + speaker — both boards)
37. **Audio spectrum analyzer / waterfall** ⭐ — FFT of the mic to the display; showcases the screen.
38. **Instrument tuner** (pitch detect) · **tone/function generator** · **dB SPL meter** ·
    **metronome** · **morse beacon/decoder/trainer** · **white/pink-noise generator**. (Small, could
    share one `audio`/`snd` command with subcommands to save slots.)

### Utility / cover apps (double as undercover disguise — fit the Notes/undercover theme)
- **✅ DONE 2026-07-07b: `home`/`hm` home-launcher cover** (phone-style home screen) + **`weather`/`wx`**
  (OpenWeather, GPS/config location, self-seeding `/config/weather.conf`) shown in the home hero. Real
  clock/battery/weather in the hero. See [[project_undercover_home_launcher]] + [[progress_log]] 07b.
39. **TOTP 2FA authenticator** ⭐ — reuse existing mbedTLS HMAC-SHA1 + ClockManager time; genuinely
    useful AND a believable cover app. Secrets encrypted on SD. Command: `totp/2fa`.
40. **Encrypted password vault** (AES-on-SD) · **calculator** (sci/programmer) · **unit/currency
    converter** · **stopwatch/timer/alarm** · **ebook/txt reader** (paged, bookmarks) · **todo** ·
    **contacts** · ~~weather~~ (✅ done above) · **WiFi-cred QR share** (ties to #19).

### Games (excellent undercover cover — buddy is a pet, NOT a game; none exist)
41. **Snake · 2048 · Tetris · Minesweeper · Conway's Life · Sudoku · Wordle · Pong · Dino-runner.**
    Could live under one `game/gm <name>` launcher to spend a single command slot.

### Dev / network tools
42. **Wake-on-LAN sender** · **HTTP request tool** (curl-lite) · **MQTT client** · **network speed
    test** (iperf-like) · **mDNS/Bonjour browser** (netspy already parses mDNS) · **serial/UART
    bridge** (Grove) · **ADC voltmeter/scope** (Grove) · **SD benchmark**.

### Add-on-dependent (only with extra hardware)
43. **IR remote / TV-B-Gone** (IR LED) · **NFC/RFID clone** (PN532) · **433 MHz replay** (CC1101) ·
    **BME280 environmental logger**.
