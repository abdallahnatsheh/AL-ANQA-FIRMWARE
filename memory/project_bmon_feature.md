# bmon — BLE Advertisement Monitor (Future Feature)

## Command
`bmon` / `bm` — passive BLE advertisement sniffer

## What it does
- Passive scan on all 3 advertising channels (37, 38, 39)
- Captures and parses every BLE advertisement packet from every nearby device
- Decodes all AD structure types: 0xFF manufacturer, 0x16 service data, 0x08/09 name, 0x0A TX power, 0x01 flags
- **Cleartext detector** — flags payloads containing printable ASCII strings, key=value patterns, IP-shaped bytes
- **Service UUID decoder** — maps known UUIDs to device type (thermometer, heart rate, lock, etc.)
- **iBeacon parser** — UUID/major/minor
- **Eddystone parser** — URL (0xAA/0xBB), UID, TLM
- Saves to SD in PCAP format (linktype 251 LINKTYPE_BLUETOOTH_LE_LL) → opens in Wireshark
- SD path: `/logs/bmon/<timestamp>.pcap`

## Limitations (hardware)
- Advertisements only — cannot sniff BLE connected sessions
- Connected session sniffing requires Ubertooth One or nRF Sniffer (frequency hopping, 37 data channels at ~1600 hops/sec)
- Encrypted advertisement data cannot be decrypted without key

## UI
```
[CYAN::BMON]  passive · ch37/38/39

AA:BB:CC:DD:EE:FF  -61dBm  iBeacon        UUID:FDA50693~ major:1 minor:1
11:22:33:44:55:66  -74dBm  Manufacturer   [FF] 4c00 0215 ...
DE:AD:BE:EF:00:01  -55dBm  ⚠ CLEARTEXT   "temp=23.4;hum=61;pass=admin"
AB:CD:EF:01:02:03  -80dBm  Service Data   [16] 1809 → Health Thermometer 36.6°C

[p]pause  [s]save pcap  [f]filter  [q]quit
Captured: 247 packets  Flagged: 3
```

## Keys
- `p` pause/resume
- `s` save current capture to SD as PCAP
- `f` filter — by RSSI threshold or flagged-only
- `q` quit

## What it realistically catches
- IoT sensors broadcasting sensor data unencrypted in ads
- Devices leaking model/version in name field
- iBeacon UUIDs for venue fingerprinting
- Manufacturer payloads from proprietary protocols (reverse engineering starting point)
- Devices advertising service data with actual values (health monitors, etc.)
