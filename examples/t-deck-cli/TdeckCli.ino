#include <Wire.h>
#include "LGFX_T-Deck.h"
#include "utilities.h"
#include <WiFi.h>
#include <ctype.h>
#include <Pangodream_18650_CL.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <DigitalRainAnimation.hpp>
#include <ESP32Ping.h>

#define LILYGO_KB_SLAVE_ADDRESS 0x55
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define CONV_FACTOR 1.8
#define READS 20

#ifndef BOARD_HAS_PSRAM
#error "PSRAM not enabled. Please set PSRAM to OPI PSRAM in ArduinoIDE."
#endif

LGFX tft;
DigitalRainAnimation<LGFX> matrix_effect = DigitalRainAnimation<LGFX>();
Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

const uint16_t promptHeight = 30;
const uint16_t promptY = 0;
const uint16_t outputY = promptY + promptHeight + 8;

const uint16_t bufferSize = 64;
char command[bufferSize];
uint8_t commandIndex = 0;
int numberOfNetworks = 0;
int numberOfDevices = 0;
bool networkScanExecuted = false;
bool bluetoothScanExecuted = false;
bool connectedToNetwork = false;
int scanTime = 5;
BLEScan* pBLEScan;
String HOST_NAME = "T-DECk";

// Command structure with descriptions
typedef void (*CommandFunction)();
struct Command {
    const char* name;
    CommandFunction function;
    const char* description;
    bool haveArgs;
};

// Function prototypes
void executeCommand();
void registerCommand(const char* name, CommandFunction function, const char* description, bool haveArgs);
void printESPInfo();
void clearCommandOutput();
void printHelp();
void scanWiFiNetworks();
void connectToWiFiCommand();
void scanBluetoothDevices();
void launchMatrixAnimation();
void networkPortScan();
void networkDiscover();
bool startsWith(const char* str, const char* prefix);
String getValue(const String& data, char separator, int index);
void clearInputText();
void clearScreen();
void tdeck_begin();
void printCommandScreen();
char getKeyboardInput();
String readPassword();
float voltageToPercentage(float voltage);
String getBatteryChargeLevel(float volts);
void setupCommands();
void setDefaultTextSize();
void printDefaultTableHelpInstructions();

// Array to hold all commands
Command commands[50];
int commandCount = 0;

void setup(){
    Serial.begin(115200);
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    tft.init();
    tft.setRotation(1);
    tft.setBrightness(128);
    tft.invertDisplay(1);
    matrix_effect.init(&tft);
    clearScreen();
    tdeck_begin();
    setupCommands();
    // Check keyboard
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    if (Wire.read() == -1) {
        while (1) {
            Serial.println("LILYGO Keyboad not online .");
            delay(1000);
        }
    }
}

void loop() {
    char incoming = 0;
    while (true) {
        incoming = getKeyboardInput();
        if (incoming != 0) {
            Serial.print("keyValue: ");
            Serial.println(incoming);

            if (incoming == '\n' || incoming == '\r') {
                tft.setCursor(10, tft.getCursorY());
                executeCommand();
            } else if (incoming == '\b' && commandIndex > 0) {
                commandIndex--;
                command[commandIndex] = '\0';
                tft.fillRect(tft.getCursorX() - 7, tft.getCursorY(), SCREEN_WIDTH, promptHeight, TFT_BLACK);
                tft.setCursor(tft.getCursorX() - 7, tft.getCursorY());
            } else if (commandIndex < bufferSize - 1) {
                command[commandIndex] = incoming;
                commandIndex++;
                command[commandIndex] = '\0';
                tft.print(incoming);
            }
        }
    }
}

void executeCommand() {
    clearInputText();
    for (int i = 0; i < commandCount; i++) {
        if ((commands[i].haveArgs && startsWith(command, commands[i].name)) ||
            (!commands[i].haveArgs && strcmp(command, commands[i].name) == 0)) {
            commands[i].function();
            memset(command, 0, bufferSize);
            commandIndex = 0;
            return;
        }
    }

    tft.setCursor(10, tft.getCursorY());
    tft.println("Invalid command. Type 'help' to see available commands.");
    printCommandScreen();

    memset(command, 0, bufferSize);
    commandIndex = 0;
}
void registerCommand(const char* name, CommandFunction function, const char* description, bool haveArgs) {
    if (commandCount < sizeof(commands) / sizeof(commands[0])) {
        commands[commandCount++] = {name, function, description, haveArgs};
    }
}

