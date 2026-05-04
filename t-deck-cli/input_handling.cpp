#include "input_handling.h"
#include "utilities.h"
#include <Wire.h>

void InputHandling::begin() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
}

char InputHandling::getKeyboardInput() {
    if (digitalRead(BOARD_KEYBOARD_INT) == HIGH) return 0;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    if (!Wire.available()) return 0;
    return Wire.read();
}
