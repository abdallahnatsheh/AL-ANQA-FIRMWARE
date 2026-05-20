# NotificationManager — Future Feature

## Goal
Standalone, reusable notification module — zero dependency on T-Rex internals.
Drop two files into any T-Deck project and it works.

## Design
- Singleton: `NotificationManager::getInstance()`
- Uses `extern LGFX tft` directly (no displayManager dependency)
- Uses `SD.h` directly (no sdCardManager dependency)
- Own self-contained I2S tone generation (BCK=7, WS=5, DOUT=6)
- Screen wake via registered callback — app wires in its own wake logic
- Config persisted to `/notif.conf` (key=value)

## Public API
```cpp
void begin();
void setWakeCallback(std::function<void()> cb);
void notify(NotifLevel level, const char* message = nullptr);
void setVolume(uint8_t vol);   // 0–100
void volumeUp(uint8_t step = 10);
void volumeDown(uint8_t step = 10);
void enable(NotifLevel level, bool on);
void enableAll(bool on);
void loadConfig();
void saveConfig();
void printStatus();
```

## Notification levels + default tones (no SD needed)
| Level | Sound | Use case |
|---|---|---|
| NOTIF_ALERT | 3× fast high beeps 1000Hz | deauth, evil twin, critical threat |
| NOTIF_WARNING | 2× medium beeps 700Hz | suspicious activity |
| NOTIF_SUCCESS | ascending 500→800Hz | handshake captured, crack found |
| NOTIF_INFO | 1× soft beep 500Hz | scan complete, info event |
| NOTIF_PING | 1× short 600Hz | generic |

## SD config /notif.conf
```
volume=70
alert=on
warning=on
success=on
info=on
ping=on
```

## Commands to register in command_manager
- `vol` / `vol` — `[0-100|up|down|off]` — get/set volume
- `notif` / `nf` — `[on|off|<level> on|off]` — notification settings

## Integration points (wire in when building)
1. **buddy.cpp** — notify(NOTIF_PING) when Claude Desktop permission popup arrives
2. **trackme.cpp** — replace current alert display with notify():
   - Gate1 match → NOTIF_INFO
   - Gate2 score warning → NOTIF_WARNING
   - Gate3 confirmed tracker → NOTIF_ALERT
3. **wguard** (future) — notify(NOTIF_ALERT) on deauth flood / evil twin / handshake harvest
4. **hiddenssid.cpp** — already has I2S beep on found; replace with notify(NOTIF_SUCCESS)
5. **wpasniff** — notify(NOTIF_SUCCESS) on handshake captured / crack found

## PowerSaveManager addition needed
- Add `forceWake()` — clears `_manualOff`, calls `wakeUp()`. Used by notification wake callback so attack alerts always break through manual screen-off.

## main.ino wiring
```cpp
// setup():
notifManager.begin();
notifManager.setWakeCallback([]() {
    PowerSaveManager::getInstance().forceWake();
});
```

## I2S note
- Must use i2s_driver_install() — tone() fails on ESP32-S3
- Check if I2S driver already installed before re-installing
- Reuse pattern from spktest / hiddenssid I2S beep code
