<p align="center">
  <img src="images/banner.png" width="420"/>
</p>

<p align="center">
  <a href="https://github.com/abdallahnatsheh/T-REX-FIRMWARE/actions/workflows/build.yml"><img src="https://github.com/abdallahnatsheh/T-REX-FIRMWARE/actions/workflows/build.yml/badge.svg?style=flat-square"/></a>
  <img src="https://img.shields.io/badge/platform-LilyGo%20T--Deck%20%7C%20T--Deck%20Plus-blue?style=flat-square"/>
  <img src="https://img.shields.io/badge/language-C%2FC%2B%2B-yellow?style=flat-square"/>
  <img src="https://img.shields.io/badge/license-AGPL--3.0-red?style=flat-square"/>
  <img src="https://img.shields.io/badge/status-active%20development-orange?style=flat-square"/>
  <img src="https://img.shields.io/badge/device-ESP32--S3-green?style=flat-square"/>
</p>

<p align="center">
  <a href="https://abdallahnatsheh.github.io/T-REX-FIRMWARE"><b>📖 Documentation</b></a>
</p>

<p align="center">
  <b>Offensive security firmware for the LilyGo T-Deck — hacker CLI in your pocket.</b><br/>
  <i>DISCOVER. ENUMERATE. COMPROMISE.</i>
</p>

---

> **⚠️ Legal Disclaimer**
> For **authorized security testing, CTF competitions, and educational use only.**
> Using these tools against networks or devices without explicit permission is illegal.
> **Always get written permission before you test.**

---

## What is T-Rex?

T-Rex turns the LilyGo T-Deck into a pocket pentesting terminal. No menus, no GUI — just a blinking cursor, a physical keyboard, and a full suite of offensive security tools running on an ESP32-S3.

---

## Features

**📡 WiFi Attacks** — [full guide](docs/wifi-attacks.md)
- Scan, connect, monitor mode (Nets + Clients views), targeted deauth, raw PCAP capture (Wireshark-compatible), passive probe logger (MAC+SSID harvest → CSV)
- Evil Twin AP with adaptive deauth + captive portal
- Hidden SSID reveal, WPA2 handshake capture + on-device crack, PMKID capture + crack (no client needed)
- MAC spoofer, WPS flag detection
- **Beacon flood** (`bf`) — inject hundreds of fake beacon frames/sec; 5 modes: built-in list, rickroll, sequential, SD file, or clone a real network from scan; ~90-100 frames/sec, random MAC per beacon, automatic channel hopping — [full guide](docs/beacon-flood.md)
- **WiFi IDS** (`wguard`) — passive intrusion detection: deauth flood, evil twin, handshake harvest, PMKID grab, auth flood, probe storm, beacon flood, BSSID cloning, Karma attack; background mode with shield icon + popup alerts; session CSV logs with session-relative timestamps

**📻 ESP-NOW Radio** — [full guide](docs/espnow.md)
- **Off-grid chat** (`ec`) — encrypted peer-to-peer messaging over ESP-NOW; no router, no WiFi association, 200 m+ LOS range; any ESP32/ESP8266 device can join the public channel; private mode uses AES-128 (CCMP) with PIN-derived LMK; 3-attempt PIN validation via encrypted round-trip — wrong PIN silently dropped by hardware; contacts saved to SD (persistent) or RAM (session-only, cleared on reboot); background mode (`ec bg`) with popup bar + notification sounds; timestamps, scroll slider, unread badge, private header shows contact name — [full guide](docs/espnow.md)
- **ESP-NOW sniffer** (`es`) — passive action-frame capture; CSV + PCAP output; channel hop or lock; filter, detail view
- **ESP-NOW diagnostic** (`est`) — broadcast ping every 2 s, RX log, channel select
- **Walkie-talkie** (`ev`) — half-duplex HD voice over ESP-NOW; ITU-T G.722 wideband codec (16 kHz, 64 kbps) via vendored libg722; push-to-talk toggle (SPACE); receiver shows `>> RECEIVING <mac>`; Roger "over" beep on both ends at end of transmission (explicit EOT marker + silence-timeout fallback); live mic-level meter; app-local RX volume (`+/-`) and TX mic gain (`o/p`) that never touch global volume; no router, no pairing — any T-Deck on the same channel hears you — [full guide](docs/espnow.md)

