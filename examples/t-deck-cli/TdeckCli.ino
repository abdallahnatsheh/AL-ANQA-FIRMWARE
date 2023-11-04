#include <Wire.h>
#include <TFT_eSPI.h>
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
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define CONV_FACTOR 1.8
#define READS 20
String HOST_NAME = "T-DECk";


TFT_eSPI tft = TFT_eSPI();
DigitalRainAnimation<TFT_eSPI> matrix_effect = DigitalRainAnimation<TFT_eSPI>();
Pangodream_18650_CL BL(BOARD_BAT_ADC, CONV_FACTOR, READS);

const uint16_t promptHeight = 30;
const uint16_t promptY = 0;
const uint16_t outputY = promptY + promptHeight + 8;

// Command buffer
const uint16_t bufferSize = 64;
char command[bufferSize];
uint8_t commandIndex = 0;
int numberOfNetworks = 0;
int numberOfDevices = 0;
bool networkScanExecuted = false; // Flag to track if network scan has been executed
bool bluetoothScanExecuted = false; // Flag to track if bluetooth scan has been executed
bool connectedToNetwork = false; // Flag to track if device connected to wifi network
int scanTime = 5; // In seconds
BLEScan* pBLEScan;

void setup()
{
    Serial.begin(115200);
    Serial.println("T-Deck Keyboard Master");

    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    tft.begin();
    tft.setRotation(1);
    tft.invertDisplay(0);
    matrix_effect.init(&tft);
    clearScreen();

    // Display command prompt
    tdeck_begin();
        // Check keyboard
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    if (Wire.read() == -1) {
        while (1) {
            Serial.println("LILYGO Keyboad not online .");
            delay(1000);
        }
    }
}

void loop()
{
    // Read key value from T-Keyboard
    char incoming = 0;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available() > 0)
    {
        incoming = Wire.read();
        if (incoming != (char)0x00)
        {
            Serial.print("keyValue: ");
            Serial.println(incoming);

            if (incoming == '\n' || incoming == '\r')
            {
                // Execute command when Enter key is pressed
                tft.setCursor(10, tft.getCursorY());
                executeCommand();
            }
            else if (incoming == '\b' && commandIndex > 0)
            {
                // Handle backspace by deleting previous character
                commandIndex--;
                command[commandIndex] = '\0';
                tft.fillRect(tft.getCursorX() - 6, tft.getCursorY(), SCREEN_WIDTH, promptHeight, TFT_BLACK);
                tft.setCursor(tft.getCursorX() - 6, tft.getCursorY());
                //clearCommandOutput();
                //printCommandScreen();
            }
            else if (commandIndex < bufferSize - 1)
            {
                // Add incoming character to command buffer
                command[commandIndex] = incoming;
                commandIndex++;
                command[commandIndex] = '\0';
                    
                // Display the character on the TFT display
                tft.print(incoming);
            }
        }
    }
}

