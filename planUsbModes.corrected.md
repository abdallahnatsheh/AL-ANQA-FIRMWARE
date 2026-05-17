# USB Gadget Plan — Corrected for T-DECK / ESP32-S3
> Replaces both planUsbModes.prompt.md and USB-MODES-PROMPT.TXT

---

## Implementation Status (2026-05-17)

**SHIPPED — USB MSC + HID working** (`usb_manager.cpp`, commit `392a3f9`)

### What was actually implemented (differs from plan below)

The plan below describes `f_unmount()` + `sdmmc_read_sectors()` for raw sector access.
What actually works on T-Deck is simpler: **`SD.readRAW()` / `SD.writeRAW()`** via the
Arduino SD library, routed through a FreeRTOS queue so all SPI2 access is serialized
to the main Arduino task (the same task that owns LovyanGFX).

### Root causes found during implementation

1. **RADIO_CS_PIN (GPIO9) not held HIGH** — the LoRa SX1262 shares SPI2 (same
   SCK/MOSI/MISO as SD). If its CS floats or is LOW during SD reads, the radio
   drives MISO and corrupts every transfer. Fix: `digitalWrite(RADIO_CS_PIN, HIGH)`
   before every MSC session. Confirmed by LilyGO UnitTest.ino.

2. **SPI2 shared by three devices** — display (CS=12), SD (CS=39), radio (CS=9).
   LovyanGFX uses `spi_device_acquire_bus()` (ESP-IDF); Arduino SD uses an
   internal FreeRTOS semaphore. These don't coordinate. Fix: queue all SD I/O
   through the main task so only one SPI context runs at a time.

3. **Insufficient write retries** — SD cards stall during wear-levelling. Fix:
   reads 10×20ms, writes 15×100ms back-off.

4. **SD remount after MSC** — After `SD.end()`, send 80 SPI clocks with CS HIGH
   (SD spec reset), then retry `SD.begin()` up to 8 times at 500ms intervals.

### Commands shipped
- `usbmsc` / `um` — Mass Storage (expose SD card to PC)
- `usbhid` / `uh` — HID keyboard test (types "T-Rex HID Test")

### Tested
- Read files from SD via USB MSC ✓
- Write 2 MB image to SD via USB MSC ✓
- Cable unplug → Q → `ls` (SD remounts) ✓
- HID keyboard types into Notepad ✓

### Still pending
- BadUSB / DuckyScript engine (see plan below)

---

---

## What Both Plans Get WRONG

### 1. Hot-switching USB modes at runtime is not possible on ESP32-S3
Both plans describe switching MSC → HID → OFF like flipping a switch.
TinyUSB on ESP32-S3 does NOT work that way.

- `USB.begin()` is called once at startup (or automatically via ARDUINO_USB_CDC_ON_BOOT)
- The USB descriptor (what the PC sees) is FIXED after USB.begin()
- You cannot add or remove interfaces (MSC, HID) at runtime without a USB reconnect
- `USB.end()` does NOT exist in arduino-esp32 — calling it will not compile

**The correct model:** register ALL interfaces (CDC + MSC + HID) at startup as a
COMPOSITE device. At runtime, control BEHAVIOR per interface:
- MSC: call `msc.mediaPresent(true/false)` — PC sees card ejected when false
- HID: only send keystrokes when in active HID mode, otherwise idle
- SD: firmware-side lock prevents corruption regardless of mode

### 2. `SD.end()` before MSC kills the card handle needed for block access
Both plans say: call SD.end(), then use the card for raw sector reads.
This is a contradiction. SD.end() calls esp_vfs_fat_sdcard_unmount() which
deinitializes the SPI host and nulls the internal card pointer.

After SD.end(), you have no card handle. sdmmc_read_sectors() needs that handle.

**The correct approach:** when entering MSC mode:
- Save card size from SD before ending
- Call f_unmount() to detach the FAT VFS without destroying the SPI driver
- Keep the sdmmc_card_t* valid by NOT calling SD.end()
- Use sdmmc_read_sectors() / sdmmc_write_sectors() with the live card pointer
- On MSC exit: call SD.begin() to remount the FAT VFS cleanly