**🔑 WiFi Credentials** — [full guide](docs/wifi-credentials.md)
- Saved credential manager (`wifipass`) — view all saved passwords
- Auto-save on connect, NVS + SD dual storage
- Linux `wpa_supplicant.conf` bidirectional sync — share networks between T-Deck and Linux
- Desktop Linux migration via `nmcli` + `wpa_passphrase`

**🌐 Network** — [full guide](docs/network.md)
- ARP host discovery, TCP port scan (4× parallel), top-31 ports
- ICMP ping, banner grabber (HTTP/TLS/MySQL/Redis), OS fingerprinting
- **SSH client** (`ssh`) — real interactive SSH via libssh; modern crypto (curve25519/AES/ChaCha20, HW-accelerated); password auth + PTY shell; 16-colour terminal with trackpad scrollback — [full guide](docs/ssh.md)

**🔵 Bluetooth** — [full guide](docs/bluetooth.md)
- BLE device scanner (paginated, RSSI, name)
- **BLE GATT enumeration** (`bi`) — connect to any BLE device and read its full service/characteristic tree; 0x2901 user descriptions, 0x2904 type auto-decode (uint8/16/32, int8/16, UTF-8); interactive write (trackpad cursor, write-without-response fallback), fuzz (seq/rand/boundary), notify/indicate sniff with 30 s live stream + write-while-connected, pairing/bonding; **`[b]` security audit** — link posture (encrypted? Just Works vs MITM? bonded? chars readable/writable without auth) + secret-value leak scan; `bi all` sweeps every scanned device; full UUID + full hex saved to SD — [full guide](docs/bleinfo.md)
  - **Auth leak detector** (`[b]` audit) — inline risk scoring flags AES-sized binary blobs, hex-encoded secrets, and PIN-shaped values; `[b]` key shows filtered triage view
  - **Write-cap** (`[r]wcap`) — replay any captured notification back to a writable char; sniff auto-saves `.ble` packet archives to SD; load captures from previous sessions
  - **Protocol reverse engineering** — sniff baseline → trigger action on device → identify new packet → replay or write back; works on proprietary protocols with no documentation
- **Tracking detector** (`tm`) — passive BLE + WiFi probe surveillance; 60s baseline learning period; 3-gate pipeline (signature → behaviour score → GPS/time confirmation); Kalman-filtered RSSI; detects AirTag (Apple Find My), Tile, Samsung SmartTag, Chipolo, Pebblebee, and Google Find My Device tags by **service-data UUID** (the way tags actually advertise when separated), verified against AirGuard; GPS movement gate on T-Deck Plus — [full guide](docs/trackme.md)
- **Fast Pair attack** (`fp`) — scan for Fast Pair devices, flood Google FP advertisements with per-cycle MAC randomization, GATT probe (WhisperPair) to read anti-spoofing keys
- **BLE notification spam** (`bs`) — Apple Continuity (Proximity Pairing + Nearby Info popups), Google Fast Pair flood, Microsoft Swift Pair, Samsung Galaxy accessory popups
- **BLE Keyboard + Mouse** (`bk`) — T-Deck as a wireless BLE HID keyboard + mouse; same features as USB keyboard (`uk`) but over Bluetooth; MITM-protected bonding (passkey shown on screen, typed on host); tap = left click, hold = right click, hold 1.5s = exit; auto-reconnects on drop

**🤖 Claude Desktop Buddy** — [full guide](docs/buddy.md)
- **BLE remote** (`bd`) — approve/deny Claude Desktop permission prompts from the T-Deck keyboard; MITM-protected bonding (passkey shown on screen)
- Full-screen terminal popup for long commands — full text wrapped across multiple lines, no truncation
- Live session stats: tokens, level, mood, energy — all persisted to NVS
- 19 ASCII pet species with 7-state animation at 5 fps

**💾 SD Card**
- Browse, read, delete files; all attack logs saved automatically

