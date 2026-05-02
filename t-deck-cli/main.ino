#include "command_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "bluetooth_functions.h"
#include "sdcard_manager.h"
#include "wifimon_functions.h"

LGFX tft;
DisplayManager   displayManager(tft);
CommandManager   commandManager;
InputHandling    inputHandler;
ESPInfoPrinter   espInfoPrinter(displayManager);
WiFiFunctions    wifiFunctions(displayManager);
BluetoothFunctions bluetoothFunctions;
SDCardManager    sdCardManager(displayManager);
WiFiMonitor      wifiMonitor(displayManager, sdCardManager);

void setup() {
    Serial.begin(115200);
    displayManager.init();
    inputHandler.begin();

    if (!sdCardManager.begin()) {
        Serial.println("SD card not found or failed to mount.");
    }

    commandManager.setupCommands();
}

void loop() {
    char input = inputHandler.getKeyboardInput();
    commandManager.processInput(input);
}