void setupCommands() {
    registerCommand("info", printESPInfo, "Print T-DECK information",false);
    registerCommand("cls", tdeck_begin, "Clear the screen",false);
    registerCommand("scanwifi", scanWiFiNetworks, "Scan for available Wi-Fi networks",false);
    registerCommand("sw", scanWiFiNetworks, "Scan for available Wi-Fi networks",false);
    registerCommand("connectwifi", connectToWiFiCommand, "Connect to a Wi-Fi network",true);
    registerCommand("cw", connectToWiFiCommand, "Connect to a Wi-Fi network",true);
    registerCommand("scanblue", scanBluetoothDevices, "Scan for Bluetooth devices",false);
    registerCommand("sbl", scanBluetoothDevices, "Scan for Bluetooth devices",false);
    registerCommand("matrix", launchMatrixAnimation, "Launch matrix animation",false);
    registerCommand("MATRIX", launchMatrixAnimation, "Launch matrix animation",false);
    registerCommand("netdiscover", networkDiscover, "Discover devices on the network",false);
    registerCommand("nd", networkDiscover, "Discover devices on the network",false);
    registerCommand("portscan", networkPortScan, "Simple port scanning",true);
    registerCommand("ps", networkPortScan, "Simple port scanning",true);
    registerCommand("help", printHelp, "Print this help message",false);
}


void printESPInfo()
{
    tft.setCursor(10, tft.getCursorY());
    tft.println("T-DECK Info:");
    tft.setCursor(10, tft.getCursorY());
    tft.print("ESP32-S3 Chip ID: ");
    tft.println((uint32_t)ESP.getEfuseMac());
        // Print the IP address if connected to Wi-Fi
    if (WiFi.status() == WL_CONNECTED)
    {
        tft.setCursor(10, tft.getCursorY());
        tft.print("IP Address: ");
        tft.println(WiFi.localIP());
    }else {
        tft.setCursor(10, tft.getCursorY());
        tft.print("IP Address: ");
        tft.println("not connected to network");
    }
    tft.setCursor(10, tft.getCursorY());
    tft.print("MAC Address: ");
    tft.println(WiFi.macAddress());

    tft.setCursor(10, tft.getCursorY());
    tft.print("Average value from pin: ");
    tft.println(BL.pinRead());
    tft.setCursor(10, tft.getCursorY());
    tft.print("Volts: ");
    tft.println(BL.getBatteryVolts());
    tft.setCursor(10, tft.getCursorY());
    tft.print("Charge level: ");
    tft.println(getBatteryChargeLevel(BL.getBatteryVolts()));
    printCommandScreen();
    return;
}

void scanWiFiNetworks()
{
    numberOfNetworks = 0;
    int totalPages = 0;
    int currentPage = 0;
    char incomingKey = 0;
    bool updateInProgress = false;
    const int networksPerPage = 8;

    while (true)
    {
        if (updateInProgress || numberOfNetworks == 0 )
        {
            tft.setCursor(10, tft.getCursorY());
            tft.println("Updating Wi-Fi networks...");
            tft.println();
            tft.setCursor(10, tft.getCursorY());
            tft.println("Press 'q' to exit");
            tft.println();
            updateInProgress = false;
            numberOfNetworks = WiFi.scanNetworks();
            totalPages = (numberOfNetworks + networksPerPage - 1) / networksPerPage;
            currentPage = 0;
            networkScanExecuted = true;
        }

        clearScreen();

        tft.setCursor(10, outputY);
        setDefaultTextSize();
        tft.print("Page ");
        tft.print(currentPage + 1);
        tft.print("/");
        tft.println(totalPages);
        tft.println("---------------------------------------------");
        tft.println("| Index |      SSID     | Signal | Password |");
        tft.println("---------------------------------------------");

        int startIndex = currentPage * networksPerPage;
        int endIndex = min(startIndex + networksPerPage, numberOfNetworks);

        for (int i = startIndex; i < endIndex; i++)
        {
            setDefaultTextSize();
            tft.print("|   ");
            tft.print(i);
            tft.print("   | ");
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 15)
            {
                ssid = ssid.substring(0, 12) + "...";
            }
            tft.print(ssid);
            for (int j = 0; j < 15 - ssid.length(); j++)
            {
                tft.print(" ");
            }
            tft.print(" |  ");
            tft.print(WiFi.RSSI(i));
            tft.print("   |  ");
            tft.print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "No" : "Yes");
            tft.println("  |");
        }

        tft.println("---------------------------------------------");
        printDefaultTableHelpInstructions();

        while (true)
        {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L')
            {
                currentPage++;
                if (currentPage >= totalPages)
                {
                    currentPage = totalPages - 1;
                }
                break;
            }
            else if (incomingKey == 'a' || incomingKey == 'A')
            {
                currentPage--;
                if (currentPage < 0)
                {
                    currentPage = 0;
                }
                break;
            }
            else if (incomingKey == 'q' || incomingKey == 'Q')
            {
                printCommandScreen();
                return;
            }
            else if (incomingKey == 'u' || incomingKey == 'U')
            {
                updateInProgress = true;
                break;
            }
        }
    }
}