void executeCommand()
{
    // Process and execute the command
    // Clear the command output area
    clearInputText();

    if (strcmp(command, "info") == 0)
    {
        printESPInfo();
    }
    else if (strcmp(command, "cls") == 0)
    {
        tdeck_begin();
    }
    else if (strcmp(command, "scanwifi") == 0 || strcmp(command, "sw") == 0)
    {
        scanWiFiNetworks();
    }
    else if (startsWith(command, "connectwifi") == 1 || startsWith(command, "cw") == 1)
    {
        connectToWiFiCommand();
    }
    else if (strcmp(command, "scanblue") == 0 || strcmp(command, "sbl") == 0)
    {
        scanBluetoothDevices();
    }
    else if (strcmp(command, "matrix") == 0 || strcmp(command, "MATRIX") == 0){
        launchMatrixAnimation();
    }
    else if (startsWith(command, "portscan") == 1 || startsWith(command, "ps") == 1)
    {
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

            // Check if the target IP address is reachable
            else if (!Ping.ping(targetIp))
            {
                tft.setCursor(10, tft.getCursorY());
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
        }
    else if (startsWith(command, "netdiscover") == 1 || startsWith(command, "nd") == 1)
    {
        networkDiscover();
    }

     
    else if (strcmp(command, "help") == 0) {
        printHelp();
    }
    else {
        tft.setCursor(10, tft.getCursorY());
        tft.println("Invalid command. Type 'help' to see available commands.");
        printCommandScreen();
    }


    // Clear the command buffer and reset the index
    memset(command, 0, bufferSize);
    commandIndex = 0;
}
String getValue(const String &data, char separator, int index)
{
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++)
    {
        if (data.charAt(i) == separator || i == maxIndex)
        {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }

    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void clearCommandOutput()
{
    // Clear the command output area
    tft.fillRect(0, outputY, SCREEN_WIDTH, SCREEN_HEIGHT - outputY, TFT_BLACK);
}
void printHelp() {
  tft.setCursor(10, tft.getCursorY());
  tft.println("Available commands:");
  tft.setCursor(10, tft.getCursorY());
  tft.println("info        - Print T-DECK information");
  tft.setCursor(10, tft.getCursorY());
  tft.println("cls         - Clear the screen");
  tft.setCursor(10, tft.getCursorY());
  tft.println("scanwifi/sw - Scan for available Wi-Fi networks");
  tft.setCursor(10, tft.getCursorY());
  tft.println("connectwifi/cw <networkIndex> - Connect to a Wi-Fi network");
  tft.setCursor(10, tft.getCursorY());
  tft.println("scanblue/sbl - Scan for Bluetooth devices");
  tft.setCursor(10, tft.getCursorY());
  tft.println("matrix/MATRIX - Launch matrix animation");
  tft.setCursor(10, tft.getCursorY());
  tft.println("netdiscover/nd - Discover devices on the network");
  tft.setCursor(10, tft.getCursorY());
  tft.println("help        - Print this help message");
  printCommandScreen();
  return;
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
    // Add more information as needed
    printCommandScreen();
    return;
}
float voltageToPercentage(float voltage) {
  float minVoltage = 2.95;  // Minimum voltage (corresponding to 0%)
  float maxVoltage = 4.45;  // Maximum voltage (corresponding to 100%)

  // Calculate battery level percentage using linear interpolation
  float percentage = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100.0;

  // Ensure the percentage value is within the valid range (0 to 100)
  if (percentage < 0.0) {
    percentage = 0.0;
  } else if (percentage > 100.0) {
    percentage = 100.0;
  }

  return percentage;
}
String getBatteryChargeLevel(float volts){
    if(volts > 4.90)
        return String("charging");
    else if (volts >= 4.45 && volts < 4.90)
        return String("100%");
    else {
        int chargeLevel = voltageToPercentage(volts);
        Serial.print("charge level! ");
        Serial.println(String(String(chargeLevel) + "%"));
        return String(chargeLevel) + "%";
    }
}

void scanWiFiNetworks()
{
    numberOfNetworks = 0;
    int totalPages = 0;
    int currentPage = 0;
    char incomingKey = 0;
    bool updateInProgress = false;
    const int networksPerPage = 5; // Number of networks to display per page

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
            networkScanExecuted = true; // Set the flag to indicate network scan has been executed
        }

        clearScreen();

        tft.setCursor(10, outputY);
        tft.setTextSize(1); // Set a smaller font size
        tft.print("Page ");
        tft.print(currentPage + 1);
        tft.print("/");
        tft.println(totalPages);
        tft.println("----------------------------------------------------");
        tft.println("| Index |         SSID          | Signal | Password |");
        tft.println("----------------------------------------------------");

        int startIndex = currentPage * networksPerPage;
        int endIndex = min(startIndex + networksPerPage, numberOfNetworks);

        for (int i = startIndex; i < endIndex; i++)
        {
            tft.setTextSize(1); // Set a smaller font size
            tft.print("|   ");
            tft.print(i);
            tft.print("   | ");
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 20)
            {
                ssid = ssid.substring(0, 17) + "...";
            }
            tft.print(ssid);
            for (int j = 0; j < 21 - ssid.length(); j++)
            {
                tft.print(" ");
            }
            tft.print(" |  ");
            tft.print(WiFi.RSSI(i));
            tft.print("   |    ");
            tft.print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "No" : "Yes");
            tft.println("    |");
        }

        tft.println("----------------------------------------------------");

        // Wait for user input to navigate through pages or update the networks
        while (true)
        {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L')
            {
                currentPage++;
                if (currentPage >= totalPages)
                {
                    currentPage = totalPages - 1; // Set currentPage to the last page
                }
                break;
            }
            else if (incomingKey == 'a' || incomingKey == 'A')
            {
                currentPage--;
                if (currentPage < 0)
                {
                    currentPage = 0; // Set currentPage to the first page
                }
                break;
            }
            else if (incomingKey == 'q' || incomingKey == 'Q')
            {
                //tdeck_begin();
                printCommandScreen();
                return;
            }
            else if (incomingKey == 'u' || incomingKey == 'U')
            {
                // Update the scanned networks
                updateInProgress = true;
                break;
            }
        }
    }
}

