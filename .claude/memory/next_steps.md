---
name: Next Steps
description: Ordered feature queue — priority order, cut worthless items
type: project
---

## WiFi Pentest

1. **WPS flag in scanwifi** — detect WPS IE (tag 0xDD OUI 00:50:F2:04) in beacon frames; show cyan `[WPS]` tag in scan table.

2. **PMKID attack** — capture PMKID from single EAPOL frame (no client needed); reuse `wpasniff` crack logic. Command: `pmkid/pm <idx|bssid> [ch]`. Save to `/logs/pmkid.csv`.

3. **Karma / MANA** — softAP responding to any probe with matching SSID; log probing clients to `/logs/karma.csv`; `[p]` to attach captive portal. Command: `karma/km`.

4. **ARP Poisoning** — fake ARP replies to poison victim + gateway; MITM redirect; `q` stops + sends gratuitous ARP to restore. Command: `arpspoof/as <victim-ip> <gateway-ip>`.

5. **LAN MITM (Gateway Takeover)** — T-Deck joins real network as STA; ARP poisons ALL devices + gateway simultaneously; enables lwip `IP_FORWARD` to relay traffic transparently (devices keep internet); sniff DNS + HTTP from forwarded traffic; `[b]` block a device (drop MAC); `[t]` throttle a device (rate-limit); `[d]` DNS spoof; log to `/logs/lanmitm.csv`; `q` restores all ARP tables. Command: `lanmitm/lm`. Works on home networks (5–15 devices); heavy on ESP32 at scale.

6. **AP Bridge (Transparent Gateway)** — T-Deck creates AP with custom SSID; STA connects to real internet simultaneously; DHCP assigns T-Deck as default gateway; all connecting devices get real internet through T-Deck; sniff DNS + HTTP; `[b]` block device; `[t]` throttle; `[d]` DNS spoof + redirect to captive portal; log to `/logs/apbridge.csv`. Advanced Evil Twin — transparent, clients have internet, no captive portal suspicion unless you trigger it. Command: `apbridge/ab [ssid]`.

7. **Responder** — LLMNR/NBT-NS/mDNS poisoner; capture NTLMv2 hashes from Windows name resolution; log to `/logs/responder.csv`. Command: `responder/rsp`.

6. **SSH Connect** — SSH client to host from `nd` scan or IP; keyboard forwarded, output displayed. Command: `sshcon/sc <ip|#> [user]`.

7. **TCP Listener / Client** — `tcplisten/tl <port>` catches reverse shells; `tcpclient/tc <ip> <port>` connects and forwards input.

8. **SOCKS4 Proxy** — T-Deck as SOCKS4 proxy; route laptop traffic through T-Deck WiFi. Command: `socks4/s4 [port]`.

9. **DPWO** — default credential checker against discovered hosts via HTTP; log successes to `/logs/dpwo.csv`. Command: `dpwo/dw <ip|#>`.

10. **Beacon flood** — spam fake SSIDs; modes: `random` / `funny` / `rickroll` / `custom <ssid>`; raw 802.11 inject. Command: `beaconflood/bf [mode] [count]`.

11. **WiFi Password Recovery** — list all SSIDs + saved passwords from NVS (Preferences). Command: `wifipass/wp`.

## Bluetooth

12. **BLE Spam** — flood BLE ads spoofing Apple/Samsung/Windows/SwiftPair; endless pairing popups. Command: `blespam/bs [apple|samsung|windows|all]`.

13. **Bluetooth Jammer** — flood all 3 BLE advertising channels (37/38/39) simultaneously with rapid-fire advertisements to saturate the channel and disrupt nearby BLE discovery/pairing; measure effective range; `q` to stop. For authorized testing only. Command: `btjam/bj`. Note: ESP32 can't do true RF jamming — this is advertisement channel flooding via NimBLE rapid-advertise.

14. **BLE GATT enumeration** — connect to MAC from last `scanblue`, walk services→characteristics, print UUID + properties + readable values. Command: `bleinfo/bi <index>`.

