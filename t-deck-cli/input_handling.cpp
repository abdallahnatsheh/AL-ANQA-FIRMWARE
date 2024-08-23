#include "input_handling.h"
#include "utilities.h"
#include <Wire.h>

char InputHandling::getKeyboardInput(){
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    return Wire.available() > 0 ? Wire.read() : 0;
}