char getKeyboardInput()
{
    char incoming = 0;
    Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
    while (Wire.available() > 0)
    {
        incoming = Wire.read();
        if (incoming != (char)0x00)
        {
            return incoming;
        }
    }
    
}

bool startsWith(const char* str, const char* prefix)
{
    while (*prefix)
    {
        if (*str != *prefix)
            return false;
        str++;
        prefix++;
    }
    return true;
}
void printFirstCommandScreen(){
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // Set a smaller font size
    tft.print("CMD> ");
    tft.print(command);
}
void printCommandScreen(){
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // Set a smaller font size
    memset(command, 0, bufferSize);
    commandIndex = 0;
    tft.println();
    tft.setCursor(10, tft.getCursorY());
    tft.print("CMD> ");
    tft.print(command);
}

//begin t-deck cli by clear the screen and print command line 
void tdeck_begin(){
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    // Clear the command buffer and reset the index
    memset(command, 0, bufferSize);
    commandIndex = 0;
    printFirstCommandScreen();
}

//clear the screen under the main title t-deck cli v0.1
void clearScreen()
{
    tft.fillRect(0, promptY + promptHeight, SCREEN_WIDTH, SCREEN_HEIGHT - (promptY + promptHeight), TFT_BLACK);
}

void connectToWiFiCommand()
{
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
        WiFi.setHostname(HOST_NAME.c_str()); //define hostname   
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    else
    {
        tft.print("Connecting to Wi-Fi ");
        tft.println(ssid);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname(HOST_NAME.c_str()); //define hostname   
        WiFi.begin(ssid.c_str());
    }
    unsigned long startTime = millis(); // Get the start time

    while (WiFi.status() != WL_CONNECTED)
            {
                delay(1000);
                tft.print(".");
                unsigned long currentTime = millis(); // Get the current time
                unsigned long elapsedTime = currentTime - startTime; // Calculate the elapsed time

                if (elapsedTime > 15000) // Timeout after 15 seconds
                {
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

String readPassword()
{
    String password;
    char character;

    while (true)
    {
        Wire.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1);
        while (Wire.available() > 0)
        {
            character = Wire.read();
            if (character != (char)0x00)
            {
                if (character == '\n' || character == '\r')
                {
                    break;
                }
                else if (character == '\b' && password.length() > 0)
                {
                    password.remove(password.length() - 1);  // Remove the last character from the password
                    tft.fillRect(tft.getCursorX() - 6, tft.getCursorY(), SCREEN_WIDTH, promptHeight, TFT_BLACK);
                    tft.setCursor(tft.getCursorX() - 6, tft.getCursorY());
                }
                else if (isAlphaNumeric(character) && password.length() < 100)
                {
                    password += character;
                    tft.print(character);
                }
            }
        }

        if (character == '\n' || character == '\r')
        {
            break;
        }
    }
    tft.println();
    return password;
}

void clearInputText()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // Set a smaller font size
    tft.println("CMD> ");
}
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    }
};

