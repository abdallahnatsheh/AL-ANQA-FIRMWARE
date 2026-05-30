#ifndef INPUT_HANDLING_H
#define INPUT_HANDLING_H

// Autocomplete trigger: apostrophe (Sym+K on T-DECK keyboard → 0x27)
#define KEY_AUTOCOMPLETE '\''

#include <Arduino.h>

enum TrackballEvent { TBALL_NONE, TBALL_LEFT, TBALL_RIGHT, TBALL_UP, TBALL_DOWN, TBALL_CLICK };

class InputHandling {
public:
    void begin();
    char           getKeyboardInput();
    TrackballEvent getTrackballEvent();
    void           clearPendingClicks();
    uint32_t getLastActivityTime() const { return lastActivityTime; }
    void updateActivity() { lastActivityTime = millis(); }

private:
    uint32_t lastActivityTime = 0;
    bool     _tballLast[4]   = { true, true, true, true };

    // Backspace key-repeat state.
    // No release detection: I2C keyboard sends one byte on press then silence.
    // Any non-backspace key cancels the pending repeat.
    char     _repeatKey        = 0;
    uint32_t _repeatStart      = 0;     // time of last physical backspace press
    uint32_t _repeatLast       = 0;     // last time we returned this key (physical or synthetic)
    uint32_t _lastBsReturnMs = 0;       // last time \b was actually delivered (physical or
                                        // synthetic); arm only when gap >= kRepeatDelayMs

    static constexpr uint32_t kRepeatDelayMs = 1500; // hold time before first repeat
    static constexpr uint32_t kRepeatRateMs  = 80;   // interval between repeats
};

#endif // INPUT_HANDLING_H
