# T-Rex — CSI Human Detector & Camera Scanner Feature Plan
# Reference: https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector

---

## Overview

Two new commands derived from WiFi CSI (Channel State Information) technology:

1. `csidetect` (`cd`) — Human presence detector using WiFi signal disturbance
2. `camdetect` (`cm`) — Hidden camera / spy device detector using WiFi OUI sniffing

Both use the ESP32-S3's built-in WiFi hardware — no extra sensors needed.

---

## How WiFi CSI Works

WiFi CSI measures how radio signals change as they bounce off objects in
the environment. The ESP32 can read raw channel state information from
received WiFi frames — amplitude and phase of subcarriers.

When a human body moves, it disturbs the WiFi signal in a measurable way.
By analyzing variance in amplitude and phase over a sliding window, motion
and presence can be detected without any camera or radar hardware.

This uses standard ESP-IDF APIs:
- `esp_wifi_set_csi_config()` — configure CSI collection
- `esp_wifi_set_csi_rx_cb()` — register CSI callback (IRAM_ATTR)
- `esp_wifi_set_csi(true)` — enable CSI collection
- `esp_wifi_set_promiscuous(true)` — needed to receive CSI frames

**Requires:** WiFi connected to a network (any network). CSI is extracted
from received WiFi frames — the more WiFi traffic nearby, the more frames,
the better the detection. Works on any 2.4GHz channel.

---

## Feature 1 — `csidetect`

### Command
```
CMD> csidetect
```
Alias: `cd`

### Use case
Place T-Rex on a table, connected to WiFi, and leave it running.
It detects if a human enters or moves in the room using only the WiFi radio.
Useful during physical pentests — leave T-Rex running as a motion sensor
while you're away. Speaker beeps on presence detection.

### Algorithm (from reference implementation)

**CSI Callback (IRAM_ATTR, runs on every received WiFi frame):**

```
For each CSI frame:
  1. Extract I/Q pairs from raw buffer
  2. Calculate amplitude = sqrt(r² + im²) for each subcarrier pair
  3. Calculate mean amplitude across all subcarrier pairs
  4. Calculate mean sin(phase) = im/amp for phase tracking
  5. Store in sliding window buffer (50 frames)
  6. Calculate amplitude variance over window
  7. Calculate phase variance over window
  8. Normalize both variances using asymmetric EMA:
     - Floor EMA: fast decay (0.1) when below floor, slow (0.002) above
     - Max EMA: slow decay (0.005) to track running maximum
  9. Blend: motionScore = 0.6 × ampMotion + 0.4 × phaMotion
```

**Hold/coast logic (in main loop at ~15Hz):**
```
If motionScore > threshold:
  holdCount = 150  (10 seconds at 15Hz)
  heldMotion = motionScore
  present = true
Else if holdCount > 0:
  holdCount--
  motion = heldMotion × (0.10 + 0.90 × holdCount/150)  // fade gracefully
  present = true
Else:
  present = false
  motion = 0.0
```

This prevents the presence indicator from snapping off immediately
when someone stands still — coasts for 10 seconds then fades.

**Default threshold:** 0.35 (adjustable with `[` and `]` keys)

### Data structures
```cpp
#define CSI_WINDOW      50    // sliding window size
#define CSI_HOLD_COUNT  150   // 10s at 15Hz

struct CsiState {
    float  ampBuf[CSI_WINDOW];
    float  phaBuf[CSI_WINDOW];
    int    idx;
    int    filled;
    float  varMax;
    float  varMin;
    float  phaVarMax;
    float  phaVarMin;
    volatile float  motion;   // blended 0.0-1.0
    volatile int8_t rssi;
    volatile uint32_t frameCount;
};

// Fixed-size motion history for graph (same approach as trackme score history)
#define CSI_HISTORY  100
float motionHistory[CSI_HISTORY];
int   motionHistIdx;
```

