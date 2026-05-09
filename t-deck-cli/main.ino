#include "splash_screen.h"
#include "command_manager.h"
#include "display_manager.h"
#include "battery_manager.h"
#include "input_handling.h"
#include "powersave_manager.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "network_scanner.h"
#include "bluetooth_functions.h"
#include "sdcard_manager.h"
#include "wifimon_functions.h"
#include "deauth_functions.h"
#include "task_manager.h"
#include "trackme.h"
#include "eviltwin.h"

LGFX tft;
DisplayManager   displayManager(tft);
BatteryManager   batteryManager(displayManager);
CommandManager   commandManager;
InputHandling    inputHandler;
ESPInfoPrinter   espInfoPrinter(displayManager);
WiFiFunctions    wifiFunctions(displayManager);
NetworkScanner   networkScanner(displayManager);
BluetoothFunctions bluetoothFunctions;
SDCardManager    sdCardManager(displayManager);
WiFiMonitor      wifiMonitor(displayManager, sdCardManager);
DeauthAttack     deauthAttack(displayManager, wifiFunctions);
TrackMeScanner   trackMe(displayManager, sdCardManager);
EvilTwin         evilTwin(displayManager, sdCardManager);

void setup() {
    Serial.begin(115200);
    displayManager.init();
    inputHandler.begin();
    showSplashScreen();
    displayManager.tdeck_begin();

    if (!sdCardManager.begin()) {
        Serial.println("SD card not found or failed to mount.");
    }

    // Initialize PowerSaveManager after battery manager
    PowerSaveManager::getInstance().init(&batteryManager);

    commandManager.setupCommands();
}

void loop() {
    char input = inputHandler.getKeyboardInput();
    commandManager.processInput(input);
}
