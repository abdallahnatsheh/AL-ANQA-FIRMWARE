---
name: Next Steps
description: Ordered feature queue — priority order
type: project
---

## Already implemented (do NOT re-add)
`beaconflood/bf` · `bleinfo/bi` · `usbkbd/uk` · `usbexec/ux` (BadUSB) · `clock/ClockManager` · `buddy/bd` · `wguard/wg` · `hiddenssid/hs` · `blespam/bs` · `jiggle/jg` (mouse jiggler) · `fast_pair/fp` (Google Fast Pair scan/spam/hijack) · `show/sh` (last scan results) · `tz` (timezone config) · `volume/vol` (I2S volume) · `notif/nf` (per-level sound config) · `wifimon/wm` (airmon-ng rewrite: nets+clients views, targeted deauth, raw PCAP, probe logger `[p]` → `/apps/wifimon/probes.csv`) · `oui_lookup.h` (shared ~350-entry vendor+type table) · `pmkid/pm` (PMKID capture+crack, no client needed, passive M1 sniff). SD layout
is now `/apps/<tool>/` + `/config/` (v2 reorg) — see `project_sdcard_reorg_v2.md`.

---

## WiFi Pentest

1. **Karma / MANA** — softAP responding to any probe with matching SSID; log probing clients; `[p]` attach captive portal. Command: `karma/km`
2. **ARP Poisoning** — MITM via fake ARP replies. Command: `arpspoof/as <victim-ip> <gw-ip>`
3. **LAN MITM (Gateway Takeover)** — join real net as STA, ARP poison all devices + GW, relay traffic, sniff DNS/HTTP, `[b]` block / `[t]` throttle / `[d]` DNS spoof. Command: `lanmitm/lm`
5. **AP Bridge** — transparent AP+STA bridge; clients get real internet; sniff + redirect. Command: `apbridge/ab [ssid]`
6. **Responder** — LLMNR/NBT-NS/mDNS poisoner; capture NTLMv2 hashes. Command: `responder/rsp`
7. **SSH Connect** — SSH client from `nd` scan or IP. Command: `sshcon/sc <ip|#> [user]`
8. **TCP Listener/Client** — catch reverse shells / forward input. `tcplisten/tl <port>` · `tcpclient/tc <ip> <port>`
9. **DPWO** — default credential checker against discovered hosts. Command: `dpwo/dw <ip|#>`

## Bluetooth

10. **BadBLE** — BLE HID spoofing + DuckyScript injection. Command: `badble/bb <mac|#> <script>`
    - Clone MAC + HID profile of a bonded keyboard from last `sbl` scan
    - Optional forced disconnect phase: flood BLE adv channels / 2.4GHz to drop real keyboard
    - Host auto-reconnects to cloned MAC; exploits BLESA (2020) — unpatched Android/old iOS/some Windows accept unencrypted HID reconnect → keystrokes injected before re-auth
    - Patched devices require valid LTK → connection drops on encrypt failure (no injection)
    - NimBLE: set MAC before `init()` via `esp_wifi_set_mac` equivalent for BLE
11. **macwatch** — see `project_macwatch_idea.md` for full spec. Command: `macwatch/mw`
12. **BT Command Relay** — send CLI command to remote T-Deck over BLE NUS; output streams back. `btcmd/btc <mac> <cmd>`

## GPS / T-Deck Plus Only

13. **Wardriving** — continuous WiFi scan + GPS log → WiGLE CSV 1.4. Command: `wardrive/wd`
14. **GPS Tracker** — log coords + timestamp every N seconds. Command: `gpstracker/gtr [interval_s]`

## USB Attacks

15. **Auto OS Detection** — detect Windows/macOS/Linux on USB connect; auto-select script folder for `ux`
16. **Remote BadUSB via WiFi** — `rbadusb/rb`; HTTP AP to trigger scripts from phone
17. **Keylogger Mode** — `keylog/kl`; USB HID host-direction capture to SD

## Other

18. **VNC Client** — connect to VNC server; 320×240 scaled. Library: `moononournation/ArduinoVNC`. Command: `vnc/vn <ip> [port] [pw]`
19. **QR Code** — render QR from text/URL/WiFi cred. Command: `qrcode/qr <text>`
20. **LoRa scanner** — SX1262 receive; 433/868/915 MHz; log RSSI/SNR/payload to `/apps/lorascan/lora.csv`. Command: `lorascan/ls` (note: `lt` has live RX display but no SD logging yet)
21. **Chromecast Control** — Cast API port 8009; `cast rickroll` for all Chromecasts on LAN. Command: `cast/ca`
22. **bmon** — passive BLE ad sniffer; iBeacon/Eddystone/cleartext; PCAP linktype 251. Command: `bmon/bm`

## Low Priority

23. **Mic Record** — ES7210 mic; both boards; record to `/apps/micrec/rec_<ts>.wav`. Command: `micrec/mr`
24. **NES Emulator** *(Easter egg)* — Nofrendo; ROMs from `/roms/*.nes`
