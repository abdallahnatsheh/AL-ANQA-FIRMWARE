---
name: Known Bugs in T-DECK-CLI
description: Confirmed bugs found during code review that must be fixed before adding new features
type: project
originSessionId: 2336d293-7b97-4ecb-9509-7252b79993a1
---
All bugs discovered during 2026-05-02 code review. Fix these before merging new features.

**Bug 1 — pingScan only scans 1 host** (`wifi_functions.cpp:315`)
`for (int i = 1; i <= gatewayLastPart; ++i)` — if gateway is 192.168.1.1, only scans .1.
Fix: change to `i <= 254`.

**Bug 2 — WiFi passwords reject special characters** (`wifi_functions.cpp:213`)
`isAlphaNumeric(input)` blocks `!@#$_-` etc. Most real WPA2 passwords need these.
Fix: change to `isPrintable(input)`.

**Bug 3 — Passwords printed to Serial in plaintext** (`wifi_functions.cpp:39,45`)
`Serial.println("Entered password: " + password)` — security issue.
Fix: remove all password Serial prints.

**Bug 4 — Duplicate sscanf condition** (`wifi_functions.cpp:23`)
`sscanf(...) != 1 && sscanf(...) != 1` — same call twice. Logic should be `||` not `&&`.

**Bug 5 — BLE callback memory leak** (`bluetooth_functions.cpp:28`)
`new MyAdvertisedDeviceCallbacks()` never deleted — heap leak every time scanblue runs.
Fix: store pointer, delete on scan exit.

**Bug 6 — Port scan re-runs full scan on every page render** (`wifi_functions.cpp:418`)
Outer `while(true)` in `performPortScan` re-scans all ports each page flip.
Fix: scan once into `std::vector<int> openPorts`, then paginate the vector.

**Bug 7 — Command buffer too small** (`command_manager.h`)
64-byte buffer truncates `portscan 192.168.100.200 1 65535` (38 chars fine, but edge cases).
Fix: increase to 128 bytes.

**Bug 8 — Keyboard INT pin (GPIO 46) never asserts LOW** (`input_handling.cpp`)
GPIO 46 is an ESP32-S3 strapping pin with a hardware pull-down. `INPUT_PULLUP` + `digitalRead` check always reads HIGH — keyboard MCU at 0x55 is alive on I2C but INT never fires.
Fix: bypass INT entirely, poll I2C directly every 10ms via `Wire.requestFrom`. MCU returns 0x00 when no key, key code when pressed. **FIXED 2026-05-04.**

**Why:** These were found in the first full code review. Fix all before new feature PRs.
**How to apply:** When touching any of these files, fix the relevant bug in the same commit.