### 3. Plan 1 puts USB inside SDCardManager — wrong
SDCardManager handles files. USB is a separate hardware subsystem.
All other modules in this project are separate .cpp/.h pairs. USB is no different.

### 4. ARDUINO_USB_CDC_ON_BOOT=1 is already active in platformio.ini
This means Serial already runs over USB CDC from boot. Any USB work must
be a composite device: CDC + MSC + HID. All three interfaces must be registered
BEFORE the auto-init that ARDUINO_USB_CDC_ON_BOOT triggers (i.e. before setup()).
This is done via global constructors — the USB* objects must be global.

### 5. Plan 2's "composite mode" as a runtime state is wrong
Composite is a USB descriptor concept, not a runtime mode. You don't switch
INTO composite — composite is always the device type. What you control at
runtime is which interfaces are active/presenting data.

### 6. Command style doesn't match the project
This project uses: registerCommand("name", "shortname", lambda, "desc", hasArgs, "Category")
- Max 64 commands total
- Short names matter (keyboard input is slow on T-DECK)
- Commands are one-liners in setupCommands()
Neither plan follows this.

### 7. Plan 1's SD command structure "sd usb / sd usb off / sd hid" is wrong
These aren't how commands work here. "sd" is not a namespace prefix.

---

## Hardware Reality

- ESP32-S3 has native USB (no bridge chip) → TinyUSB works natively
- SPI bus SPI2_HOST is SHARED: LovyanGFX (display) + SD card (CS=39)
  - LovyanGFX holds the SPI bus with bus_shared=true
  - SD uses the same SPI bus via its own CS
  - When SD is released for MSC, the display SPI still runs — don't reinit SPI