void scanBluetoothDevices() {
    const int devicesPerPage = 5; // Number of devices to display per page
    int currentPage = 0;
    int totalPages = 0;
    char incomingKey = 0;
    bool updateInProgress = false;
    numberOfDevices = 0;

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan(); // Create new scan
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); // Active scan uses more power, but gets results faster
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // Less or equal setInterval value
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
        tft.setTextSize(1); // Set a smaller font size
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
                name = name.substring(0, 7); // Truncate name to 7 characters
                name += "..."; // Add ellipsis to indicate truncation
            }

            tft.print(name);
            for (int j = 0; j < 12 - name.length(); j++) {
                tft.print(" ");
            }
            tft.println(" |");
        }

        tft.println("-----------------------------------------------------");

        while (true) {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L') {
                currentPage++;
                if (currentPage >= totalPages) {
                    currentPage = totalPages - 1; // Set currentPage to the last page
                }
                break;
            } else if (incomingKey == 'a' || incomingKey == 'A') {
                currentPage--;
                if (currentPage < 0) {
                    currentPage = 0; // Set currentPage to the first page
                }
                break;
            } else if (incomingKey == 'q' || incomingKey == 'Q') {
                pBLEScan->clearResults(); // Delete results from BLEScan buffer to release memory
                //tdeck_begin();
                printCommandScreen();
                return;
            } else if (incomingKey == 'u' || incomingKey == 'U') {
                // Update the scanned networks
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
                // Get local IP and subnet mask
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
        // Perform ping scan
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

    // Check if 'q' key is pressed
      char incomingKey = getKeyboardInput();
      if (incomingKey == 'q' || incomingKey == 'Q') {
        stopScan = true;
        break;
      }
    
  }

  if (stopScan) {
    //tdeck_begin();
    printCommandScreen();
    return;
  }
}
bool performPortCheck(const IPAddress& ip, int port)
{
  WiFiClient client;

  if (client.connect(ip, port))
  {
    client.stop(); // Close the connection
    return true; // Port is open
  }
  else
  {
    return false; // Port is closed
  }
}

void performPortScan(const IPAddress& targetIP, int startPort, int endPort)
{
    const int portsPerPage = 5; // Number of ports to display per page
    int currentPage = 0;
    char incomingKey = 0;

    while (true)
    {
        clearScreen();
        tft.setCursor(10, outputY);
        tft.setTextSize(1); // Set a smaller font size
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

        //int startPortIndex = currentPage * portsPerPage;
        //int endPortIndex = min(startPortIndex + portsPerPage, endPort + 1);
        for (int port = startPort; port <= endPort; port++)
        {
            String status = performPortCheck(targetIP, port) ? "Open" : "Closed";
            if(strcmp(status.c_str(),"Open") == 0){
                tft.setTextSize(1); // Set a smaller font size
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

        // Wait for user input to navigate through pages or exit
        while (true)
        {
            incomingKey = getKeyboardInput();
            if (incomingKey == 'l' || incomingKey == 'L')
            {
                currentPage++;
                if (currentPage >= (endPort + 1) / portsPerPage)
                {
                    currentPage = (endPort + 1) / portsPerPage - 1; // Set currentPage to the last page
                }
                break;
            }
            else if (incomingKey == 'a' || incomingKey == 'A')
            {
                currentPage--;
                if (currentPage < 0)
                {
                    currentPage = 0; // Set currentPage to the first page
                }
                break;
            }
            else if (incomingKey == 'q' || incomingKey == 'Q')
            {
                printCommandScreen();
                return; // Exit the port scan
            }
        }
    }
}
bool isValidIPAddress(const char* text)
{
  IPAddress ipAddress;
  if (WiFi.hostByName(text, ipAddress))
  {
    return true;
  }
  return false;
}