#include "command_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "bluetooth_functions.h"

LGFX tft;
DisplayManager displayManager(tft);
CommandManager commandManager;
InputHandling inputHandler;
ESPInfoPrinter espInfoPrinter(displayManager);
WiFiFunctions wifiFunctions(displayManager);
BluetoothFunctions bluetoothFunctions;

void setup() {
    Serial.begin(115200);
    displayManager.init();
    commandManager.setupCommands();
}
void loop() {
    char input = inputHandler.getKeyboardInput();
    commandManager.processInput(input);
}