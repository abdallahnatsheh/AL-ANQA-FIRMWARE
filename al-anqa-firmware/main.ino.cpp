# 1 "C:\\Users\\ABDNAT~1\\AppData\\Local\\Temp\\tmp2wiw664l"
#include <Arduino.h>
# 1 "C:/Users/abd nat/Desktop/T-DECK PROJECTS/T-DECK-CLI/al-anqa-firmware/main.ino"
#include "splash_screen.h"
#include "command_manager.h"
#include "display_manager.h"
#include "battery_manager.h"
#include "input_handling.h"
#include "powersave_manager.h"
#include "vol_manager.h"
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
#include "pmkid_attack.h"
#include "mac_changer.h"
#include "man_pages.h"
#include "usb_manager.h"
#include "notification_manager.h"
#include "wguard.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include "weather_manager.h"
#include "touch_manager.h"
#include "undercover.h"

LGFX tft;
DisplayManager displayManager(tft);
BatteryManager batteryManager(displayManager);
CommandManager commandManager;
InputHandling inputHandler;
ESPInfoPrinter espInfoPrinter(displayManager);
WiFiFunctions wifiFunctions(displayManager);
NetworkScanner networkScanner(displayManager);
BluetoothFunctions bluetoothFunctions;
SDCardManager sdCardManager(displayManager);
WiFiMonitor wifiMonitor(displayManager, sdCardManager);
DeauthAttack deauthAttack(displayManager, wifiFunctions);
TrackMeScanner trackMe(displayManager, sdCardManager);
EvilTwin evilTwin(displayManager, sdCardManager);
HiddenSSID hiddenSSID(displayManager, wifiFunctions, deauthAttack);
HandshakeCapture handshakeCapture(displayManager, wifiFunctions, deauthAttack);
PmkidAttack pmkidAttack(displayManager, wifiFunctions, deauthAttack);
WGuard wGuard(displayManager, wifiFunctions);
ManPages manPages(displayManager);
void setup();
void loop();
#line 52 "C:/Users/abd nat/Desktop/T-DECK PROJECTS/T-DECK-CLI/al-anqa-firmware/main.ino"
void setup() {
    Serial.begin(115200);
    displayManager.init();
    inputHandler.begin();
    TouchManager::instance().begin();


    if (!sdCardManager.begin()) {
        Serial.println("SD card not found or failed to mount.");
    }

    if (ucBootCoverEnabled()) {

        tft.fillScreen(TFT_BLACK);
    } else {
        showSplashScreen();
        displayManager.tdeck_begin();
    }

    ClockManager::instance().init();
    WeatherManager::instance().init();
    MacChanger::getInstance().begin();
    PowerSaveManager::getInstance().init(&batteryManager);
    usbManager.begin();
    NotificationManager::getInstance().begin();
    NotificationManager::getInstance().setWakeCallback([]() {
        PowerSaveManager::getInstance().forceWake();
    });

    commandManager.setupCommands();
    loadVolConf();
    ucInit();





    LockScreenManager::getInstance().init();
}

void loop() {
    char input = inputHandler.getKeyboardInput();
    commandManager.processInput(input);

    TrackballEvent evt = inputHandler.getTrackballEvent();
    evt = LockScreenManager::getInstance().interceptTrackball(evt);
    commandManager.processTrackball(evt);

    TouchEvent te = TouchManager::instance().poll();
    if (te.type != TouchEvent::NONE) {
        inputHandler.updateActivity();
        LockScreenManager::getInstance().updateActivity();



    }
    te = LockScreenManager::getInstance().interceptTouch(te);

}