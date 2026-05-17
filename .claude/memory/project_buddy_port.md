---
name: project-buddy-port
description: Full spec to port claude-desktop-buddy BLE Claude Desktop remote to T-DECK as buddy/bd command — self-contained for any PC
metadata:
  type: project
---

## What & Why

`claude-desktop-buddy` (https://github.com/anthropics/claude-desktop-buddy) connects a physical ESP32 device to **Claude Desktop via BLE Nordic UART Service**. Claude Desktop pushes session status + pending permission prompts over BLE; device displays them and lets user approve/deny from hardware.

Goal: add this as a `buddy/bd [name]` command in T-DECK-CLI so the T-DECK keyboard can remotely approve Claude Code permission prompts.

**Status:** Working — implemented 2026-05-17. Commit `9b48de8` on `feature/pentest-enhancements`.

**Security note:** `setSecurityAuth`/`setSecurityIOCap`/`setSecurityPasskey` called before `pSvc->start()` crashes NimBLE 1.4.x on ESP32-S3. Currently no passkey — plain NUS GATT server. Claude Desktop connects fine without it. If bonding is needed later, set security AFTER `pSvc->start()`.

**Why:** Claude Desktop's BLE Maker API is opt-in. Requires developer mode on desktop: `Help → Troubleshooting → Enable Developer Mode`.

---

## Source repo — what to keep vs skip

Clone: `https://github.com/anthropics/claude-desktop-buddy`

### Keep (port/adapt)
| File | Action |
|---|---|
| `src/ble_bridge.cpp` | Port to NimBLE (see full mapping below) |
| `src/ble_bridge.h` | Keep API, change includes |
| `src/data.h` | Keep `TamaState` struct + `dataPoll()` + `_applyJson()` — strip M5.Rtc calls |

### Skip entirely
- All `src/buddies/*.cpp` — 18 ASCII pet animation files, irrelevant
- `src/buddy.cpp` / `src/character.cpp` — pet renderer + GIF decoder
- `src/stats.h` — NVS pet stats (level/exp), not needed
- `src/xfer.h` — GIF folder push protocol, not needed
- `src/main.cpp` — entire file, replace with T-DECK command loop

---

## TamaState struct (copy verbatim into buddy.h)

```cpp
struct TamaState {
  uint8_t  sessionsTotal;
  uint8_t  sessionsRunning;
  uint8_t  sessionsWaiting;
  bool     recentlyCompleted;
  uint32_t tokensToday;
  uint32_t lastUpdated;        // millis() when last JSON arrived
  char     msg[24];            // one-line summary from desktop
  bool     connected;          // true if JSON arrived in last 30s
  char     lines[8][92];       // transcript lines
  uint8_t  nLines;
  uint16_t lineGen;            // bumps when lines array changes
  char     promptId[40];       // pending permission ID; empty = no prompt
  char     promptTool[20];     // tool name e.g. "Bash"
  char     promptHint[44];     // hint e.g. "rm -rf /tmp"
};
```

---

## BLE Wire Protocol (Nordic UART Service)

**Service UUID:** `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
**RX char** (desktop→device, WRITE): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
**TX char** (device→desktop, NOTIFY): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

All messages are **line-delimited UTF-8 JSON** (`\n` terminated).

### Desktop → Device (heartbeat, ~every 10s)
```json
{
  "total": 3,
  "running": 1,
  "waiting": 1,
  "msg": "approve: Bash",
  "entries": ["line1", "line2"],
  "tokens": 184502,
  "tokens_today": 31200,
  "completed": true,
  "prompt": {
    "id": "req_abc123",
    "tool": "Bash",
    "hint": "rm -rf /tmp/cache"
  }
}
```
`prompt` is only present when `waiting > 0`. `completed` is optional.

### Desktop → Device (one-shot on connect, time sync)
```json
{ "time": [1775731234, -25200] }
```
`[epoch_sec, tz_offset_sec]` — skip on T-DECK (no RTC hardware), just ignore this message.

### Device → Desktop (approve/deny)
```json
{"cmd":"permission","id":"req_abc123","decision":"once"}
{"cmd":"permission","id":"req_abc123","decision":"deny"}
```
`"once"` = approve once, `"always"` = approve always, `"deny"` = deny.

### BLE Security
- LE Secure Connections + MITM + Bonding
- Device is **DisplayOnly** — shows a 6-digit passkey on screen during first pairing
- User types passkey on Claude Desktop side
- Bond stored in NVS — subsequent connections auto-pair
- MTU: device requests 517, macOS negotiates ~185 → chunk writes to `mtu - 3`

---

## BLE Stack Port: Classic → NimBLE

T-DECK-CLI uses NimBLE throughout (scanblue, trackme). Classic BLE (Bluedroid) and NimBLE cannot coexist in one firmware — must port `ble_bridge.cpp` to NimBLE.

### Direct API mapping

```
BLEDevice::init(name)                    → NimBLEDevice::init(name)
BLEDevice::setMTU(517)                   → NimBLEDevice::setMTU(517)
BLEDevice::setEncryptionLevel(...)       → NimBLEDevice::setSecurityAuth(true,true,true)
BLEDevice::setSecurityCallbacks(cb)      → NimBLEDevice::setSecurityCallbacks(cb)
BLEDevice::createServer()               → NimBLEDevice::createServer()
server->setCallbacks(cb)                → server->setCallbacks(cb)  [same pattern]
server->createService(uuid)             → server->createService(uuid)
svc->createCharacteristic(uuid, props)  → svc->createCharacteristic(uuid, props)
BLECharacteristic::PROPERTY_NOTIFY      → NIMBLE_PROPERTY::NOTIFY
BLECharacteristic::PROPERTY_WRITE       → NIMBLE_PROPERTY::WRITE
BLECharacteristic::PROPERTY_WRITE_NR    → NIMBLE_PROPERTY::WRITE_NR
char->setAccessPermissions(...)         → char->setAccessPermissions(...)  [same]
new BLE2902()  [CCCD descriptor]        → NOT NEEDED — NimBLE adds it automatically
BLEAdvertising* adv = BLEDevice::getAdvertising() → NimBLEAdvertising* adv = NimBLEDevice::getAdvertising()
adv->addServiceUUID(uuid)               → adv->addServiceUUID(uuid)  [same]
BLEDevice::startAdvertising()           → NimBLEDevice::startAdvertising()
```

### Security callbacks — NimBLE signature differs

```cpp
// Classic BLE:
class SecCallbacks : public BLESecurityCallbacks {
  void onPassKeyNotify(uint32_t pk) override { passkey = pk; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override { ... }
};

// NimBLE equivalent:
class SecCallbacks : public NimBLESecurityCallbacks {
  void onPassKeyNotify(NimBLEConnInfo& info, uint32_t pk) override { passkey = pk; }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    secure = info.isEncrypted();
    if (!secure) NimBLEDevice::getServer()->disconnect(info.getConnHandle());
  }
};
```

### Security init — NimBLE style

```cpp
NimBLEDevice::setSecurityAuth(true, true, true);        // bonding, MITM, SC
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY); // we show passkey
NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
```

### onDisconnect — restart advertising

```cpp
void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
  connected = false; secure = false; passkey = 0;
  NimBLEDevice::startAdvertising();
}
```

### Write callback — NimBLE

```cpp
class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    std::string v = c->getValue();
    if (!v.empty()) rxPush((const uint8_t*)v.data(), v.size());
  }
};
```

### bleWrite chunking — NimBLE

```cpp
size_t bleWrite(const uint8_t* data, size_t len) {
  if (!connected || !txChar) return 0;
  NimBLEServer* srv = NimBLEDevice::getServer();
  uint16_t mtu = srv ? srv->getPeerMTU(srv->getConnId()) : 23;
  size_t chunk = (mtu > 3 ? mtu - 3 : 20);
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    size_t n = min(chunk, len - sent);
    txChar->setValue(data + sent, n);
    txChar->notify();
    sent += n;
    delay(4);
  }
  return sent;
}
```

---

## Hardware Differences: T-DECK vs M5StickC Plus

| Feature | M5StickC Plus | T-DECK — what to do |
|---|---|---|
| Display | 135×240 | 320×240 — more space, use `displayManager` |
| Buttons | BtnA / BtnB physical | PS/2 keyboard: `y`=approve, `n`=deny, `q`=quit |
| Power IC | AXP192 | None — battery via GPIO4 ADC (already in T-DECK-CLI) |
| LED GPIO10 | Active-low indicator | **POWER PIN — must stay HIGH — do NOT use as LED** |
| IMU | MPU6886 | None — skip all shake/face-down logic |
| RTC | M5.Rtc | None — ignore `{"time":[...]}` messages, no harm |
| Audio | M5.Beep PWM | I2S already works — use for pairing tone if wanted |
| Filesystem | LittleFS | SD already available — bonds stored in NVS (handled by BLE stack) |

---

## dataPoll() adaptation for T-DECK

Strip all M5.Rtc calls from `_applyJson()`. Replace with no-op:

```cpp
// Original (M5StickC):
JsonArray t = doc["time"];
if (!t.isNull()) { M5.Rtc.SetTime(...); M5.Rtc.SetDate(...); return; }

