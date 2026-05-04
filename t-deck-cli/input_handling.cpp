#include "input_handling.h"
#include "utilities.h"
#include <Wire.h>

void InputHandling::begin() {
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    delay(300);
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available()) Wire.read();
}

char InputHandling::getKeyboardInput() {
    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if (now - lastPoll < 10) return 0;
    lastPoll = now;
    if (Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1) == 0) return 0;
    return Wire.read();
}
