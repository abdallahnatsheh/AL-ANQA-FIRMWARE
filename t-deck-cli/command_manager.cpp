#include "command_manager.h"
#include "display_manager.h"
#include "utils.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "bluetooth_functions.h"
#include "sdcard_manager.h"
#include "wifimon_functions.h"
#include "deauth_functions.h"

extern DisplayManager  displayManager;
extern ESPInfoPrinter  espInfoPrinter;
extern WiFiFunctions   wifiFunctions;
extern BluetoothFunctions bluetoothFunctions;
extern SDCardManager   sdCardManager;
extern WiFiMonitor     wifiMonitor;
extern DeauthAttack    deauthAttack;

CommandManager::CommandManager() : commandIndex(0), commandCount(0) {
    resetCommand();
}

void CommandManager::registerCommand(const char* name, const char* shortName, CommandFunction function, const char* description, bool haveArgs) {
    if (commandCount < sizeof(commands) / sizeof(commands[0])) {
        commands[commandCount++] = {name, shortName, function, description, haveArgs};
    }
}

void CommandManager::processInput(char incoming) {
        if (incoming != 0) {
            if (incoming == '\n' || incoming == '\r') {
                displayManager.newLine();
                executeCommand();
            } else if (incoming == '\b' && commandIndex > 0) {
                commandIndex--;
                command[commandIndex] = '\0';
                displayManager.backspaceChar();
            } else if (commandIndex < bufferSize - 1) {
                command[commandIndex] = incoming;
                commandIndex++;
                command[commandIndex] = '\0';
                displayManager.printText(incoming);
            }
        }
}

void CommandManager::executeCommand() {
    displayManager.clearInputText();
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    for (int i = 0; i < commandCount; i++) {
        if ((commands[i].haveArgs && (Utils::startsWith(command, commands[i].name) || Utils::startsWith(command, commands[i].shortName))) ||
            (!commands[i].haveArgs && strcmp(command, commands[i].name) == 0) ||
            (!commands[i].haveArgs && strcmp(command, commands[i].shortName) == 0)) {
            
            char* args = NULL;
            
            if (commands[i].haveArgs) {
                const char* cmdPrefix = (Utils::startsWith(command, commands[i].shortName)) ? commands[i].shortName : commands[i].name;
                args = command + strlen(cmdPrefix);
                if (*args == ' ') {
                    args++;
                }
                if (args[0] == '\0') {
                    args = NULL;
                }
            }
            commands[i].function(args);
            resetCommand();
            return;
        }
    }
    displayManager.println("Invalid command. Type 'help' to see available commands.");
    displayManager.printCommandScreen();
    resetCommand();
}


void CommandManager::resetCommand () {
    memset(command, 0, bufferSize);
    commandIndex = 0;
}

void CommandManager::setupCommands() {
    registerCommand("info", "inf", [](char* args) { espInfoPrinter.printESPInfo(); }, "Print T-DECK information", false);
    registerCommand("clear", "clr", [](char* args) { displayManager.tdeck_begin(); }, "Clear the screen", false);
    registerCommand("scanwifi","sw", [](char* args) { wifiFunctions.scanWiFiNetworks(); }, "Scan for available Wi-Fi networks",false);
    registerCommand("connectwifi","cw", [](char* args) { wifiFunctions.connectToWiFiCommand(args); }, "Connect to a Wi-Fi network",true);
    registerCommand("clearwifi", "clrw", [](char* args) { wifiFunctions.clearAllWiFiCredentials(); }, "Clear the saved wifi networks", false);
    registerCommand("netdiscover","nd", [](char* args) { wifiFunctions.networkDiscovery(); }, "Discover devices on the network - ping scan",false);
    registerCommand("portscan","ps", [](char* args) { wifiFunctions.networkPortScan(args); }, "Simple port scanning - ping scan", true);
    registerCommand("MATRIX","matrix", [](char* args) { displayManager.launchMatrixAnimation(); }, "Launch matrix animation",false);
    registerCommand("scanblue","sbl", [](char* args) { bluetoothFunctions.scanBluetoothDevices(); }, "Scan for Bluetooth devices",false);
    registerCommand("help",    "hlp", [](char* args) { Utils::printHelp(args); },                         "Print this help message",            true);
    registerCommand("sdinfo",  "sdi", [](char* args) { sdCardManager.printInfo(); },                       "SD card type and storage info",       false);
    registerCommand("sdls",    "ls",  [](char* args) { sdCardManager.listDirectory(args && *args ? args : "/"); }, "List SD card directory [path]",  true);
    registerCommand("sdread",  "sdr", [](char* args) { if (args && *args) sdCardManager.readFile(args); else displayManager.println("Usage: sdread <path>"); displayManager.printCommandScreen(); }, "Read file from SD card",  true);
    registerCommand("sdrm",    "srm", [](char* args) { if (args && *args) sdCardManager.removeFile(args); else displayManager.println("Usage: sdrm <path>"); displayManager.printCommandScreen(); },  "Delete file from SD card", true);
    registerCommand("wifimon", "wm",  [](char* args) {
        int ch = 0;
        if (args && *args) ch = atoi(args);
        wifiMonitor.start(ch);
    }, "WiFi monitor mode [channel 1-13, 0=hop]", true);
    registerCommand("deauth",  "da",  [](char* args) { deauthAttack.start(args); },
        "Deauth attack: deauth <bssid> [ch] [client]", true);
}