// T-DECK: just skip it
JsonArray t = doc["time"];
if (!t.isNull()) { _lastLiveMs = millis(); return; }  // accept but ignore
```

Strip `_usbLine.feed(Serial, out)` — T-DECK-CLI never uses Serial.
Keep BLE ring-buffer drain loop exactly as-is.

---

## New Files to Create in T-DECK-CLI

```
t-deck-cli/src/
├── buddy.h          — TamaState struct, bleInit/bleConnected/blePasskey/bleWrite/bleRead/bleAvailable/bleClearBonds declarations
├── buddy.cpp        — NimBLE GATT peripheral + dataPoll() + command loop
```

Register in `setupCommands()` (`command_manager.cpp`):
```cpp
registerCommand("buddy", "bd", buddyCommand, "Claude Desktop BLE remote", true, "System");
```

Usage: `buddy [name]` — optional custom BLE name suffix (default: `Claude-XXYY` using last 2 MAC bytes).

---

## Target UI on T-DECK (320×240, displayManager)

**No connection:**
```
[Claude Buddy]
Advertising as: Claude-A1B2
Waiting for Claude Desktop...
Developer mode required on desktop
[q] quit
```

**Pairing (passkey on screen):**
```
[Claude Buddy — PAIRING]
Enter this code in Claude Desktop:

        4 8 3 1 9 2

