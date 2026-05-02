#include "input_handling.h"
#include "utilities.h"
#include <Wire.h>

volatile bool InputHandling::keyAvailable = false;

void IRAM_ATTR InputHandling::onKeyInterrupt() {
    keyAvailable = true;
}

void InputHandling::begin() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOARD_KEYBOARD_INT), onKeyInterrupt, FALLING);
}

char InputHandling::getKeyboardInput() {
    if (!keyAvailable) return 0;
    keyAvailable = false;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    return Wire.available() > 0 ? Wire.read() : 0;
}