### UI Layout
```
┌─────────────────────────────────────┐
│ T-REX // CSI DETECT        [q] exit │  ← RED header
├─────────────────────────────────────┤
│ WiFi: connected  Chan:6  Fr:1247     │  ← status: connection, channel, frame count
├─────────────────────────────────────┤
│ ┌───────────────────────────────┐   │
│ │     ▓▓ PRESENCE DETECTED ▓▓  │   │  ← big status box (RED bg = presence)
│ └───────────────────────────────┘   │  ←                 (BLUE bg = clear)
│                                     │
│ Motion: ████████░░░░  78%           │  ← motion bar
│ Thresh: ████░░░░░░░░  35%           │  ← threshold marker
│                                     │
│ ▁▂▃▅▇█▇▅▃▂▁▁▁▁▁▁▂▃▄▅▄▃▂▁▁▁▁▁▁▁▁   │  ← scrolling motion graph (100 samples)
│                                     │
│ RSSI: -62dBm  Frames: 1247          │
├─────────────────────────────────────┤
│ [  [  ] ]thresh  [c]cal  [q]quit    │
└─────────────────────────────────────┘
```

### Key bindings
- `[` → threshold -5%
- `]` → threshold +5%
- `c` → calibrate (reset variance min/max, re-baseline)
- `q` → quit

### Speaker alert
- PRESENCE detected → single short beep (500Hz, 150ms)
- PRESENCE sustained 30s → repeat beep every 30s
- CLEAR after presence → single low beep (300Hz, 100ms)

### Requirements
- Must be connected to WiFi before running
- If not connected: show "Connect to WiFi first (use connectwifi)"
- Uses promiscuous mode + CSI — cannot run simultaneously with:
  - deauth, wifimon, eviltwin (all use promiscuous)
  - trackme WiFi side (uses promiscuous)
- On exit: disable CSI, disable promiscuous, restore WiFi state

### SD Logging
If SD present, log events to `/logs/csidetect.log`:
```
[timestamp] PRESENCE | motion:0.78 | rssi:-62 | duration:45s
[timestamp] CLEAR    | motion:0.02 | rssi:-62
```

---

## Feature 2 — `camdetect`

### Command
```
CMD> camdetect
```
Alias: `cm`

### Use case
Walk into a hotel room, Airbnb, meeting room, or any unknown space.
Run `camdetect` and it sniffs WiFi probe requests and beacon frames,
matching source MACs against a database of known camera/IoT device OUIs.
Detects hidden cameras, smart doorbells, baby monitors, and ESP32-based
DIY surveillance devices.

No WiFi connection needed — works in promiscuous/sniffer mode only.
Channel hop across 1-13 to maximize coverage.

### OUI Database (from reference implementation + expanded)

The source MAC first 3 bytes (OUI) identify the manufacturer.
This is the complete list to include:

```cpp
struct OuiEntry { uint8_t b[3]; const char* vendor; const char* type; };

static const OuiEntry kCamOuis[] = {
    // ESP32-based (DIY cameras, cheap IoT devices)
    {{0x24,0x0A,0xC4}, "Espressif", "ESP32"},
    {{0x30,0xAE,0xA4}, "Espressif", "ESP32"},
    {{0x24,0x6F,0x28}, "Espressif", "ESP32"},
    {{0xDC,0x54,0x75}, "Espressif", "ESP32"},
    {{0xE8,0x9F,0x6D}, "Espressif", "ESP32"},
    {{0x8C,0xAA,0xB5}, "Espressif", "ESP32-S3"},
    {{0x34,0x85,0x18}, "Espressif", "ESP32-S3"},

    // Consumer cameras
    {{0x2C,0xAA,0x8E}, "Wyze",      "Camera"},
    {{0xD0,0x3F,0x27}, "Wyze",      "Camera"},
    {{0x7C,0x78,0xB2}, "Wyze",      "Camera"},
    {{0xFC,0x65,0xDE}, "Ring",      "Camera"},
    {{0x68,0x37,0xE9}, "Ring",      "Camera"},
    {{0x34,0xD2,0x70}, "Amazon",    "Echo/Cam"},
    {{0xF0,0x27,0x2D}, "Hikvision", "Camera"},
    {{0xC0,0x56,0xE3}, "Hikvision", "Camera"},
    {{0x44,0x19,0xB6}, "Hikvision", "Camera"},
    {{0x28,0x57,0xBE}, "Reolink",   "Camera"},
    {{0x00,0xE0,0x4C}, "Realtek",   "IoT"},
    {{0xBC,0xDD,0xC2}, "Arlo",      "Camera"},
    {{0x4C,0x69,0x05}, "Blink",     "Camera"},

    // Additional cameras (expand from OUI databases)
    {{0x00,0x18,0xAE}, "D-Link",    "Camera"},
    {{0x1C,0xBD,0xB9}, "D-Link",    "Camera"},
    {{0xB8,0xA4,0x4F}, "Dahua",     "Camera"},
    {{0x4C,0x11,0xAE}, "Dahua",     "Camera"},
    {{0xEC,0x71,0xDB}, "Amcrest",   "Camera"},
    {{0x9C,0xA9,0xE4}, "TP-Link",   "Camera"},
    {{0x54,0xAF,0x97}, "Nest",      "Camera"},
    {{0x18,0xB4,0x30}, "Nest",      "Camera"},
};
```

