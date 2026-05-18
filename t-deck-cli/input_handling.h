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
    uint32_t getLastActivityTime() const { return lastActivityTime; }
    void updateActivity() { lastActivityTime = millis(); }

private:
    uint32_t lastActivityTime = 0;
    bool     _tballLast[5]    = { true, true, true, true, true };
};

#endif // INPUT_HANDLING_H