14. **BadBLE** — connect as BLE HID keyboard; inject keystrokes from DuckyScript on SD. Command: `badble/bb <mac|#> <script>`.

15. **macwatch** — WiFi probe + BLE MAC watchlist; alert when target MAC enters range. Command: `macwatch/mw`.

16. **BT Command Relay** — send CLI command string to a remote T-Deck over BLE UART (Nordic NUS); remote executes + streams output back in chunks; useful for coordinating attacks between two T-Decks. Commands: `btcmd/btc <mac> <command>` (sender) + auto-listen mode on receiver.

## GPS / T-Deck Plus Only

16. **Wardriving** — continuous WiFi scan while moving; log SSID/BSSID/RSSI/lat/lon to `/logs/wardriving.csv` in WiGLE CSV 1.4 format. Command: `wardrive/wd`.

17. **GPS Tracker** — log coordinates + timestamp to `/logs/gps_track.csv` every N seconds; summary on exit. Command: `gpstracker/gtr [interval_s]`.

## Other (high priority first)

18. **VNC Client** — connect to VNC server; display scaled to 320×240; keyboard + trackball as mouse. Library: `moononournation/ArduinoVNC`. Command: `vnc/vn <ip> [port] [password]`.

19. **BadUSB** — TinyUSB HID; parse DuckyScript from SD `/scripts/*.duck`. Command: `badusb/bu <script>`.

20. **QR Code generator** — render QR on screen from text/URL/WiFi credential. Command: `qrcode/qr <text>`.


22. **LoRa scanner** — SX1262 receive mode; 433 MHz on T-Deck (`env:T-Deck`), 915 MHz on T-Deck Plus (`env:T-Deck-Plus`); use `#ifdef BOARD_TDECK_PLUS` to select frequency; log packets to `/logs/lora.csv`. Command: `lorascan/ls`. Note: two devices are on different frequencies so cannot relay to each other — BT relay is the inter-device channel.

23. **Audio Config** — persistent sound on/off + volume (0–100); affects all I2S alerts. Command: `audio/au [on|off|vol <0-100>]`.

24. **Chromecast Control** — discover all Google Cast devices on LAN via mDNS (`_googlecast._tcp.local`); connect to port 8009 (TLS + Cast protobuf); control: pause/stop/volume/cast URL; no ARP poison needed — open local API, just needs to be on same WiFi. Command: `cast/ca [discover|pause|stop|vol <0-100>|play <url>|rickroll]`. Prank mode: `cast rickroll` — load YouTube app (Cast app ID `233637DE`) with video ID `dQw4w9WgXcQ`, set volume to 100 on ALL discovered Chromecasts simultaneously. Log discovered devices.

## Low Priority

24. **Clock** — NTP sync if WiFi up; GPS time fallback (Plus); live clock display. Command: `clock/ck`.

25. **Mic Record** *(T-Deck Plus only)* — record ES7210 mic to `/logs/rec_<ts>.wav`; `q` to stop. Command: `micrec/mr`.

26. **JavaScript scripting engine** — mJS or Duktape; run `/scripts/*.js` from SD; hardware API bindings. Command: `jsrun/jr <script>`. Research RAM usage before starting.

27. **NES Emulator** *(Easter egg)* — Nofrendo engine; ROMs from `/roms/*.nes`; WASD=D-pad, Z/X=B/A, Enter=Start; I2S audio on Plus. Command: `nes/nes [rom]`.

28. **USB Keyboard** — T-Deck as USB HID keyboard via TinyUSB; read I2C keyboard (0x55) → forward as USB HID keycodes to connected PC. Command: `usbkbd/uk`.

29. **Bluetooth Keyboard** — T-Deck as BLE HID keyboard; pair with phone/PC; physical keyboard input forwarded wirelessly; useful for typing on a target device from T-Deck. Command: `btkbd/bk`.
