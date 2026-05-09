#ifndef INPUT_HANDLING_H
#define INPUT_HANDLING_H

#include <Arduino.h>

class InputHandling {
public:
    void begin();
    char getKeyboardInput();
    uint32_t getLastActivityTime() const { return lastActivityTime; }
    void updateActivity() { lastActivityTime = millis(); }

private:
    uint32_t lastActivityTime = 0;
};

#endif // INPUT_HANDLING_H
