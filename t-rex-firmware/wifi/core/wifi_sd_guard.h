#pragma once
// T-REX — WiFi↔SD GDMA safety guard
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ESP32-S3 shares the GDMA controller between WiFi and SPI (SD). Writing to the
// SD card while promiscuous-mode RX DMA is live can corrupt FatFS. The rule
// (see CLAUDE.md) is: pause promiscuous, do the SD write, resume.
//
// This RAII guard encodes that rule as a type so callers can't forget it:
//
//     {
//         ScopedPromiscPause _;          // promiscuous paused here (if it was on)
//         sd.appendLine(PATH, row);      // safe SD write
//     }                                  // promiscuous restored to prior state
//
// It is *self-correcting*: it reads the current promiscuous state in the ctor
// and only restores what it found. Dropping it into code where promiscuous was
// never enabled is a harmless no-op — so it is always safe to add, never unsafe.
// This makes it suitable for new code AND for retrofitting existing modules
// without having to reason about whether promiscuous happens to be on.

#include "esp_wifi.h"

class ScopedPromiscPause {
public:
    ScopedPromiscPause() : _wasOn(false) {
        // esp_wifi_get_promiscuous leaves *_wasOn untouched on error — start false.
        esp_wifi_get_promiscuous(&_wasOn);
        if (_wasOn) esp_wifi_set_promiscuous(false);
    }
    ~ScopedPromiscPause() {
        if (_wasOn) esp_wifi_set_promiscuous(true);
    }

    bool wasActive() const { return _wasOn; }

    // Non-copyable / non-movable — must stay scoped to the write window.
    ScopedPromiscPause(const ScopedPromiscPause&)            = delete;
    ScopedPromiscPause& operator=(const ScopedPromiscPause&) = delete;

private:
    bool _wasOn;
};