### Detection method
Uses WiFi promiscuous mode (no connection needed).
Sniffs all management and data frames.
Extracts source MAC (bytes 10-15 of payload for most frame types).
Matches OUI (first 3 bytes) against kCamOuis table.
Tracks each unique device: RSSI history, last seen, EMA-smoothed distance.

### RSSI → distance estimation
```
// Map RSSI to approximate radius for display
// -45 dBm = very close (inner ring), -78 dBm = far (outer ring)
float t = (rssi + 45.0f) / (-33.0f);  // 0.0 = close, 1.0 = far
t = clamp(t, 0.0f, 1.0f);
float radius = MAX_RADIUS * (0.30f + t * 0.60f);
```

### Data structures
```cpp
#define CAMDETECT_MAX    16    // max tracked camera devices
#define CAM_LIFE_MS   60000   // drop device after 60s silence

struct CamDevice {
    uint8_t  mac[6];
    char     vendor[12];
    char     type[10];
    int8_t   rssi;
    float    rssiSmoothed;    // EMA smoothed
    uint32_t firstSeen;
    uint32_t lastSeen;
    uint8_t  channel;
    uint16_t frameCount;
    bool     active;
};

CamDevice devices[CAMDETECT_MAX];
int        deviceCount;
```

### Channel hopping
```
Sweep channels 1, 6, 11 (most common) first, 200ms each.
Then sweep remaining channels 2-5, 7-10, 12-13, 100ms each.
When a device is detected on a channel, dwell 2s before continuing hop.
```

### UI Layout
```
┌─────────────────────────────────────┐
│ T-REX // CAM DETECT        [q] exit │  ← RED header
├─────────────────────────────────────┤
│ Ch:6  Frames:3421  Devices:2  t:02:11│  ← status
├─────────────────────────────────────┤
│ #  VENDOR     TYPE    RSSI   SEEN   │  ← table header
│ 1  Ring       Camera  -58    02:03  │  ← RED (close, <-65dBm)
│ 2  Espressif  ESP32   -74    00:45  │  ← YELLOW (medium)
│                                     │
│                                     │
│                                     │
├─────────────────────────────────────┤
│ [!] Ring camera -58dBm — VERY CLOSE │  ← alert bar
└─────────────────────────────────────┘
```

### Alert levels by RSSI
| RSSI | Level | Color | Meaning |
|------|-------|-------|---------|
| > -65 dBm | ALERT | RED | Very close, same room likely |
| -65 to -75 dBm | WARNING | ORANGE | Nearby, adjacent room possible |
| < -75 dBm | NOTICE | YELLOW | Distant or through wall |

### Speaker alert
- New device found > -65 dBm → 3 short beeps (high pitch)
- New device found -65 to -75 → 1 short beep
- Device gets closer (RSSI improves 10dBm+) → 2 beeps

### Key bindings
- `l` / `a` → next/prev page
- `c` → clear device list
- `s` → save to SD
- `h` → toggle channel hop on/off (lock to current channel)
- `q` → quit

### SD Logging
Save to `/logs/camdetect.log`:
```
[timestamp] CH:X | MAC | VENDOR | TYPE | RSSI | DURATION
```

---

## Integration with `trackme`

