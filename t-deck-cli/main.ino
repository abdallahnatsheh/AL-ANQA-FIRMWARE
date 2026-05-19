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
#include "hidden_ssid.h"
#include "handshake_capture.h"
#include "mac_changer.h"
#include "man_pages.h"
#include "usb_manager.h"

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
HiddenSSID       hiddenSSID(displayManager, wifiFunctions, deauthAttack);
HandshakeCapture handshakeCapture(displayManager, wifiFunctions, deauthAttack);
ManPages         manPages(displayManager);

void setup() {
    Serial.begin(115200);
    displayManager.init();
    inputHandler.begin();
    showSplashScreen();
    displayManager.tdeck_begin();

    if (!sdCardManager.begin()) {
        Serial.println("SD card not found or failed to mount.");
    }

    MacChanger::getInstance().begin();
    PowerSaveManager::getInstance().init(&batteryManager);
    usbManager.begin();

    commandManager.setupCommands();
}

void loop() {
    char input = inputHandler.getKeyboardInput();
    commandManager.processInput(input);

    TrackballEvent evt = inputHandler.getTrackballEvent();

    // Double-click detection: two clicks within 400 ms → toggle screen off
    static uint32_t lastClickTime = 0;
    if (evt == TBALL_CLICK) {
        uint32_t now = millis();
        if (now - lastClickTime < 400) {
            PowerSaveManager::getInstance().toggleManualOff();
            lastClickTime = 0;  // reset so triple-click doesn't re-trigger
            return;             // don't pass click to command manager
        }
        lastClickTime = now;
    }

    commandManager.processTrackball(evt);
}