[q] cancel
```

**Connected, idle:**
```
[Claude Buddy — connected | Claude-A1B2]
Sessions: 2 total  0 waiting
Tokens today: 31,200
─────────────────────────────
 No pending approvals
─────────────────────────────
[q] quit
```

**Pending approval:**
```
[Claude Buddy — APPROVAL NEEDED]
Sessions: 2 total  1 waiting
─────────────────────────────
 Tool:  Bash
 Hint:  rm -rf /tmp/cache
─────────────────────────────
[y] approve once  [n] deny  [q] quit
```

---

## Implementation Order

1. `buddy.h` — TamaState struct + ble_bridge.h API (bleInit, bleConnected, blePasskey, bleWrite, bleRead, bleAvailable, bleClearBonds)
2. `buddy.cpp` — NimBLE ble_bridge (port from original, NimBLE mapping above)
3. `buddy.cpp` — `_applyJson()` + `dataPoll()` (strip M5.Rtc, strip Serial, keep BLE drain)
4. `buddy.cpp` — `buddyCommand()` main loop: advertise → wait connect → poll + display → handle y/n/q
5. Wire into `setupCommands()` and `#include "buddy.h"` in main .cpp

---

## Key Invariants

- Never use GPIO10 for LED — it is the T-DECK power enable pin
- Call `NimBLEDevice::deinit()` on `q` so BLE scanner (scanblue/trackme) can reinit normally afterward — they both call `NimBLEDevice::init()`
- Do not write to SD during buddy command — no SD activity needed
- All display via `displayManager`, never `tft` directly
- Poll `inputHandler.getKeyboardInput()` for `q` in the main wait loop (standard T-DECK pattern)
- ArduinoJson already in lib_deps — no new dependencies needed for JSON parsing
