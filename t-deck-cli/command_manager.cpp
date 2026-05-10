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
#include "hidden_ssid.h"
#include "handshake_capture.h"
#include "powersave_manager.h"
#include "mac_changer.h"
#include "man_pages.h"
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
extern HiddenSSID         hiddenSSID;
extern HandshakeCapture   handshakeCapture;
extern ManPages           manPages;

// Forward declarations for standalone command functions
void runGpsOn();
void runGpsOff();
void runGpsTest();
void runSpeakerTest();
void runLoraTest();

CommandManager::CommandManager() : commandIndex(0), commandCount(0) {
    resetCommand();
}

void CommandManager::registerCommand(const char* name, const char* shortName, CommandFunction function, const char* description, bool haveArgs, const char* category) {
    if (commandCount < sizeof(commands) / sizeof(commands[0])) {
        commands[commandCount++] = {name, shortName, function, description, haveArgs, category};
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
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_RED);    displayManager.printText("ERR");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::CMD]  '");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText(command);
    displayManager.setTextColor(0x7BEF);     displayManager.println("' not found");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);     displayManager.println("type help for commands");
    displayManager.printCommandScreen();
    resetCommand();
}


void CommandManager::resetCommand () {
    memset(command, 0, bufferSize);
    commandIndex = 0;
}

void CommandManager::setupCommands() {
    // ── System ────────────────────────────────────────────────────────────────
    registerCommand("help",        "hlp",    [](char* a) { Utils::printHelp(a); },                                          "Help [cmd]",                              true,  "System");
    registerCommand("man",         "mn",     [](char* a) { manPages.show(a); },                                               "Manual: man <command>",                   true,  "System");
    registerCommand("info",        "inf",    [](char* a) { espInfoPrinter.printESPInfo(); },                                 "Device info (IP, MAC, battery)",          false, "System");
    registerCommand("clear",       "clr",    [](char* a) { displayManager.tdeck_begin(); },                                  "Clear screen",                            false, "System");
    registerCommand("MATRIX",      "matrix", [](char* a) { displayManager.launchMatrixAnimation(); },                       "Matrix rain animation",                   false, "System");
    registerCommand("pwrsave",     "psv",    [](char* a) { PowerSaveManager::handleCommand(a); },                         "Power save: on/off/set/status",  true,  "System");
    // ── WiFi ──────────────────────────────────────────────────────────────────
    registerCommand("scanwifi",    "sw",     [](char* a) { wifiFunctions.scanWiFiNetworks(); },                              "Scan WiFi networks",                      false, "WiFi");
    registerCommand("connectwifi", "cw",     [](char* a) { wifiFunctions.connectToWiFiCommand(a); },                        "Connect to WiFi: cw <index>",             true,  "WiFi");
    registerCommand("clearwifi",   "clrw",   [](char* a) { wifiFunctions.clearAllWiFiCredentials(); },                      "Clear saved WiFi credentials",            false, "WiFi");
    registerCommand("wifimon",     "wm",     [](char* a) { wifiMonitor.start(a && *a ? atoi(a) : 0); },                    "WiFi monitor [ch 1-13, 0=hop]",           true,  "WiFi");
    registerCommand("deauth",      "da",     [](char* a) { deauthAttack.start(a); },                                        "Deauth: da <bssid> [ch] [client]",        true,  "WiFi");
    registerCommand("eviltwin",    "et",     [](char* a) { evilTwin.start(a); },                                            "Evil Twin AP + captive portal",           true,  "WiFi");
    registerCommand("hiddenssid",  "hs",     [](char* a) { hiddenSSID.start(a); },                                          "Uncover hidden SSID: hs <idx|bssid> [ch] [silent]", true,  "WiFi");
    registerCommand("macchanger",  "mc",     [](char* a) { MacChanger::getInstance().handleCommand(a); },                   "MAC spoof: mc on/off/random/set <mac>",              true,  "WiFi");
    registerCommand("wpasniff",    "ws",     [](char* a) { handshakeCapture.start(a); },                                      "WPA2 handshake: ws <idx|bssid> [ch]",                true,  "WiFi");
    // ── Network ───────────────────────────────────────────────────────────────
    registerCommand("netdiscover", "nd",     [](char* a) { networkScanner.networkDiscovery(); },                             "ARP scan local subnet",                   false, "Network");
    registerCommand("portscan",    "ps",     [](char* a) { networkScanner.networkPortScan(a); },                            "Port scan: ps <ip|#> <start> <end>",      true,  "Network");
    registerCommand("topscan",     "ts",     [](char* a) { networkScanner.topPortScan(a); },                                "Top 26 ports: ts <ip|#>",                 true,  "Network");
    registerCommand("ping",        "pg",     [](char* a) { networkScanner.pingHost(a); },                                   "Ping: pg <ip or hostname>",               true,  "Network");
    // ── Bluetooth ─────────────────────────────────────────────────────────────
    registerCommand("scanblue",    "sbl",    [](char* a) { bluetoothFunctions.scanBluetoothDevices(); },                    "BLE device scan",                         false, "Bluetooth");
    registerCommand("trackme",     "tm",     [](char* a) { trackMe.start(a && (strncmp(a,"s",1)==0||strncmp(a,"silent",6)==0)); }, "[EXP] Anti-tracking: tm [silent]",   true, "Bluetooth");
    // ── SD Card ───────────────────────────────────────────────────────────────
    registerCommand("sdinfo",      "sdi",    [](char* a) { sdCardManager.printInfo(); },                                    "SD card type and size",                   false, "SD Card");
    registerCommand("sdls",        "ls",     [](char* a) { sdCardManager.listDirectory(a && *a ? a : "/"); },               "List SD directory [path]",                true,  "SD Card");
    registerCommand("sdread",      "sdr",    [](char* a) { if (a&&*a) sdCardManager.readFile(a); else displayManager.println("Usage: sdread <path>"); displayManager.printCommandScreen(); }, "Read file from SD",  true,  "SD Card");
    registerCommand("sdrm",        "srm",    [](char* a) { if (a&&*a) sdCardManager.removeFile(a); else displayManager.println("Usage: sdrm <path>");  displayManager.printCommandScreen(); }, "Delete file from SD", true, "SD Card");
    // ── Diagnostics ───────────────────────────────────────────────────────────
    registerCommand("gpson",       "gon",    [](char* a) { runGpsOn(); },                                                   "GPS background task + live status",       false, "Diagnostics");
    registerCommand("gpsoff",      "gof",    [](char* a) { runGpsOff(); },                                                  "Stop GPS background task",                false, "Diagnostics");
    registerCommand("gpstest",     "gt",     [](char* a) { runGpsTest(); },                                                  "GPS coordinate test",                     false, "Diagnostics");
    registerCommand("spktest",     "st",     [](char* a) { runSpeakerTest(); },                                              "Speaker tone test",                       false, "Diagnostics");
    registerCommand("loratest",    "lt",     [](char* a) { runLoraTest(); },                                                 "LoRa SX1262 diagnostic",                  false, "Diagnostics");
}