The camera OUI detection from `camdetect` can be integrated into `trackme`
as an optional additional scan layer. When running `trackme`:

- BLE side: already detects AirTag/Tile/Samsung etc.
- WiFi side: probe sniffing already implemented
- NEW: add OUI match against kCamOuis during WiFi probe sniff

If a probe request comes from a known camera OUI → add to trackme results
as a special "CAM" type device with THREAT_WARNING minimum level.

This means `trackme` in one command detects:
1. BLE trackers (AirTag, Tile, Samsung etc.)
2. WiFi surveillance devices following you (probe request patterns)
3. Camera devices broadcasting nearby (OUI match)

---

## Shared Infrastructure

Both commands share:

```cpp
// csi_common.h
void csi_init();           // init WiFi + CSI callbacks
void csi_deinit();         // cleanup CSI + promiscuous
void csi_enable();         // start CSI collection
void csi_disable();        // stop CSI collection

// promiscuous sniff already shared with trackme WiFi side
// reuse the same promiscuous setup/teardown pattern
```

---

## Files to Create

- `t-rex-firmware/sensing/csidetect.cpp/.h` — CSI presence detection command
- `t-rex-firmware/sensing/camdetect.cpp/.h` — Camera OUI detection command
- `t-rex-firmware/sensing/csi_common.cpp/.h` — Shared CSI init/callback

## Files to Modify

- `t-rex-firmware/shell.cpp` — register csidetect/cd and camdetect/cm
- `t-rex-firmware/trackme.cpp` — add OUI match to WiFi probe sniff loop
- `platformio.ini` — no new libraries needed (all ESP-IDF built-in)
- `README.md` — add both commands to features and commands table

---

## Radio Conflict Rules

```
csidetect running:
  → promiscuous ON + CSI ON
  → cannot run: deauth, wifimon, eviltwin, trackme WiFi, camdetect

camdetect running:
  → promiscuous ON (channel hopping)
  → cannot run: deauth, wifimon, eviltwin, trackme, csidetect

Both are foreground commands — conflicts auto-resolve on exit.
On start of either command: check if WiFi is in conflicting state,
warn user and abort if necessary.
```

---

## License Note

The reference repo (skizzophrenic/Cardputer-CSI-Human-Detector) is
**MIT licensed** — you can freely use, adapt, and distribute the code
with attribution.

MIT requirements:
1. Keep the original copyright notice in adapted files
2. Include the MIT license text or a reference to it

Add this to the top of any file that adapts code from the reference repo:
```cpp
// Portions adapted from Cardputer-CSI-Human-Detector
// https://github.com/skizzophrenic/Cardputer-CSI-Human-Detector
// MIT License — Copyright (c) skizzophrenic (TalkingSasquach)
```

**Also discovered:** Espressif has an official esp-csi repository:
https://github.com/espressif/esp-csi
This is the official CSI reference from the chip maker itself — Apache 2.0
licensed and fully compatible with AGPL-3.0. It includes human detection
examples specifically for ESP32-S3. Worth cross-referencing for the
algorithm implementation.

The OUI table is factual data (public MAC registry) — no license concern.

---

## Coding Standards (same as rest of T-Rex)

- No dynamic allocation
- No STL containers
- Fixed-size buffers only
- CSI callback MUST be IRAM_ATTR
- Promiscuous callback MUST be IRAM_ATTR
- Follow RED header UI pattern
- Consistent nav keys (l/a/q/c/s)
- All display via dm. methods
- All input via inputHandler.getKeyboardInput()
- T-Deck Plus GPS features behind #ifdef BOARD_TDECK_PLUS

---

## README Updates Needed

### Add to Features → new section 🔍 Sensing:
| Feature | Status |
|---------|--------|
| CSI human presence detection | 🔨 WIP |
| Hidden camera / spy device detector | 🔨 WIP |

### Add to Commands table:
| `csidetect` | `cd` | WiFi CSI human presence detector |
| `camdetect` | `cm` | Hidden camera / spy device scanner |

### Add to Roadmap:
- [ ] CSI human presence detection (csidetect)
- [ ] Hidden camera detector with OUI database (camdetect)
- [ ] Camera OUI detection integrated into trackme
