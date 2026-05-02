#ifndef INPUT_HANDLING_H
#define INPUT_HANDLING_H

#include <Arduino.h>

class InputHandling {
public:
    void begin();
    char getKeyboardInput();
private:
    static volatile bool keyAvailable;
    static void IRAM_ATTR onKeyInterrupt();
};

#endif // INPUT_HANDLING_H