void connectToWiFiCommand(){
    if (!networkScanExecuted)
    {
        tft.println("Please execute scanwifi command first");
        return;
    }

    int networkIndex;
    if (sscanf(command, "connectwifi %d", &networkIndex) != 1 && sscanf(command, "cw %d", &networkIndex) != 1)
    {
        tft.println("Invalid command format. Please use 'connectwifi <networkIndex>' or 'cw <networkIndex>'.");
        return;
    }

    if (networkIndex < 0 || networkIndex > numberOfNetworks)
    {
        tft.println("Invalid network index");
        return;
    }

    String ssid = WiFi.SSID(networkIndex);
    bool requiresPassword = WiFi.encryptionType(networkIndex) != WIFI_AUTH_OPEN;

    if (requiresPassword)
    {
        tft.setCursor(10, tft.getCursorY());
        tft.print("Enter password for network ");
        tft.println(ssid);
        String password = readPassword();
        if (password.length() ==1 && password[0] == 'q'){
            printCommandScreen();
            return;
        }
            

        tft.print("Connecting to Wi-Fi ");
        tft.println(ssid);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(HOST_NAME.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    else
    {
        tft.print("Connecting to Wi-Fi ");
        tft.println(ssid);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(HOST_NAME.c_str());
        WiFi.begin(ssid.c_str());
    }
    unsigned long startTime = millis();

    while (WiFi.status() != WL_CONNECTED)
            {
                delay(1000);
                tft.print(".");
                unsigned long currentTime = millis();
                unsigned long elapsedTime = currentTime - startTime;

                if (elapsedTime > 15000)
                {
                    tft.setTextColor(TFT_RED);
                    tft.println("\nConnection timed out. Please check the password and try again.");
                    WiFi.disconnect();
                    delay(2000);
                    tdeck_begin();
                    return;
                }
            }
    tft.println();
    tft.println("Wi-Fi connected");
    tft.print("IP address: ");
    tft.println(WiFi.localIP());
    connectedToNetwork = true;
    tft.println("returning to cli ...");
    delay(3000);
    tdeck_begin();
    return;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    }
};

void scanBluetoothDevices() {
    const int devicesPerPage = 6;
    int currentPage = 0;
    int totalPages = 0;
    char incomingKey = 0;
    bool updateInProgress = false;
    numberOfDevices = 0;

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    BLEScanResults foundDevices;

    while (true) {
        if (updateInProgress || numberOfDevices == 0) {
            tft.setCursor(10, tft.getCursorY());
            tft.println("Scanning Bluetooth Devices:");
            tft.println();
            tft.setCursor(10, tft.getCursorY());
            tft.println("Press 'q' to exit");
            tft.println();
            updateInProgress = false;
            foundDevices = pBLEScan->start(scanTime, false);
            numberOfDevices = foundDevices.getCount();
            totalPages = (numberOfDevices + devicesPerPage - 1) / devicesPerPage;
            bluetoothScanExecuted = true;
            currentPage = 0;
        }

        clearScreen();
        tft.setCursor(10, outputY);
        tft.setTextSize(1);
        tft.print("Page ");
        tft.print(currentPage + 1);
        tft.print("/");
        tft.println(totalPages);
        tft.println("-----------------------------------------------------");
        tft.println("| Index |       Address       | RSSI  |    Name    |");
        tft.println("-----------------------------------------------------");

        int startIndex = currentPage * devicesPerPage;
        int endIndex = min(startIndex + devicesPerPage, numberOfDevices);

        for (int i = startIndex; i < endIndex; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);
            tft.print("|   ");
            tft.print(i);
            tft.print("   | ");
            tft.print(device.getAddress().toString().c_str());
            for (int j = 0; j < 19 - device.getAddress().toString().length(); j++) {
                tft.print(" ");
            }
            tft.print(" |  ");
            tft.print(device.getRSSI());
            tft.print("   | ");

            String name = device.getName().c_str();
            if (name.length() > 7) {
                name = name.substring(0, 7);
                name += "...";
            }

            tft.print(name);
            for (int j = 0; j < 12 - name.length(); j++) {
                tft.print(" ");
            }
            tft.println("|");
        }

        tft.println("-----------------------------------------------------");
        printDefaultTableHelpInstructions();

        while (true) {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L') {
                currentPage++;
                if (currentPage >= totalPages) {
                    currentPage = totalPages - 1;
                }
                break;
            } else if (incomingKey == 'a' || incomingKey == 'A') {
                currentPage--;
                if (currentPage < 0) {
                    currentPage = 0;
                }
                break;
            } else if (incomingKey == 'q' || incomingKey == 'Q') {
                pBLEScan->clearResults();
                printCommandScreen();
                return;
            } else if (incomingKey == 'u' || incomingKey == 'U') {
                updateInProgress = true;
                break;
            }
        }
    }
}

