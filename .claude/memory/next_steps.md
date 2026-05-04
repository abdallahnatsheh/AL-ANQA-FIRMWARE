---
name: Next Steps — Pending Implementation
description: Concrete tasks queued for the next sessions, in priority order
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
## Immediate (next session)

### 1. ARP Scan (replace pingScan)
Replace `pingScan` with ARP-based host discovery using lwIP primitives.

**Why:** Ping requires ICMP replies (many devices ignore them). ARP works on all LAN devices — every device must respond to ARP or it can't use the network. Also gives MAC addresses for free.

**Implementation plan:**
- Get `struct netif*` via `netif_find("st1")` or walk `netif_list`
- Build target `ip4_addr_t` for each host x.x.x.1–254
- Call `etharp_request(netif, &target)` for all 254 hosts (fast, just sends UDP-like frame)
- Wait ~150ms for replies
- Walk `etharp_find_addr(netif, &target, &eth_mac, &ip_found)` to read the ARP cache
- Report: `[+] x.x.x.x  AA:BB:CC:DD:EE:FF` for each hit
- Headers: `#include "lwip/etharp.h"`, `#include "lwip/netif.h"`

**How to apply:** Replace `pingScanTaskFn` in `wifi_functions.cpp`. Keep same FreeRTOS task pattern and HOST_FOUND queue messages — just change what runs inside the loop.

---

### 2. Top-Ports Mode for Port Scan
Add `ps <ip> top` that scans ~100 curated common ports instead of user-supplied range.

**Implementation plan:**
- Add `static const uint16_t TOP_PORTS[]` in flash (`PROGMEM` or just `const` array): 21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445, 993, 995, 1433, 1723, 3306, 3389, 5900, 5985, 8080, 8443, 8888, 9090, …
- In `portScanCommand()` arg parser: if second token is "top", use TOP_PORTS array instead of range loop
- `portScanTaskFn` struct: add `bool useTopPorts` + `const uint16_t* portList` + `int portCount`

---

### 3. Banner Grabbing
New command `banner/bn <ip> <port>` — connect, read first 256 bytes, identify service.

**Implementation plan:**
- `WiFiClient client; client.connect(ip, port, 500);`
- Read up to 256 bytes with 1s timeout
- Match known prefixes: `SSH-` → SSH, `HTTP/` or `GET` → HTTP, `220 ` → FTP/SMTP, `+OK` → POP3, `* OK` → IMAP
- Print raw banner + identified service label
- New file: `banner_functions.h/cpp`
- Register: `banner/bn`

---

### 4. Reduce Port Scan Timeout
Change 500ms → 100ms in `portScanTaskFn` for LAN hosts.

**Why:** LAN devices respond in <10ms. 500ms was chosen conservatively; 100ms is safe for same-subnet and reduces scan time 5x.

**File:** `wifi_functions.cpp`, `WiFiClient::connect(ip, port, 500)` → `connect(ip, port, 100)`

---

## Medium Term

### 5. Evil Twin AP (`eviltwin/et`)
Rogue AP + captive portal to capture credentials.

**Plan:**
- `WiFi.softAP(ssid, nullptr)` (open, matching target SSID)
- `AsyncWebServer` or basic `WiFiServer` serving redirect page
- Store POSTed creds to SD `/captures/eviltwin.txt`
- Show connected client count live
- q=stop

---

### 6. BLE GATT Enumeration (`bleinfo/bi <index>`)
Connect to scanned BLE device, list all services and characteristics.

**Plan:**
- `NimBLEClient` connect to MAC from last `blescan` result
- Walk `getServices()` → `getCharacteristics()`
- Print UUID, properties (READ/WRITE/NOTIFY), value if readable
- Disconnect cleanly on q

---

### 7. BLE Device Tracker (`btrack/bt`)
Continuous BLE scan, track new/disappearing MACs over time.

**Plan:**
- Continuous scan with 5s windows
- Map: MAC → last_seen, first_seen, RSSI history
- Mark newly appeared (green) and disappeared (red) devices
- Optional SD log to `/logs/bt.txt`

---

### 8. LoRa Scanner (`lorascan/lscan`)
Passive LoRa packet capture using RadioLib (already in lib/).

**Plan:**
- `SX1276 radio = new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, SPI)`
- Set to receive mode, common frequencies (868MHz EU / 915MHz US)
- Display: frequency, RSSI, SNR, raw hex payload, packet count
- SD log to `/logs/lora.txt`

---

### 9. BadUSB / HID (`badusb/bu <script>`)
TinyUSB HID keyboard emulation playing DuckyScript from SD.

**Plan:**
- `Adafruit_TinyUSB` or ESP-IDF USB HID
- Parse DuckyScript from SD `/scripts/*.duck`
- `STRING`, `DELAY`, `ENTER`, `GUI`, `CTRL`, modifier keys
- List scripts: `badusb ls`
- Run: `badusb <filename>`
