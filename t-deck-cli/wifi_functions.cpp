#include <Preferences.h>
#include "wifi_functions.h"
#include "input_handling.h"
#include <ESP32Ping.h>
#include "utils.h"

extern InputHandling inputHandler;
extern Utils utils;
Preferences preferences;

WiFiFunctions::WiFiFunctions(DisplayManager& displayManager)
    : displayManager(displayManager) {
    preferences.begin("wifi", false); // Initialize preferences with a namespace "wifi"
    }

void WiFiFunctions::connectToWiFiCommand(char* args) {
    if (!networkScanExecuted) {
        displayManager.println("Please execute scanwifi command first");
        return;
    }

    int networkIndex;
    if (sscanf(args, "%d", &networkIndex) != 1 && sscanf(args, "%d", &networkIndex) != 1) {
        displayManager.println("Invalid command format. Please use 'connectwifi <networkIndex>' or 'cw <networkIndex>'.");
        return;
    }

    if (networkIndex < 0 || networkIndex > numberOfNetworks) {
        displayManager.println("Invalid network index");
        return;
    }

    String ssid = WiFi.SSID(networkIndex);
    bool requiresPassword = WiFi.encryptionType(networkIndex) != WIFI_AUTH_OPEN;
    String password;

    if (requiresPassword) {
        password = getWiFiPassword(ssid);
        Serial.println("Retrieved password: " + password);
        if (password.isEmpty()) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.printText("Enter password for network ");
            displayManager.println(ssid);
            password = readPassword();
            Serial.println("Entered password: " + password);
            if (password.length() == 1 && password[0] == 'q') {
                displayManager.printCommandScreen();
                return;
            }else if (password.length() < 8)
            {
                displayManager.setCursor(10, displayManager.getCursorY());
                displayManager.println("Password must be at least 8 characters long");
                delay(2000);
                displayManager.tdeck_begin();
                return;
            }
            storeWiFiCredentials(ssid, password);
            Serial.println("Password saved: " + password);
        }
    }

    displayManager.printText("Connecting to Wi-Fi ");
    displayManager.println(ssid);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(HOST_NAME.c_str());
    WiFi.begin(ssid.c_str(), requiresPassword ? password.c_str() : nullptr);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        displayManager.printText(".");
        unsigned long currentTime = millis();
        unsigned long elapsedTime = currentTime - startTime;
        if (elapsedTime > 15000) {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("\nConnection timed out. Please check the password and try again.");
            WiFi.disconnect();
            delay(2000);
            displayManager.tdeck_begin();
            return;
        }
    }
    displayManager.println();
    displayManager.println("Wi-Fi connected");
    displayManager.printText("IP address: ");
    displayManager.println(WiFi.localIP().toString());
    connectedToNetwork = true;
    displayManager.println("returning to cli ...");
    delay(3000);
    displayManager.tdeck_begin();
    return;
}

