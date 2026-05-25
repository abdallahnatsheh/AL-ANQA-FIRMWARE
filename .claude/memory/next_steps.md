---
name: Next Steps
description: Ordered feature queue — priority order
type: project
---

## Already implemented (do NOT re-add)
`beaconflood/bf` · `bleinfo/bi` · `usbkbd/uk` · `usbexec/ux` (BadUSB) · `clock/ClockManager` · `buddy/bd` · `wguard/wg` · `hiddenssid/hs` · `blespam/bs`

---

## WiFi Pentest

1. **PMKID attack** — capture PMKID from single EAPOL frame (no client needed); reuse `wpasniff` crack logic. Command: `pmkid/pm <idx|bssid> [ch]`
2. **Karma / MANA** — softAP responding to any probe with matching SSID; log probing clients; `[p]` attach captive portal. Command: `karma/km`
3. **ARP Poisoning** — MITM via fake ARP replies. Command: `arpspoof/as <victim-ip> <gw-ip>`
4. **LAN MITM (Gateway Takeover)** — join real net as STA, ARP poison all devices + GW, relay traffic, sniff DNS/HTTP, `[b]` block / `[t]` throttle / `[d]` DNS spoof. Command: `lanmitm/lm`
5. **AP Bridge** — transparent AP+STA bridge; clients get real internet; sniff + redirect. Command: `apbridge/ab [ssid]`
6. **Responder** — LLMNR/NBT-NS/mDNS poisoner; capture NTLMv2 hashes. Command: `responder/rsp`
7. **SSH Connect** — SSH client from `nd` scan or IP. Command: `sshcon/sc <ip|#> [user]`
8. **TCP Listener/Client** — catch reverse shells / forward input. `tcplisten/tl <port>` · `tcpclient/tc <ip> <port>`
9. **DPWO** — default credential checker against discovered hosts. Command: `dpwo/dw <ip|#>`

## Bluetooth

10. **BadBLE** — connect as BLE HID keyboard; inject DuckyScript. Command: `badble/bb <mac|#> <script>`
11. **macwatch** — see `project_macwatch_idea.md` for full spec. Command: `macwatch/mw`
12. **BT Command Relay** — send CLI command to remote T-Deck over BLE NUS; output streams back. `btcmd/btc <mac> <cmd>`

## GPS / T-Deck Plus Only

13. **Wardriving** — continuous WiFi scan + GPS log → WiGLE CSV 1.4. Command: `wardrive/wd`
14. **GPS Tracker** — log coords + timestamp every N seconds. Command: `gpstracker/gtr [interval_s]`

## USB Attacks

15. **Mouse Jiggler** — `jiggle/jg`; move mouse ±1px every ~30s; trivial (HID already wired)
16. **Auto OS Detection** — detect Windows/macOS/Linux on USB connect; auto-select script folder for `ux`
17. **Remote BadUSB via WiFi** — `rbadusb/rb`; HTTP AP to trigger scripts from phone
18. **Keylogger Mode** — `keylog/kl`; USB HID host-direction capture to SD

## Other

19. **VNC Client** — connect to VNC server; 320×240 scaled. Library: `moononournation/ArduinoVNC`. Command: `vnc/vn <ip> [port] [pw]`
20. **QR Code** — render QR from text/URL/WiFi cred. Command: `qrcode/qr <text>`
21. **LoRa scanner** — SX1262 receive; 433 MHz T-Deck / 915 MHz Plus; log to `/logs/lora.csv`. Command: `lorascan/ls`
22. **Audio Config** — persistent vol + on/off for I2S alerts. Command: `audio/au [on|off|vol <0-100>]`
23. **Chromecast Control** — Cast API port 8009; `cast rickroll` for all Chromecasts on LAN. Command: `cast/ca`
24. **bmon** — passive BLE ad sniffer; iBeacon/Eddystone/cleartext; PCAP linktype 251. Command: `bmon/bm`

## Low Priority

25. **Mic Record** *(Plus only)* — ES7210 → `/logs/rec_<ts>.wav`. Command: `micrec/mr`
26. **NES Emulator** *(Easter egg)* — Nofrendo; ROMs from `/roms/*.nes`
