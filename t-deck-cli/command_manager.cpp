#include "command_manager.h"
#include "display_manager.h"
#include "utils.h"
#include "esp_info.h"
#include "wifi_functions.h"
#include "network_scanner.h"
#include "bluetooth_functions.h"
#include "sdcard_manager.h"
#include "wifimon_functions.h"
#include "deauth_functions.h"
#include "trackme.h"
#include "eviltwin.h"
#include "gps_manager.h"
#include "input_handling.h"

extern DisplayManager     displayManager;
extern ESPInfoPrinter     espInfoPrinter;
extern WiFiFunctions      wifiFunctions;
extern NetworkScanner     networkScanner;
extern BluetoothFunctions bluetoothFunctions;
extern SDCardManager      sdCardManager;
extern WiFiMonitor        wifiMonitor;
extern DeauthAttack       deauthAttack;
extern TrackMeScanner     trackMe;
extern EvilTwin           evilTwin;
extern InputHandling      inputHandler;

// Temp test commands (no header needed — forward-declared here)
void runGpsTest();
void runSpeakerTest();

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
    registerCommand("netdiscover","nd", [](char* args) { networkScanner.networkDiscovery(); },    "ARP scan local subnet — shows IP + MAC",    false);
    registerCommand("portscan",  "ps", [](char* args) { networkScanner.networkPortScan(args); }, "Port scan: ps <ip|arpIdx> <start> <end>",   true);
    registerCommand("topscan",   "ts", [](char* args) { networkScanner.topPortScan(args); },     "Top ports scan: ts <ip|arpIdx>",             true);
    registerCommand("ping",      "pg", [](char* args) { networkScanner.pingHost(args); },         "Ping host: ping <ip or hostname>",            true);
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
    registerCommand("eviltwin", "et",  [](char* args) { evilTwin.start(args); },
        "Evil Twin AP: et [ssid]", true);
    registerCommand("trackme", "tm",  [](char* args) {
        bool silent = args && (strncmp(args, "s", 1) == 0 || strncmp(args, "silent", 6) == 0);
        trackMe.start(silent);
    }, "Anti-tracking scanner (BLE+WiFi): tm [s]ilent", true);
    registerCommand("gpstest", "gt",  [](char* args) { runGpsTest(); },
        "GPS coordinate test", false);
    registerCommand("spktest", "st",  [](char* args) { runSpeakerTest(); },
        "Speaker tone test", false);

#ifdef BOARD_TDECK_PLUS
    registerCommand("gpson", "gon", [](char* args) {
        GpsManager& gm = GpsManager::instance();
        if (!gm.isRunning()) {
            displayManager.println("Starting GPS background task...");
            gm.start();
            displayManager.println("GPS task running. Use gpsoff to stop.");
        }
        // Live status screen — exit with any key, GPS stays on
        displayManager.clearScreen();
        while (true) {
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.printText("GPS BACKGROUND STATUS");
            displayManager.setCursor(4, outputY + LINE_HEIGHT);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText("─────────────────────────────");
            char line[48];
            if (gm.isValid()) {
                displayManager.setCursor(4, outputY + LINE_HEIGHT * 2);
                displayManager.setTextColor(TFT_GREEN);
                displayManager.printText("Status: FIX             ");
                snprintf(line, sizeof(line), "Sats:   %lu / 12         ", (unsigned long)gm.satellites());
                displayManager.setCursor(4, outputY + LINE_HEIGHT * 3);
                displayManager.printText(line);
                if (gm.timeValid()) {
                    snprintf(line, sizeof(line), "UTC:    %02d:%02d:%02d        ",
                             (int)gm.hour(), (int)gm.minute(), (int)gm.second());
                    displayManager.setCursor(4, outputY + LINE_HEIGHT * 4);
                    displayManager.printText(line);
                }
            } else {
                uint32_t sats = gm.satellites();
                displayManager.setCursor(4, outputY + LINE_HEIGHT * 2);
                displayManager.setTextColor(sats >= 4 ? TFT_YELLOW : TFT_ORANGE);
                displayManager.printText("Status: searching       ");
                snprintf(line, sizeof(line), "Sats:   %lu / 12         ", (unsigned long)sats);
                displayManager.setCursor(4, outputY + LINE_HEIGHT * 3);
                displayManager.printText(line);
            }
            displayManager.setCursor(4, outputY + LINE_HEIGHT * 6);
            displayManager.setTextColor(TFT_DARKGREY);
            displayManager.printText("GPS stays on in background");
            displayManager.setCursor(4, outputY + LINE_HEIGHT * 7);
            displayManager.printText("[any key] return to CLI");

            vTaskDelay(pdMS_TO_TICKS(500));
            char k = inputHandler.getKeyboardInput();
            if (k) break;
        }
        displayManager.tdeck_begin();
    }, "Start GPS background task (T-Deck Plus)", false);

    registerCommand("gpsoff", "gof", [](char* args) {
        GpsManager& gm = GpsManager::instance();
        if (gm.isRunning()) {
            displayManager.println("Stopping GPS background task...");
            gm.stop();
            displayManager.println("GPS stopped.");
        } else {
            displayManager.println("GPS background task not running.");
        }
    }, "Stop GPS background task (T-Deck Plus)", false);
#endif
}