- SD SPI pins: MOSI=41, MISO=38, SCK=40, CS=39
- USB hardware: built into ESP32-S3, no extra pins needed
- Available TinyUSB headers in arduino-esp32:
  - USB.h — core USB control
  - USBMSC.h — Mass Storage
  - USBHIDKeyboard.h — HID keyboard
  (NOT "USBHID.h" — that doesn't exist as a standalone header)

---

## Corrected Architecture

### New module: usb_gadget.cpp / usb_gadget.h

Single singleton (like PowerSaveManager). Owns all USB state.
SDCardManager becomes a consumer: checks UsbGadget::getInstance().isSDAllowed()

```
UsbGadget (singleton)
├── USBMSC msc          — block-level SD exposure
├── USBHIDKeyboard hid  — keyboard HID
├── BadUsbEngine engine — sandboxed DuckyScript interpreter
└── UsbState state      — OFF / MSC / HID / COMPOSITE
```

### State machine (corrected)

```
enum class UsbState { OFF, MSC, HID, COMPOSITE };
```

Valid transitions (all require USB reconnect to PC — device stays composite,
we just change what media/reports are active):

```
OFF  → MSC        msc.mediaPresent(true),  SD locked
OFF  → HID        HID keyboard ready,      SD still accessible
OFF  → COMPOSITE  both active,             SD locked
MSC  → OFF        msc.mediaPresent(false), SD.begin() remount
HID  → OFF        HID idle
COMPOSITE → OFF   both down, SD remount
```

Note: "switching" here means behavior change only. The USB descriptor
does NOT change. The PC may need to refresh (re-plug or OS re-enum).

---

## Module Spec

### usb_gadget.h

```cpp
#pragma once
#include <Arduino.h>
#include <USBMSC.h>
#include <USBHIDKeyboard.h>

enum class UsbState { OFF, MSC, HID, COMPOSITE };

class BadUsbEngine {
public:
    bool    loadScript(const char* path);  // reads from SD_DIR_SCRIPTS
    bool    executeStep();                 // call from loop, one keystroke per step
    void    stop();
    bool    isRunning() const;
private:
    // file handle + current position + rate limiting
};

class UsbGadget {
public:
    static UsbGadget& getInstance();

    void    init();          // call in setup() — registers MSC+HID before USB auto-start

    bool    startMSC();      // lock SD, msc.mediaPresent(true)
    bool    startHID();      // SD stays accessible, HID enters active mode
    bool    startComposite();// lock SD, both MSC+HID active
    bool    stop();          // release all modes, remount SD

    bool    execScript(const char* path); // load + start BadUSB script
    void    update();        // call from loop() — drives BadUsbEngine steps

    bool    isSDAllowed() const;
    UsbState getState() const;
    void    printStatus();

private:
    UsbGadget() = default;
    UsbState      _state   = UsbState::OFF;
    bool          _sdLocked = false;
    BadUsbEngine  _engine;

    // MSC callbacks (static, passed to USBMSC)
    static int32_t onMscRead (uint32_t lba, uint32_t offset, void* buf, uint32_t size);
    static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t size);
    static bool    onMscStartStop(uint8_t power, bool start, bool loadEject);
};

// Global objects — must be global for USB registration before setup()
extern USBMSC         usbMsc;
extern USBHIDKeyboard usbHid;
extern UsbGadget      usbGadget;
```

### usb_gadget.cpp — key implementation notes

**init():**
```cpp
void UsbGadget::init() {
    // Register MSC
    usbMsc.vendorID("TDECK");
    usbMsc.productID("SD-STORE");
    usbMsc.productRevision("1.0");
    usbMsc.onRead(onMscRead);
    usbMsc.onWrite(onMscWrite);
    usbMsc.onStartStop(onMscStartStop);
    usbMsc.mediaPresent(false);   // start with no media exposed
    usbMsc.begin();

    usbHid.begin();
    // USB.begin() is called automatically by ARDUINO_USB_CDC_ON_BOOT
}
```

**startMSC():**
```cpp
bool UsbGadget::startMSC() {
    if (!sdCardManager.isReady()) return false;

    // Get card handle BEFORE any unmount
    sdmmc_card_t* card = SD.card();
    if (!card) return false;

    // Save sector count
    uint64_t sectors = SD.totalBytes() / 512;

    // Unmount FAT only — keeps SPI driver + card handle alive
    // SD.card() pointer remains valid after this
    f_unmount("0:");   // detach FAT volume from VFS

    _sdLocked = true;
    _state = UsbState::MSC;

    usbMsc.begin(sectors, 512);
    usbMsc.mediaPresent(true);   // PC now sees the storage device
    return true;
}
```

**stop():**
```cpp
bool UsbGadget::stop() {
    usbMsc.mediaPresent(false);

    // Give PC time to notice media is gone
    delay(300);

    _sdLocked = false;
    _state = UsbState::OFF;

    // Remount FAT through Arduino SD (reinits VFS layer)
    // Do NOT call SPI.begin() — LovyanGFX still owns the SPI bus
    return SD.begin(BOARD_SDCARD_CS, SPI, 4000000);
}
```

**MSC read callback:**
```cpp
int32_t UsbGadget::onMscRead(uint32_t lba, uint32_t offset, void* buf, uint32_t size) {
    sdmmc_card_t* card = SD.card();
    if (!card) return -1;
    uint32_t count = size / 512;
    esp_err_t err = sdmmc_read_sectors(card, buf, lba, count);
    return (err == ESP_OK) ? (int32_t)size : -1;
}
```

**MSC write callback:**
```cpp
int32_t UsbGadget::onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t size) {
    sdmmc_card_t* card = SD.card();
    if (!card) return -1;
    uint32_t count = size / 512;
    esp_err_t err = sdmmc_write_sectors(card, buf, lba, count);
    return (err == ESP_OK) ? (int32_t)size : -1;
}
```

**isSDAllowed():**
```cpp
bool UsbGadget::isSDAllowed() const {
    return !_sdLocked;
}
```

---

## SDCardManager integration

Add to the top of every SD operation (appendLine, removeFile, readFile,
ensureDir, formatSDCard, listDirectory):

```cpp
if (!UsbGadget::getInstance().isSDAllowed()) {
    displayManager.println("SD locked: USB mode active.");
    return false;
}
```

`begin()` and `isReady()` are exempt — they don't touch file data.

---

## BadUSB Engine

Scripts live in `/scripts/` on SD (SD_DIR_SCRIPTS already defined).
Format: DuckyScript-compatible subset.

Supported tokens:
```
REM <comment>
DELAY <ms>
STRING <text>
ENTER
TAB
BACKSPACE
CTRL <key>
ALT <key>
SHIFT <key>
GUI <key>
CTRL ALT <key>
CTRL SHIFT <key>
```

Engine rules:
- File is read line-by-line from SD (not loaded into RAM — T-DECK has limited heap)
- Execution is step-by-step via update() called from loop()
- Default typing speed: 80ms between characters (human-like)
- DELAY token is non-blocking (millis-based)
- Cancellable by pressing 'q' (checked each step via inputHandler)
- Requires EXPLICIT user confirmation before first keystroke:
  press 'y' within 5 seconds or execution aborts
- NO auto-execution on boot, ever

---

## Commands to register in setupCommands()

Add to "SD Card" category:

```cpp
registerCommand("usbmsc",    "um",  [](char* a) { usbGadget.startMSC(); },        "USB Mass Storage (expose SD to PC)",   false, "SD Card");
registerCommand("usbhid",    "uh",  [](char* a) { usbGadget.startHID(); },         "USB HID keyboard mode",                false, "SD Card");
registerCommand("usbcomp",   "uc",  [](char* a) { usbGadget.startComposite(); },   "USB Composite (MSC + HID)",            false, "SD Card");
registerCommand("usboff",    "uo",  [](char* a) { usbGadget.stop(); },             "Stop USB mode, remount SD",            false, "SD Card");
registerCommand("usbstatus", "ust", [](char* a) { usbGadget.printStatus(); },      "Show USB mode and SD lock state",      false, "SD Card");
registerCommand("usbexec",   "ux",  [](char* a) { if (a&&*a) usbGadget.execScript(a); else displayManager.println("Usage: ux <script>"); }, "Run BadUSB script: ux <name>", true, "SD Card");
```

Note: command count check — currently ~28 commands registered, max 64.
These 6 bring total to ~34. Safe.

---

## main.ino changes

```cpp
// At top level (global):
#include "usb_gadget.h"

UsbGadget usbGadget;   // must be global for USB pre-init

// In setup(), before commandManager.setupCommands():
usbGadget.init();

// In loop():
usbGadget.update();    // drives BadUSB script stepping
```

---

## platformio.ini — no new flags needed

The existing flags are sufficient. Do NOT add:
- -DUSE_TINYUSB (not needed, arduino-esp32 handles this)
- -DARDUINO_USB_MODE=1 (already implied by ARDUINO_USB_CDC_ON_BOOT)

TinyUSB headers (USB.h, USBMSC.h, USBHIDKeyboard.h) are part of
the arduino-esp32 core bundled with espressif32 platform. No lib_deps needed.

---

## Files to create

1. t-deck-cli/usb_gadget.h
2. t-deck-cli/usb_gadget.cpp
3. Modify: t-deck-cli/sdcard_manager.cpp (add isSDAllowed checks)
4. Modify: t-deck-cli/command_manager.cpp (register 6 new commands)
5. Modify: t-deck-cli/main.ino (global UsbGadget, init(), update())

---

## Known open questions before implementation

1. Does f_unmount("0:") correctly detach FAT while keeping the sdmmc SPI driver
   alive on this specific ESP-IDF version? Needs a test build to confirm.
   Fallback: use esp_vfs_unregister() instead.

2. SD.card() pointer validity after f_unmount needs verification.
   The card struct lives in the SPI host, not the VFS layer, so it should survive.

3. The USB descriptor is built from CDC (ARDUINO_USB_CDC_ON_BOOT) + whatever
   USB* globals are declared. Order of global constructors matters.
   If MSC/HID don't appear in the descriptor, the PC won't see them.
   Need to verify composite descriptor is built correctly by arduino-esp32.

4. onMscStartStop callback: the PC will call this when ejecting the drive.
   We must handle the eject signal and set mediaPresent(false) without crashing.