**🔌 USB Gadget** — [full guide](docs/usb.md)
- **Mass Storage** (`um`) — expose SD card as a USB drive; read and write files from any PC with no drivers
- **USB Keyboard + Mouse** (`uk`) — T-Deck becomes a full USB input device; physical keyboard types into host, trackball moves the mouse cursor with hardware acceleration; tap = left click, hold = right click, hold 1.5s = exit
- **BadUSB / DuckyScript** (`ux`) — execute keystroke injection payloads; Flipper Zero DuckyScript v1 compatible; hyphen combos, REPEAT, `HOLD`/`RELEASE` press-and-hold chords; built-in T-Rex demo; scripts in `/apps/badusb/scripts/` on SD
- **BadBLE** (`ux ble`) — the same DuckyScript over **BLE HID** (no cable): fresh keyboard or **BLESA MAC-clone** of a bonded device, custom advertised name, fresh random MAC per session, interactive menu `[EXP]`. Live-scanning target picker (matches `sbl`, full MAC + clonable legend), `[i]` inspects a target's GATT to reveal its real name, auto-uses the target's real name for the spoof (never an exposing default), aborts cleanly if the host disconnects mid-run

**🖥️ System** — [full guide](docs/system.md)
- Man pages on-device (`man <cmd>`), paginated help, power save, Matrix animation
- Trackpad cursor — move cursor mid-command, click to execute
- **Lock screen** (`lock`) — [full guide](docs/lock.md) — idle-timeout auto-lock (keyboard **and** trackpad activity both reset the timer); hold trackpad center 3 s to lock from any app; no-PIN mode (Space ×3) or SHA-256 hashed PIN; live locked-duration counter; warns if no SD card; forgot PIN → remove SD + reboot
- **Undercover mode** (`undercover`/`uc`) `[EXP]` — silent **phone home-screen disguise** for opsec/duress (the [home launcher](#home-launcher); the Notes app lives on its grid): going covert (`g_covert`) mutes notification sounds and hides the real status bar while passive tools keep logging underneath; a secret SHA-256-hashed passphrase (typed anywhere in the cover) exits silently to the CLI; touch wakes a dimmed/off screen only while undercover. **Boot-cover** (`uc boot on`) — device boots directly into the home cover with no T-REX splash; if a lock PIN is also configured it fires after the passphrase exits, not before. `home`/`hm` runs the same cover standalone (no silent mode) for tuning. **Panic button** (`uc panic set`) arms an instant-hide key (default `@`) that drops into the cover from anywhere, even mid-command. Duress/decoy passphrase not planned
- **Home-launcher cover** (`home`/`hm`) `[EXP]` — an alternative disguise to the Notes-only cover: a modern phone-style home screen (fake status bar + live clock/weather hero + a 4×2 app grid). Only the **Notes** tile opens anything real; the rest are cosmetic. Status-bar clock/battery and the hero **weather** are real. Shares all the undercover plumbing (dim/wake, lock stand-down, secret-passphrase exit)
- **Weather** (`weather`/`wx`) — current conditions in the home hero via the **Open-Meteo** API (free, **no API key**, nothing secret on the card). Needs WiFi; location from GPS (Plus, zero-config) or a configured `lat`/`lon`. Config `/config/weather.conf` self-seeds on first boot. Weather can't be known offline — it's live server data

---

## Hardware

| Component | Details |
|-----------|---------|
| Devices | LilyGo T-Deck · LilyGo T-Deck Plus |
| MCU | ESP32-S3 (16 MB flash, 8 MB PSRAM) |
| Display | 320×240 ST7789 TFT |
| Input | Physical QWERTY keyboard (PS/2 over I2C) + trackball |
| Radio | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |
| GPS | L76K / u-blox M10Q (T-Deck Plus only) |

---

## Getting Started

**Requirements:** [VSCode](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/) extension

```bash
git clone https://github.com/abdallahnatsheh/T-REX-FIRMWARE
# Open in VSCode → select env:T-Deck or env:T-Deck-Plus → click Upload
```

> **Can't upload?** Hold the trackball button, plug in USB, then try again — this forces download mode.

---

## Commands

| Command | Short | Args | Description |
|---------|-------|------|-------------|
| `help` | `hlp` | `[cmd]` | List all commands or detail for one |
| `man` | `mn` | `<cmd>` | On-device manual page |
| `info` | `inf` | — | Device info (chip, MACs, battery, SD) |
| `show` | `sh` | `<wifi\|ble\|hosts>` | Re-display last scan without rescanning |
| `clear` | `clr` | — | Clear screen |
| `pwrsave` | `psv` | `[status\|on\|off\|set ...]` | Power save config (dim/screen-off on inactivity) |
| `sleep` | `slp` | — | Deep sleep (~240 µA) — manual only; click the trackball to wake (reboots) |
| `lock` | `lk` | `[new\|update\|clean\|timeout <s>\|status]` | Screen lock — PIN optional; hold trackpad 3 s or run `lock` to lock |
| `volume` | `vol` | `[0-100\|up\|down\|off]` | General audio volume |
| `notif` | `nf` | `[on\|off\|vol <n>\|test [lvl]\|<lvl> on\|off\|file <f>]` | Notification manager — per-level enable/disable, custom WAV, `test` sound picker |
| `tz` | `tz` | `[+HH\|-HH:MM\|<posix>\|status]` | Set device timezone (NVS, survives reboot) |
| `weather` | `wx` | `[loc <lat> <lon>\|units metric\|imperial\|now]` | Current weather (Open-Meteo, **no API key**) for the home hero — needs WiFi; GPS (Plus) or configured location; `/config/weather.conf` self-seeds |
| `home` | `hm` | `[EXP]` — | Home-launcher cover UI (phone-style home screen); real clock/battery/weather; the Notes tile opens the notes app. Runs the same cover `undercover` uses, standalone (no silent mode) |
| `undercover` | `uc` | `[EXP]` `[set\|clear\|status\|boot on\|off\|panic set\|off]` | Silent home-screen disguise — mutes notifications, hides status bar; secret passphrase exits to CLI; `boot on` boots directly into the home cover; `panic set` arms an instant-hide key (default `@`) |
| **WiFi** | | | |
| `scanwifi` | `sw` | `[on\|off]` | WiFi manager — scan/connect/disconnect/forget + radio on/off |
| `connectwifi` | `cw` | `<index\|ssid>` | Connect by scan index or SSID name |
| `wifipass` | `wp` | `[export\|clear]` | Saved WiFi creds — no arg: view all (SD + NVS); `export` → wpa_supplicant.conf; `clear` → erase all |
| `wifimon` | `wm` | `[ch]` | Monitor mode — Nets view (BSSID/CH/RSSI/clients) + Clients view (vendor/type/AP, trackpad cursor, `[d]` targeted deauth); `[s]` raw PCAP → `/apps/wifimon/NNN_YYYYMMDD_HHMMSS.cap`; `[p]` probe logger → `/apps/wifimon/probes.csv` (MAC+SSID harvest, deduped) |
| `deauth` | `da` | `<bssid\|#> [ch] [client]` | Deauth attack |
| `eviltwin` | `et` | — | Evil Twin AP + captive portal |
| `hiddenssid` | `hs` | `<idx\|bssid> [ch] [silent]` | Reveal hidden SSID |
| `macchanger` | `mc` | `on\|off\|random\|set <mac>` | Spoof STA MAC |
| `wpasniff` | `ws` | `<idx\|bssid> [ch]` | Capture + crack WPA2 handshake (needs client + deauth) |
| `pmkid` | `pm` | `<idx\|bssid> [ch]` | PMKID capture + crack — passive, no client or deauth needed |
| `wpa3down` | `w3d` | `[idx]` | **[EXP]** WPA3 transition-mode downgrade — deauth victim + WPA2-only rogue AP (same SSID) → crackable handshake `.cap`. Targets `WPA3/TD` APs from the last `sw` scan |
| `karma` | `km` | `[auto\|hs\|portal <ssid>]` | Rogue-AP suite — probe harvest + PNL device fingerprinting; `[h]` WPA2 half-handshake bait (manual EAPOL responder → crackable `.cap`), `[p]` captive portal, `[s]` save tables; `km auto` = hands-free harvest→bait sweep (`[v]` lists captured nets) logging engagements to `connects.csv` |
| `crack` | `cc` | `[cap] [wordlist\|dir]` | Offline WPA/WPA2 crack of a `.cap` (handshake or PMKID) — SD wordlists, a dir of lists, or built-in; `cd`-relative paths |
| `wguard` | `wg` | `<idx\|bssid> [ch] [bg]` | WiFi IDS — passive intrusion detection; `wg stop` / `wg view` |
| `beaconflood` | `bf` | `[list\|rickroll\|seq <base>\|file [path]\|clone]` | Beacon flood — fake AP injection; interactive mode picker; clone mirrors real network security |
| `wardrive` | `wd` | — | Wardriving (Plus only) — continuous WiFi scan + GPS → WiGLE 1.4 CSV; one row per AP per session; logs only while a fix is valid |
| `espsniff` | `es` | `[ch 1-13]` | Passive ESP-NOW frame sniffer — CSV + PCAP output, filter, detail view |
| `esptest` | `est` | `[ch 1-13]` | ESP-NOW TX/RX diagnostic — broadcasts every 2 s, shows RX log |
| `espchat` | `ec` | `[pub\|prv\|bg\|stop] [ch]` | Off-grid ESP-NOW chat — public broadcast (any ESP32 compatible) or private AES-128 encrypted 1:1; `ec bg` runs in background |
| `espvoice` | `ev` | `[ch 1-13]` | ESP-NOW walkie-talkie — half-duplex G.722 HD voice; SPACE = push-to-talk toggle; Roger beep + `RECEIVING` indicator; app-local volume (`+/-`) & mic gain (`o/p`) |
| **Network** | | | |
| `netdiscover` | `nd` | — | ARP scan local /24 |
| `netspy` | `ns` | `gtk` · `dump` | **[EXP]** Client-isolation device recon — finds devices `nd` can't see by passively parsing broadcast/multicast (ARP/IPv4 + DHCP/mDNS/SSDP) for IP, name + services; never transmits. Connect (`cw`) first. AirSnitch technique |
| `isoscan` | `is` | `[ns#] [auto\|inject\|bounce\|portdown\|mitm\|portup\|dns]` · `cctest` | **[EXP]** ACTIVE isolation audit (**transmits**) — CCMP-encrypts a broadcast frame with the live GTK + spoofs the AP MAC to reach clients past isolation. `auto` = smart probe → detect L2/L3 → recommend an attack (**start here**), `inject` = GTK broadcast ARP → reach victim (HW-proven), `bounce` = ARP reachability (works vs Windows firewall), `portdown` = capture victim frames → SD pcap, `mitm` = ARP poison + capture with a sustained-rate verdict `[exp]`, `portup` = gateway ARP-poison `[exp]`, `dns` = ICMPv6 RA DNS poison + dual-stack UDP-53 logger (live query list) `[exp]` — **IPv6-only, needs a network that gives clients real IPv6; does nothing on IPv4-only nets / phone hotspots**, `cctest` = crypto self-test. Confirms before firing; targets a netspy row. **Real traffic MITM is not achievable on a single radio** — the `[exp]` attacks report honestly whether a poison holds (usually it doesn't). Best use: audit that "client isolation" is bypassable + device recon + injection. Own networks only |
| `portscan` | `ps` | `<ip\|#\|ns#> <start> <end>` · `top <ip\|#\|ns#>` | TCP port scan; `ps top` scans the 26 common ports. `#` = netdiscover index, `ns#` = netspy index |
| `ping` | `pg` | `<ip\|hostname\|#\|ns#>` | Continuous ICMP ping — rolling results + live sent/recv/loss + min/avg/max RTT; `#` = netdiscover index, `ns#` = netspy index; `q` stops |
| `ssh` | `sc` | `<ip\|name> [user]` | Interactive SSH client (libssh) — password auth, PTY shell, 16-colour terminal + trackpad scrollback; saved host profiles (`ssh save/list/rm`); connect WiFi (`cw`) first |
| **Bluetooth** | | | |
| `scanblue` | `sbl` | — | BLE device scan |
| `bleinfo` | `bi` | `<index\|mac\|all>` | GATT enum: services, chars, values, write, fuzz, sniff, pair, `[b]` security audit |
| `trackme` | `tm` | `[silent]` | Anti-tracking detector |
| `fastpair` | `fp` | `[scan\|spam\|h <idx>\|h all]` | Fast Pair: scan devices / flood ads / GATT hijack |
| `blespam` | `bs` | `[apple\|android\|ms\|samsung\|all]` | BLE notification spam (popups on nearby devices) |
| `buddy` | `bd` | `[name]` | Claude Desktop remote — approve prompts, ASCII pet, NVS stats |
| `btkbd` | `bk` | — | T-Deck as BLE keyboard + mouse (MITM-bonded, passkey on screen) |
| **SD Card** | | | |
| `sdinfo` | `sdi` | — | SD card info |
| `sdls` | `ls` | `[path]` | List directory (CWD if no path, paginated, dirs in cyan) |
| `cd` | `cd` | `<dir\|..>` | Change working directory — `cd badusb`, `cd ..`, `cd /` |
| `cat` | `cat` | `<path>` | Read file — scrollable viewer, tpad UP/DN, `q` quit |
| `edit` | `ed` | `<path>` | nano-style text editor — type to edit, tpad=cursor, click=menu (save/find/exit) |
| `rm` | `rm` | `<path>` | Delete file (relative to CWD); `rm -d <dir>` removes a directory + contents (asks y/N) |
| `sdformat` | `sdf` | `[init]` | Format SD to FAT32 (`sdf init` also recreates directory structure) |
| **USB** | | | |
| `usbmsc` | `um` | — | Expose SD card as USB Mass Storage drive |
| `usbkbd` | `uk` | — | T-Deck as USB keyboard + mouse (trackball = cursor, tap = left click, hold = right click) |
| `jiggle` | `jg` | — | Mouse jiggler — nudges cursor ±2 px every 30 s to prevent host screen lock |
| `usbexec` | `ux` | `[ble [clone <mac\|#>]] demo\|<path>` | BadUSB (USB) + BadBLE (`ux ble`: BLE HID, clone/name/interactive) |
| **Diagnostics** | | | |
| `gps` | `gps` | `on\|off\|test` | GPS task control + coordinate test (T-Deck Plus) |
| `test` | `tst` | `<spk\|mic\|lora>` | Hardware self-tests — `spk` speaker tones, `mic` ES7210 (level meter, VAD, record+replay), `lora` SX1262 diagnostic |
| `csidetect` | `csi` | — | **[EXPERIMENTAL]** WiFi **CSI motion detector** (sweep-style display, not an actual radar) — senses movement from WiFi signal disturbance (needs `cw`). Motion energy only, no direction |
| `MATRIX` | `matrix` | — | Matrix rain animation |

> **Tip:** Run `nd` first, then use the host index in `ps` / `ps top` instead of typing the IP.

---

## Navigation

All scan tables share the same keys:

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-run scan |
| `q` | Quit |

**Trackball (command line):**

| Direction | Action |
|-----------|--------|
| Left / Right | Move cursor within the current command |
| Up / Down | Scroll through command history (16 entries) |
| Click | Execute command |
| Double-click | Toggle screen off / on |
| Hold 3 seconds | Lock screen (works from any screen) |

**Backspace auto-repeat:** Hold Backspace for **1.5 seconds** → auto-deletes at ~16 chars/sec. Press any char key to stop — the timer resets immediately so the next hold starts fresh. Pressing Backspace a second time while repeat is armed cancels it (tap safety).

**Autocomplete:** Press `'` (Sym+K) at any point in a command.
- At the start → completes command names and short names (`netd` + `'` → `netdiscover`; `por` + `'` → `portscan`)
- After a command with file args → completes file/dir paths from the current working directory
- After a command with subcommands → shows valid args in yellow (`psv ` + `'` → `on off status save reset set`; `ssh ` + `'` → `save list rm`)
- Two-level context: `psv set ` + `'` → shows settable options; `mc target ` + `'` → `wifi bt both`
- Smart filtering: `cd` suggests dirs only, `rm`/`ux` suggest files only, `ls`/`cat` suggest both
- Fills common prefix automatically (Linux-style); single match adds a trailing space

---

## SD Layout

```
/wpa_supplicant.conf      — saved WiFi credentials (Linux-compatible format)
/wpa_supplicant.bak       — auto-backup of original file before T-Rex modifies it
/config/                  — device-wide settings: pwrsave, macchanger, lockscreen, notif, clock
/config/notification/     — shared per-level alert WAVs (16-bit PCM, 22050Hz, mono)
/apps/                    — one self-contained folder per command (logs, captures, wordlists, config)
/apps/eviltwin/creds.csv  — captured portal credentials
/apps/eviltwin/portal/    — custom HTML portal templates
/apps/trackme/            — session.csv, known.csv, signatures.csv
/apps/hiddenssid/found.csv — discovered hidden SSIDs
/apps/wpasniff/           — wordlist.txt, <BSSID>.cap, cracked.csv
/apps/pmkid/              — wordlist.txt, <BSSID>.cap, cracked.csv
/apps/karma/              — <ssid>.cap (half-handshake, never overwritten), cracked.csv, creds.csv, connects.csv (auto-mode engagements), wordlist.txt, NNN.csv (saved tables), portal/*.html
/apps/capcrack/cracked.csv — offline cap-cracker results (crack/cc)
/apps/wifimon/            — raw 802.11 PCAP files (NNN.cap) + probes.csv
/apps/wardrive/           — WiGLE-format wardrive logs (001.csv, 002.csv … — never overwritten)
/apps/wguard/             — wguard session files (001.csv, 002.csv … — never overwritten)
/apps/beaconflood/wordlist.txt — custom SSID list for bf file
/apps/bmon/               — BLE advertisement logs (NNN.csv)
/apps/i2cscan/results.csv — I2C scanner results
/apps/fastpair/           — keys.csv, paired.csv, sniff.csv
/apps/espsniff/           — ESP-NOW captures (NNN.csv + NNN.pcap)
/apps/bleinfo/            — GATT dumps (<mac>.txt), sniff logs (<mac>_sniff.txt), write-cap archives (<mac>_replay.ble)
/apps/espchat/            — contacts.csv, config.conf, pub/, prv/ chat logs
/apps/badusb/scripts/     — DuckyScript payload files
```

> See the [SD Card guide](docs/sdcard.md) for the full file reference and quick-start checklist.

---

## Roadmap

- [x] WiFi scan, connect, monitor, deauth, Evil Twin, hidden SSID, MAC spoof, WPA2 handshake capture+crack, PMKID capture+crack (passive, no client needed), WPS flag, saved credential manager, Linux wpa_supplicant.conf sync
- [x] ARP discovery, port scan, ping, banner grabber, OS fingerprinting
- [x] `netspy` **[EXP]** — passive client-isolation device recon: finds devices ARP can't see by parsing AP-relayed broadcast/multicast (ARP/IPv4 + DHCP/mDNS/SSDP) for IP, hostname + services; never transmits (AirSnitch technique)
- [x] `isoscan` **[EXP]** — active isolation audit (AirSnitch Stage 2, transmits): software AES-CCMP-encrypts a broadcast frame with the live GTK + spoofs the AP MAC to inject at clients past isolation. `auto` smart-probes and recommends an attack; `inject` GTK broadcast ARP (HW-proven) reaches a victim; `bounce` ARP reachability (works vs Windows firewall); `portdown` captures victim frames→SD pcap; `mitm`/`portup`/`dns` `[exp]` attempt poison/RA-DNS and honestly report whether it holds. **Real traffic MITM is not achievable on a single radio** (AirSnitch's interception needs port-stealing on a 2nd BSSID = 2 radios) — best use is proving a network's "client isolation" is bypassable + recon
- [x] BLE scanner, anti-tracking detector
- [x] BLE GATT enumeration (`bi`) — full service/char tree, 0x2904 auto-decode, write (trackpad cursor, write-without-response fallback), fuzz, notify/indicate sniff with write-while-connected, pairing/bonding, `bi all` sweep, full UUID + full hex saved to SD
- [x] BLE security audit (`[b]` audit view) — access-control posture (link encrypted? Just Works vs MITM? bonded? chars readable/writable without auth) + value leak scan (AES blobs / hex secrets / PINs)
- [x] BLE write-cap (`[r]wcap`) — replay captured notification values back to writable chars; `.ble` SD archive; protocol reverse engineering workflow
- [x] Fast Pair attack — advertisement flood + GATT probe (WhisperPair)
- [x] BLE notification spam — Apple / Android / Microsoft / Samsung
- [x] SD file manager — `ls` (paginated, dirs in cyan), `cd` CWD navigation (relative paths for all SD commands), man pages, help, power save, trackpad cursor
- [x] Command history (16-entry ring buffer, trackpad UP/DOWN), tab autocomplete with smart per-command filtering (Sym+K)
- [x] LoRa diagnostic, GPS (T-Deck Plus)
- [x] USB Mass Storage — expose SD card as USB drive (read + write, 2MB file tested)
- [x] USB Keyboard + Mouse — T-Deck physical keyboard + trackball as full USB HID input device
- [x] Mouse Jiggler — USB HID nudge every 30 s to prevent host screen lock
- [x] BLE Keyboard + Mouse (`bk`) — wireless BLE HID; MITM-bonded passkey pairing; same features as USB keyboard; auto-reconnects on drop
- [x] Claude Desktop Buddy — BLE remote, permission prompts, ASCII pet, NVS stats; MITM-bonded passkey pairing
- [x] BadUSB / DuckyScript — Flipper Zero DuckyScript v1 compatible, hyphenated combos, REPEAT, HOLD/RELEASE, built-in demo
- [x] BadBLE — same DuckyScript over BLE HID (`ux ble`); fresh keyboard or BLESA MAC-clone, custom/spoofed name, fresh random MAC per session, live-scanning interactive picker with `[i]` GATT inspect + auto spoof-name, mid-run disconnect abort
- [x] `wguard` WiFi IDS — deauth flood, evil twin (two-tier RSSI-filtered detection), handshake harvest, PMKID grab, auth flood, probe storm, beacon flood, BSSID cloning, Karma attack; background mode with shield icon + popup bars; session CSV logs (session-relative timestamps, no duplicate events across save blocks)
- [x] `wardrive` (Plus only) — continuous synchronous WiFi scan + GPS → WiGLE WiFi-1.4 CSV (`/apps/wardrive/NNN.csv`, never overwritten); waits for the first GPS fix (radio idle) before scanning, logs each BSSID once per session only while a fix is valid; real altitude + HDOP accuracy; file created lazily on the first AP (no empty files); method adapted from Bruce, verified against the official WiGLE 1.4 spec
- [x] Notification manager — I2S WAV playback from SD, per-level volume, screen wake callback; wired into Buddy, TrackMe, wguard
- [x] Lock screen — idle-timeout auto-lock (keyboard + trackpad both reset timer) + hold-trackpad-3s trigger; no-PIN (Space ×3) or SHA-256-hashed PIN (salt via esp_random, mbedTLS); live locked-duration HH:MM:SS; yellow warning when no SD card; recovery = remove SD + reboot; status bar stays live (clock/WiFi/battery update every 1 s while locked)
- [x] Lock screen display blocking — all interactive apps (`buddy`, `wguard`, `trackme`, `beaconflood`, `cat`, `ls`, etc.) correctly freeze on lock and fully restore on unlock
- [x] Backspace hold-repeat redesigned — cold/hot state via `_lastBsReturnMs`; char cancel resets timer to cold so next hold is immediately available; 1500ms hold delay; second tap while armed cancels (prevents accidental auto-delete on rapid taps); same fix in CLI, USB keyboard, and BLE keyboard
- [x] Full docs overhaul — 44 pages, every command documented; Getting Started, Keyboard Reference, Workflows, Troubleshooting, T-Deck vs T-Deck Plus guides; wguard + trackme algorithm docs with academic references; clean nav hierarchy (Just the Docs parent/child/grandchild)
- [ ] LoRa packet logger
- [ ] MAC proximity watchlist
- [ ] DNS enumeration
- [ ] `bmon` — passive BLE advertisement sniffer: cleartext detector, iBeacon/Eddystone parser, PCAP export to SD (Wireshark compatible)

---

## Screenshots

| Main Screen | WiFi Scanner | Network Scan |
|-------------|-------------|--------------|
| ![main](images/1.jpg) | ![wifi](images/3.jpg) | ![net](images/4.jpg) |

---

## Dependencies

All install automatically via PlatformIO `lib_deps`:
LovyanGFX · ESP32Ping · NimBLE-Arduino (h2zero) · Digital Rain Anim · Pangodream 18650CL · RadioLib · ArduinoJson · AceButton · TouchLib · TinyGPS++

---

## Contributing

PRs and issues welcome. To add a new command or attack module:
1. Open an issue describing the feature
2. Fork, branch, implement
3. Submit a PR referencing the issue

---

## Credits

- [Bruce Firmware](https://github.com/pr3y/Bruce) — an awesome open source project, parts were adapted and built upon (AGPL-3.0)
- [LilyGo T-Deck](https://github.com/Xinyuan-LilyGO/T-Deck) — hardware reference
- [AirGuard](https://github.com/seemoo-lab/AirGuard) — anti-tracking research (TU Darmstadt)
- Built with [ChatGPT](https://chatgpt.com) · [GitHub Copilot](https://github.com/features/copilot) · [Claude Code](https://claude.ai/code)

---

<p align="center">
  <i>T-Rex — your network never saw it coming.</i>
</p>
