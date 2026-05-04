#ifndef INPUT_HANDLING_H
#define INPUT_HANDLING_H

#include <Arduino.h>

class InputHandling {
public:
    void begin();
    char getKeyboardInput();
};

#endif // INPUT_HANDLING_H