void launchMatrixAnimation(){
    matrix_effect.setTextAnimMode(AnimMode::SHOWCASE, "\nWake Up, Neo...    \nThe Matrix has you.    \nFollow     \nthe white rabbit.     \nKnock, knock, Neo.                 ");
    while (true){
    char incomingKey = getKeyboardInput();
    if (incomingKey == 'q' || incomingKey == 'Q')
    {
        tdeck_begin();
        return;
    }
    else {
        matrix_effect.loop();
        }
    }
}

void networkDiscover(){
    if (!connectedToNetwork)
    {
        tft.setCursor(10, tft.getCursorY());
        tft.println("Please connect to wifi network first");
        printCommandScreen();
        return;
    }
    else {
        tft.setCursor(10, tft.getCursorY());
        tft.println("Devices discovery started ...");
        IPAddress gatewayIP = WiFi.gatewayIP();
        IPAddress subnetMask = WiFi.subnetMask();

        tft.setCursor(10, tft.getCursorY());
        tft.print("gatewayIP ip ");
        tft.println(gatewayIP);

        Serial.print("gatewayIP ip ");
        Serial.println(gatewayIP);

        tft.setCursor(10, tft.getCursorY());
        tft.print("subnetMask ip ");
        tft.println(subnetMask);

        Serial.print("subnetMask ip ");
        Serial.println(subnetMask);
        pingScan(gatewayIP, subnetMask);
    }
}

void pingScan(const IPAddress& gatewayIP, const IPAddress& subnetMask) {
   IPAddress targetIP;
  uint8_t gatewayLastPart = gatewayIP[3];
  bool stopScan = false;

  for (int i = 1; i <= gatewayLastPart; ++i) {
    targetIP = gatewayIP;
    targetIP[3] = i;

    Serial.print("targetIP ip ");
    Serial.println(targetIP);

    if (Ping.ping(targetIP)) {
      tft.setCursor(10, tft.getCursorY());
      tft.print("Found device at IP: ");
      tft.println(targetIP);
    }

      char incomingKey = getKeyboardInput();
      if (incomingKey == 'q' || incomingKey == 'Q') {
        stopScan = true;
        break;
      }
    
  }

  if (stopScan) {
    printCommandScreen();
    return;
  }
}

