PRODUCTION USB GADGET STACK (ESP32-S3 / T-Deck)
Linux USB Gadget Architecture + Secure BadUSB Engine

GOAL:
Build a full USB subsystem with:
1. USB Mass Storage (MSC) - SD exposed to PC
2. USB HID (keyboard/mouse)
3. Composite USB (MSC + HID simultaneously)
4. Secure BadUSB scripting engine (controlled, sandboxed, NOT auto-executed)

────────────────────────────────────
1. ARCHITECTURE OVERVIEW
────────────────────────────────────

DO NOT embed USB inside SDCardManager.

Create a dedicated subsystem:

UsbGadget (CORE SYSTEM)

Layers:
- USB Controller (state machine)
- MSC Driver
- HID Driver
- BadUSB Script Engine
- System Lock (SD protection layer)

────────────────────────────────────
2. STATE MACHINE (CRITICAL)
────────────────────────────────────

enum class UsbState {
    OFF,
    MSC,
    HID,
    COMPOSITE,
    ERROR
};

Rules:
- Only one active mode at a time unless COMPOSITE
- OFF required before switching modes
- ERROR triggers recovery mode

Valid transitions:
OFF → MSC
OFF → HID
OFF → COMPOSITE
MSC → OFF
HID → OFF
COMPOSITE → OFF

Invalid:
MSC ↔ HID (must go through OFF)

────────────────────────────────────
3. SYSTEM LOCKING MODEL
────────────────────────────────────

Add global safety layer:

struct SystemLock {
    bool sdLocked;
    bool usbActive;
};

SD ACCESS RULE:
SD is allowed ONLY if:
- usbState == OFF
- sdLocked == false

ALL SD functions must enforce:
if (!usb.isSDAllowed()) return false;

────────────────────────────────────
4. USB MSC DRIVER (REAL IMPLEMENTATION)
────────────────────────────────────

Use ESP32-S3 TinyUSB:

#include "USB.h"
#include "USBMSC.h"

Responsibilities:
- Expose SD card as USB storage
- Block firmware SD access completely
- Use sector/block access (NOT file API)

MSC rules:
- SD.end() before start
- USB.begin() after init
- No filesystem calls while active

If raw SD sector API is unavailable:
- use SDMMC mode OR
- FatFs diskio wrapper

────────────────────────────────────
5. USB HID DRIVER
────────────────────────────────────

Use TinyUSB HID keyboard:

#include "USBHIDKeyboard.h"

Capabilities:
- Send keyboard input
- Send mouse input (future extension)

SAFE MODE REQUIREMENTS:
- No script execution yet
- Only allow test keystroke ("A")
- Must be manually triggered

────────────────────────────────────
6. COMPOSITE USB MODE
────────────────────────────────────

MSC + HID simultaneously:

Rules:
- SD fully locked from firmware
- MSC handles storage
- HID handles input device

Init order:
1. lock SD
2. SD.end()
3. init MSC
4. init HID
5. USB.begin()

────────────────────────────────────
7. BADUSB SCRIPT ENGINE (SECURE DESIGN)
────────────────────────────────────

DO NOT execute raw scripts directly.

Create sandbox engine:

class BadUsbEngine {
    bool loadScript(file);
    void executeStep();
    void stop();
};

SCRIPT FORMAT (DSL):

DELAY 1000
STRING hello
ENTER
CTRL ALT T

Execution rules:
- step-by-step execution
- rate-limited typing
- cancellable execution
- watchdog timeout

────────────────────────────────────
8. SECURITY MODEL (VERY IMPORTANT)
────────────────────────────────────

BadUSB MUST be disabled by default.

Add:

class UsbSecurity {
    bool scriptEnabled;
    bool requirePhysicalConfirm;
};

Rules:
- No auto execution on boot
- Scripts require explicit unlock (CLI or button)
- No SD-triggered auto HID execution

────────────────────────────────────
9. USB CONTROLLER (CORE CLASS)
────────────────────────────────────

class UsbGadget {
private:
    UsbState state;
    SystemLock lock;

    MSCDriver msc;
    HIDDriver hid;
    BadUsbEngine engine;

public:
    bool startMSC();
    bool startHID();
    bool startComposite();
    bool stop();

    bool isSDAllowed();
    UsbState getState();
};

────────────────────────────────────
10. SAFE STOP SEQUENCE
────────────────────────────────────

ALWAYS follow this order:

1. USB.end()
2. delay(300ms)
3. SPI reinit (important for ESP32-S3 + shared bus)
4. SD.begin()
5. restore filesystem checks
6. unlock SD
7. state = OFF

────────────────────────────────────
11. ERROR HANDLING / RECOVERY
────────────────────────────────────

If any failure occurs:
- set state = ERROR
- stop USB
- reinitialize SD
- reset to OFF
- unlock system

────────────────────────────────────
12. CLI DESIGN (LINUX GADGET STYLE)
────────────────────────────────────

usb status
usb msc
usb hid
usb composite
usb off
usb exec <script>
usb lock
usb unlock

────────────────────────────────────
13. INTEGRATION RULES
────────────────────────────────────

- SDCardManager becomes a consumer only
- All SD access MUST check UsbGadget::isSDAllowed()
- No direct USB logic inside SDCardManager
- USB subsystem owns hardware state

────────────────────────────────────
14. OUTPUT REQUIREMENTS
────────────────────────────────────

Generate:
1. UsbGadget.h
2. UsbGadget.cpp
3. MSCDriver implementation (ESP32-S3 compatible)
4. HIDDriver implementation (safe test mode only)
5. BadUsbEngine (sandboxed DSL interpreter)
6. SDCardManager integration changes
7. CLI command integration
8. Explanation of state machine + safety model

IMPORTANT:
- Must compile in PlatformIO ESP32-S3 Arduino
- No pseudo-code placeholders
- Must respect TinyUSB ESP32-S3 limitations
- Must include safe SD locking to prevent corruption