void WiFiFunctions::scanWiFiNetworks()
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
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Updating Wi-Fi networks...");
            displayManager.println();
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Press 'q' to exit");
            displayManager.println();
            updateInProgress = false;
            numberOfNetworks = WiFi.scanNetworks();
            totalPages = (numberOfNetworks + networksPerPage - 1) / networksPerPage;
            currentPage = 0;
            networkScanExecuted = true;
        }

        displayManager.clearScreen();

        displayManager.setCursor(10, outputY);
        displayManager.setDefaultTextSize();
        displayManager.printText("Page ");
        displayManager.printText(currentPage + 1);
        displayManager.printText("/");
        displayManager.println(totalPages);
        displayManager.println("---------------------------------------------");
        displayManager.println("| Index |      SSID     | Signal | Password |");
        displayManager.println("---------------------------------------------");

        int startIndex = currentPage * networksPerPage;
        int endIndex = min(startIndex + networksPerPage, numberOfNetworks);

        for (int i = startIndex; i < endIndex; i++)
        {
            displayManager.setDefaultTextSize();
            displayManager.printText("|   ");
            displayManager.printText(i);
            displayManager.printText("   | ");
            String ssid = WiFi.SSID(i);
            if (ssid.length() > 15)
            {
                ssid = ssid.substring(0, 12) + "...";
            }
            std::string ssid_std = ssid.c_str();
            displayManager.printText(ssid_std);
            for (int j = 0; j < 15 - ssid.length(); j++)
            {
                displayManager.printText(" ");
            }
            displayManager.printText(" |  ");
            displayManager.printText(WiFi.RSSI(i));
            displayManager.printText("   |  ");
            displayManager.printText(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "No" : "Yes");
            displayManager.println("  |");
        }

        displayManager.println("---------------------------------------------");
        displayManager.printDefaultTableHelpInstructions();

        while (true)
        {
            incomingKey = inputHandler.getKeyboardInput();
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
                displayManager.printCommandScreen();
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
String  WiFiFunctions::readPassword() {
    String password = "";
    char input = '\0';

    while (true) {
        input = inputHandler.getKeyboardInput();
        if (input != (char)0x00)
            {
                if (input == '\n' || input == '\r')
                {
                    break;
                }
                else if (input == '\b' && password.length() > 0)
                {
                    password.remove(password.length() - 1);  // Remove the last character from the password
                    displayManager.backspaceChar();                    
                }
                else if (isAlphaNumeric(input) && password.length() < 100)
                {
                    password += input;
                    displayManager.printText(input);
                }
            }
    }
    displayManager.println();
    return password;
}

void WiFiFunctions::storeWiFiCredentials(const String& ssid, const String& password) {
    if (!preferences.begin("wifi", false)) {
        Serial.println("Failed to open preferences for writing");
        return;
    }

    Serial.println("Storing SSID: " + ssid);
    Serial.println("Password: " + password);

    preferences.putString(ssid.c_str(), password);

    // Confirm storage
    String storedPassword = preferences.getString(ssid.c_str(), "");
    if (storedPassword == password) {
        Serial.println("Password stored successfully.");
    } else {
        Serial.println("Failed to store password.");
    }

    preferences.end();
}

String WiFiFunctions::getWiFiPassword(const String& ssid) {
    if (!preferences.begin("wifi", true)) {
        Serial.println("Failed to open preferences for reading");
        return "";
    }

    String password = preferences.getString(ssid.c_str(), "");

    if (password.isEmpty()) {
        Serial.println("No password found for SSID: " + ssid);
    } else {
        Serial.println("Password retrieved: " + password);
    }

    preferences.end();
    return password;
}

void WiFiFunctions::clearAllWiFiCredentials() {
    if (!preferences.begin("wifi", false)) {
        Serial.println("Failed to open preferences for clearing");
        return;
    }

    preferences.clear();
    Serial.println("All WiFi credentials cleared.");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("All WiFi credentials cleared...");
    delay(2000);
    displayManager.tdeck_begin();
    preferences.end();
}

void WiFiFunctions::networkDiscovery(){
    if (!connectedToNetwork)
    {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Please connect to wifi network first");
        displayManager.printCommandScreen();
        return;
    }
    else {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Devices discovery started ...");
        IPAddress gatewayIP = WiFi.gatewayIP();
        IPAddress subnetMask = WiFi.subnetMask();

        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("gatewayIP ip ");
        displayManager.println(gatewayIP.toString());

        Serial.print("gatewayIP ip ");
        Serial.println(gatewayIP);

        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("subnetMask ip ");
        displayManager.println(subnetMask.toString());

        Serial.print("subnetMask ip ");
        Serial.println(subnetMask);
        pingScan(gatewayIP, subnetMask);
    }
}

void WiFiFunctions::pingScan(const IPAddress& gatewayIP, const IPAddress& subnetMask) {
   IPAddress targetIP;
  uint8_t gatewayLastPart = gatewayIP[3];
  bool stopScan = false;

  for (int i = 1; i <= gatewayLastPart; ++i) {
    targetIP = gatewayIP;
    targetIP[3] = i;

    Serial.print("targetIP ip ");
    Serial.println(targetIP);

    if (Ping.ping(targetIP)) {
      displayManager.setCursor(10, displayManager.getCursorY());
      displayManager.printText("Found device at IP: ");
      displayManager.println(targetIP.toString());
    }

      char incomingKey = inputHandler.getKeyboardInput();
      if (incomingKey == 'q' || incomingKey == 'Q') {
        stopScan = true;
        break;
      }
    
  }

  if (stopScan) {
    displayManager.printCommandScreen();
    return;
  }
}

void WiFiFunctions::networkPortScan(char* args){
    Serial.println("networkPortScan");
    Serial.println(args);

    String ipAddress = utils.getValue(args, ' ', 0);
    String startPortStr = utils.getValue(args, ' ', 1);
    String endPortStr = utils.getValue(args, ' ', 2);
    IPAddress targetIp;
    Serial.print("ipAddress:");
    Serial.println(ipAddress);
    Serial.print("startPortStr:");
    Serial.println(startPortStr);
    Serial.print("endPortStr:");
    Serial.println(endPortStr);

    if (ipAddress.isEmpty() || startPortStr.isEmpty() || endPortStr.isEmpty())
    {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Invalid command usage. Usage: portscan [ip address] [start port] [end port]");
        displayManager.printCommandScreen();
    }        
    else if (!targetIp.fromString(ipAddress))
        {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Invalid IP address.");
            displayManager.setTextColor(TFT_RED);
            delay(1000);
            displayManager.printCommandScreen();
            return;
        }

        else if (!Ping.ping(targetIp))
        {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Target IP address is unreachable.");
            delay(1000);
            displayManager.printCommandScreen();
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
void WiFiFunctions::performPortScan(const IPAddress& targetIP, int startPort, int endPort)
{
    const int portsPerPage = 5;
    int currentPage = 0;
    char incomingKey = 0;

    while (true)
    {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setDefaultTextSize();
        displayManager.printText("Port Scan - IP: ");
        displayManager.println(targetIP.toString());
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Range: ");
        displayManager.printText(startPort);
        displayManager.printText(" - ");
        displayManager.println(endPort);
        displayManager.println("--------------------");
        displayManager.println("| Port   |  Status |");
        displayManager.println("--------------------");

        for (int port = startPort; port <= endPort; port++)
        {
            String status = performPortCheck(targetIP, port) ? "Open" : "Closed";
            if(strcmp(status.c_str(),"Open") == 0){
                displayManager.setDefaultTextSize();
                displayManager.printText("| ");
                displayManager.printText(port);
                displayManager.setCursor(displayManager.getCursorX(), displayManager.getCursorY());
                displayManager.printText("    |  ");
                
                displayManager.printText(status);
                for (int j = 2; j < 8 - status.length(); j++)
                {
                    displayManager.printText(" ");
                }
                displayManager.println(" |");
                displayManager.println();

            }
        }
        displayManager.println("--------------------");
        displayManager.printDefaultTableHelpInstructions();

        while (true)
        {
            incomingKey = inputHandler.getKeyboardInput();
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
                displayManager.printCommandScreen();
                return;
            }
        }
    }
}
bool WiFiFunctions::performPortCheck(const IPAddress& ip, int port)
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