void networkPortScan(){
    // Extract IP address, start port, and end port from the command
    String ipAddress = getValue(command, ' ', 1);
    String startPortStr = getValue(command, ' ', 2);
    String endPortStr = getValue(command, ' ', 3);
    IPAddress targetIp;
    

    // Check if IP address, start port, and end port are provided
    if (ipAddress.isEmpty() || startPortStr.isEmpty() || endPortStr.isEmpty())
    {
        tft.setCursor(10, tft.getCursorY());
        tft.println("Invalid command usage. Usage: portscan [ip address] [start port] [end port]");
        printCommandScreen();
    }        
    else if (!targetIp.fromString(ipAddress))
        {
            tft.setCursor(10, tft.getCursorY());
            tft.println("Invalid IP address.");
            printCommandScreen();
            return;
        }

        else if (!Ping.ping(targetIp))
        {
            tft.setCursor(10, tft.getCursorY());
            tft.setTextColor(TFT_RED);
            tft.println("Target IP address is unreachable.");
            printCommandScreen();
            return;
        }
        else
        {
            int startPort = startPortStr.toInt();
            int endPort = endPortStr.toInt();
            Serial.print("port scan on ip:");
            Serial.println(ipAddress);
            Serial.print("port scan on startPort:");
            Serial.println(startPort);
            Serial.print("port scan on endPort:");
            Serial.println(endPort);
            performPortScan(targetIp, startPort, endPort);
        }
        return;
}
void performPortScan(const IPAddress& targetIP, int startPort, int endPort)
{
    const int portsPerPage = 5;
    int currentPage = 0;
    char incomingKey = 0;

    while (true)
    {
        clearScreen();
        tft.setCursor(10, outputY);
        setDefaultTextSize();
        tft.print("Port Scan - IP: ");
        tft.println(targetIP.toString());
        tft.setCursor(10, tft.getCursorY());
        tft.print("Range: ");
        tft.print(startPort);
        tft.print(" - ");
        tft.println(endPort);
        tft.println("--------------------");
        tft.println("| Port   |  Status |");
        tft.println("--------------------");

        for (int port = startPort; port <= endPort; port++)
        {
            String status = performPortCheck(targetIP, port) ? "Open" : "Closed";
            if(strcmp(status.c_str(),"Open") == 0){
                setDefaultTextSize();
                tft.print("| ");
                tft.print(port);
                tft.setCursor(tft.getCursorX(), tft.getCursorY());
                tft.print("    |  ");
                
                tft.print(status);
                for (int j = 2; j < 8 - status.length(); j++)
                {
                    tft.print(" ");
                }
                tft.println(" |");
                tft.println();

            }
        }
        tft.println("--------------------");

        while (true)
        {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L')
            {
                currentPage++;
                if (currentPage >= (endPort + 1) / portsPerPage)
                {
                    currentPage = (endPort + 1) / portsPerPage - 1;
                }
                break;
            }
            else if (incomingKey == 'a' || incomingKey == 'A')
            {
                currentPage--;
                if (currentPage < 0)
                {
                    currentPage = 0;
                }
                break;
            }
            else if (incomingKey == 'q' || incomingKey == 'Q')
            {
                printCommandScreen();
                return;
            }
        }
    }
}
bool performPortCheck(const IPAddress& ip, int port)
{
  WiFiClient client;

  if (client.connect(ip, port))
  {
    client.stop();
    return true;
  }
  else
  {
    return false;
  }
}

void printHelp() {
    tft.setCursor(10, tft.getCursorY());
    tft.println("Available commands:");
    for (int i = 0; i < commandCount; ++i) {
        tft.setCursor(10, tft.getCursorY());
        tft.print(commands[i].name);
        tft.print(" - ");
        tft.println(commands[i].description);
    }
    printCommandScreen();
}


// Helper functions

bool startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

String getValue(const String& data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }

    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void clearInputText()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.println("CMD> ");
}

void clearScreen()
{
    tft.fillRect(0, promptY + promptHeight, SCREEN_WIDTH, SCREEN_HEIGHT - (promptY + promptHeight), TFT_BLACK);
}

//begin t-deck cli by clear the screen and print command line 
void tdeck_begin(){
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    // Clear the command buffer and reset the index
    memset(command, 0, bufferSize);
    commandIndex = 0;
    printFirstCommandScreen();
}

char getKeyboardInput(){
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    return Wire.available() > 0 ? Wire.read() : 0;
}

String readPassword() {
    String password = "";
    char input = '\0';

    while (true) {
        input = getKeyboardInput();
        if (input != (char)0x00)
            {
                if (input == '\n' || input == '\r')
                {
                    break;
                }
                else if (input == '\b' && password.length() > 0)
                {
                    password.remove(password.length() - 1);  // Remove the last character from the password
                    tft.fillRect(tft.getCursorX() - 6, tft.getCursorY(), SCREEN_WIDTH, promptHeight, TFT_BLACK);
                    tft.setCursor(tft.getCursorX() - 6, tft.getCursorY());
                }
                else if (isAlphaNumeric(input) && password.length() < 100)
                {
                    password += input;
                    tft.print(input);
                }
            }
    }
    tft.println();
    return password;
}

float voltageToPercentage(float voltage) {
    float percentage = (voltage - 3.0) / (4.2 - 3.0) * 100;
    return percentage < 0 ? 0 : (percentage > 100 ? 100 : percentage);
}

String getBatteryChargeLevel(float volts) {
    float percentage = voltageToPercentage(volts);
    return String(percentage, 1) + "%";
}

void printCommandScreen(){
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    memset(command, 0, bufferSize);
    commandIndex = 0;
    tft.println();
    tft.setCursor(10, tft.getCursorY());
    tft.print("CMD> ");
    tft.print(command);
}

void printFirstCommandScreen(){
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.print("CMD> ");
    tft.print(command);
}
void setDefaultTextSize(){
    tft.setTextSize(1.2,1.2);
}
void printDefaultTableHelpInstructions(){
        setDefaultTextSize();
        tft.setTextColor(TFT_WHITE);
        tft.print("-------");
        tft.setTextColor(TFT_GREEN);
        tft.print("a=prev");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("l=next");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("q=quit");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("u=update");
        tft.setTextColor(TFT_WHITE);
        tft.println("------");
}