# T-REX Firmware — Feature Roadmap

## Already Implemented (verified 2026-06-09)

| Command | Notes |
|---------|-------|
| `bmon` / `bm` | Passive BLE advertisement sniffer — iBeacon, Eddystone-UID/URL/TLM, cleartext names; 7-row table UI with trackpad row selection + extended detail pane; `/logs/bmon/NNN.csv` |
| Banner grab `[b]` | `grabAllBanners()` in `network_scanner.cpp`; available in `ps`/`ts` after port scan |
| `jiggle` / `jg` | HID mouse jiggle to prevent screen lock |
| WPS display | `sw` shows cyan "WPS" label from scan record — **detection only** |
| Karma detection | `wguard` tracks probe responses in session CSV — **detection only** |
| DuckyScript runner | `bad_usb.cpp` — SD-based BadUSB script execution |

---

## Can Be Added

### Network / Discovery
- `mdns` — mDNS browser (discover `.local` services on the network)
- `upnp` — UPnP/SSDP scanner (find routers, cameras, smart devices)
- `mqtt` — MQTT subscribe/publish tool (IoT message sniffing)
- `coap` — CoAP resource discovery (`/.well-known/core`)
- `snmp` — SNMP walk (`public` community string)
- `httpget` — raw HTTP GET with response header dump
- `httpbrute` — HTTP basic-auth brute force
- `ftpbrute` — FTP login brute force
- `telnet` — Telnet terminal
- `wakeonlan` — Wake-on-LAN magic packet sender
- `traceroute` — Hop-by-hop path tracer (ICMP/UDP TTL)
- `dns` — DNS query tool + reverse lookup
- `arpwatch` — ARP change monitor (detects ARP poisoning / MITM)
- `roguedhcp` — Rogue DHCP server (client IP hijack)

### WiFi Attacks
- `wps` — WPS Pixie Dust / PIN brute force attack (hardware WPS attack, not just detection)
- `karma` — Karma attack rogue AP (respond to all probe requests, not just detect them)
- `wifijam` — Continuous deauth flood across all channels
- `probeflood` — Flood probe requests with random MACs
- `dhcpstarve` — DHCP pool exhaustion (flood DISCOVER with random MACs)
- `evildhcp` — Combined rogue DHCP + DNS spoof

### BLE / Bluetooth
- `macwatch` — MAC proximity alert with audible alarm (reuses bmon ring buffer + OUI table)
- `blefuzz` — BLE GATT attribute fuzzer (random write values, crash/disconnect detection)
- `nusc` — NUS Chat over BLE serial (T-DECK ↔ T-DECK comms)

### LoRa
- `lscan` / `lsniff` — LoRa packet sniffer + logger (RadioLib already in `platformio.ini`)
- `lsend` — LoRa packet transmitter
- `lchat` — LoRa text chat
- `lorawan` — LoRaWAN join sniff (OTAA join-request logger)
- `lmap` — LoRa signal strength mapper (with GPS)

### Hardware / Physical
- `i2cscan` — I2C bus scanner (detect all devices on the bus)
- `uartscan` — UART baud rate auto-detect
- `uarttty` — Serial terminal (configurable baud)
- `gpiowatch` — GPIO state monitor with change log
- `usbethernet` — RNDIS/ECM USB gadget (T-DECK as USB Ethernet adapter)

### GPS / Mapping
- `wardriving` — Drive + scan WiFi → GPS-tagged CSV (Wigle / Kismet compatible)
- `gpstrack` — Raw GPS track log → NMEA or GPX file
- `geofence` — Alert when GPS leaves/enters defined radius

### Utility / CLI Tools
- `hash` — SHA-256/MD5 hash of file or string
- `b64` — Base64 encode/decode
- `jwt` — JWT decode (show header/payload without verify)
- `pwgen` — Password generator (random, pattern, pronounceable)
- `hexview` — Hex dump viewer for SD files
- `notes` — Simple SD text notepad
- `calc` — Expression calculator
- `macro` — Batch T-REX command runner from SD script file (separate from BadUSB DuckyScript)

---

## Priority Picks

| # | Feature | Why |
|---|---------|-----|
| 1 | `macwatch` | Trivial to add — reuses bmon ring buffer and OUI table |
| 2 | `lscan` / `lsniff` | RadioLib already in deps; hardware is idle |
| 3 | `wardriving` | High value for a GPS-equipped pentest deck |
| 4 | `i2cscan` | ~10 lines; great hardware hacking diagnostic |
| 5 | `wps` attack | High demand; needs raw 802.11 or external WPS library |
