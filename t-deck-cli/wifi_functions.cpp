#include <Preferences.h>
#include <vector>
#include "wifi_functions.h"
#include "input_handling.h"
#include "task_manager.h"
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
    if (sscanf(args, "%d", &networkIndex) != 1) {
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
        if (password.isEmpty()) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.printText("Enter password for network ");
            displayManager.println(ssid);
            password = readPassword();
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
                else if (isPrintable(input) && input != ' ' && password.length() < 100)
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

    preferences.putString(ssid.c_str(), password);

    preferences.end();
}

String WiFiFunctions::getWiFiPassword(const String& ssid) {
    if (!preferences.begin("wifi", true)) {
        Serial.println("Failed to open preferences for reading");
        return "";
    }

    String password = preferences.getString(ssid.c_str(), "");

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

// ── FreeRTOS task: ping scan on Core 0 ───────────────────────────────────────
struct PingScanParams { uint8_t base[4]; };

static void pingScanTaskFn(void* p) {
    PingScanParams* params = (PingScanParams*)p;
    for (int i = 1; i <= 254; i++) {
        if (TaskManager::stopRequested) break;
        IPAddress target(params->base[0], params->base[1], params->base[2], i);
        if (Ping.ping(target, 1)) {
            TaskResult result;
            result.type = TaskResult::HOST_FOUND;
            snprintf(result.data, sizeof(result.data), "%d.%d.%d.%d",
                     params->base[0], params->base[1], params->base[2], i);
            xQueueSend(TaskManager::resultQueue, &result, pdMS_TO_TICKS(200));
        }
    }
    TaskResult done; done.type = TaskResult::DONE; done.data[0] = '\0';
    xQueueSend(TaskManager::resultQueue, &done, pdMS_TO_TICKS(1000));
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

// ── FreeRTOS task: port scan on Core 0 ───────────────────────────────────────
struct PortScanParams { uint32_t targetIPRaw; int startPort, endPort; };

static void portScanTaskFn(void* p) {
    PortScanParams* params = (PortScanParams*)p;
    IPAddress targetIP(params->targetIPRaw);
    for (int port = params->startPort; port <= params->endPort; port++) {
        if (TaskManager::stopRequested) break;
        WiFiClient client;
        if (client.connect(targetIP, port, 500)) {
            client.stop();
            TaskResult result;
            result.type = TaskResult::PORT_OPEN;
            snprintf(result.data, sizeof(result.data), "%d", port);
            xQueueSend(TaskManager::resultQueue, &result, pdMS_TO_TICKS(200));
        }
    }
    TaskResult done; done.type = TaskResult::DONE; done.data[0] = '\0';
    xQueueSend(TaskManager::resultQueue, &done, pdMS_TO_TICKS(1000));
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

// ── networkDiscovery: launches ping scan task, shows results live ─────────────
void WiFiFunctions::networkDiscovery() {
    if (!connectedToNetwork) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Connect to WiFi first.");
        displayManager.printCommandScreen();
        return;
    }

    IPAddress gw = WiFi.gatewayIP();
    PingScanParams params;
    params.base[0] = gw[0]; params.base[1] = gw[1];
    params.base[2] = gw[2]; params.base[3] = 0;

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Network Discovery --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Subnet: ");
    displayManager.printText(gw[0]); displayManager.printText(".");
    displayManager.printText(gw[1]); displayManager.printText(".");
    displayManager.printText(gw[2]); displayManager.println(".0/24");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Scanning Core 0  q=stop");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("------------------------");

    if (!TaskManager::start(pingScanTaskFn, "pingscan", &params)) {
        displayManager.println("Task start failed.");
        displayManager.printCommandScreen();
        return;
    }

    int found = 0;
    while (TaskManager::isRunning() || uxQueueMessagesWaiting(TaskManager::resultQueue) > 0) {
        TaskResult result;
        if (xQueueReceive(TaskManager::resultQueue, &result, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (result.type == TaskResult::HOST_FOUND) {
                found++;
                displayManager.setCursor(10, displayManager.getCursorY());
                displayManager.setTextColor(TFT_GREEN);
                displayManager.printText("[+] ");
                displayManager.setTextColor(TFT_WHITE);
                displayManager.println(result.data);
            } else if (result.type == TaskResult::DONE) {
                break;
            }
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { TaskManager::requestStop(); break; }
    }

    TaskManager::cleanup();
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("------------------------");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Total hosts: ");
    displayManager.println(found);
    displayManager.printCommandScreen();
}

// ── networkPortScan: validates args then launches port scan task ──────────────
void WiFiFunctions::networkPortScan(char* args) {
    String ipAddress  = utils.getValue(args, ' ', 0);
    String startPortStr = utils.getValue(args, ' ', 1);
    String endPortStr   = utils.getValue(args, ' ', 2);

    if (ipAddress.isEmpty() || startPortStr.isEmpty() || endPortStr.isEmpty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage: portscan <ip> <start> <end>");
        displayManager.printCommandScreen();
        return;
    }

    IPAddress targetIp;
    if (!targetIp.fromString(ipAddress)) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Invalid IP address.");
        displayManager.setTextColor(TFT_WHITE);
        delay(1000);
        displayManager.printCommandScreen();
        return;
    }

    performPortScan(targetIp, startPortStr.toInt(), endPortStr.toInt());
}

// ── performPortScan: task on Core 0, live results on Core 1 ──────────────────
void WiFiFunctions::performPortScan(const IPAddress& targetIP, int startPort, int endPort) {
    PortScanParams params;
    params.targetIPRaw = (uint32_t)targetIP;
    params.startPort   = startPort;
    params.endPort     = endPort;

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Port Scan --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Target: "); displayManager.println(targetIP.toString());
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Range:  "); displayManager.printText(startPort);
    displayManager.printText(" - ");     displayManager.println(endPort);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Scanning Core 0  q=stop");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("------------------------");

    if (!TaskManager::start(portScanTaskFn, "portscan", &params)) {
        displayManager.println("Task start failed.");
        displayManager.printCommandScreen();
        return;
    }

    std::vector<int> openPorts;

    while (TaskManager::isRunning() || uxQueueMessagesWaiting(TaskManager::resultQueue) > 0) {
        TaskResult result;
        if (xQueueReceive(TaskManager::resultQueue, &result, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (result.type == TaskResult::PORT_OPEN) {
                int port = atoi(result.data);
                openPorts.push_back(port);
                displayManager.setCursor(10, displayManager.getCursorY());
                displayManager.setTextColor(TFT_GREEN);
                displayManager.printText("[+] Port ");
                displayManager.printText(port);
                displayManager.setTextColor(TFT_WHITE);
                displayManager.println(" OPEN");
            } else if (result.type == TaskResult::DONE) {
                break;
            }
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { TaskManager::requestStop(); break; }
    }

    TaskManager::cleanup();

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.println("------------------------");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Open ports found: ");
    displayManager.println((int)openPorts.size());
    displayManager.printCommandScreen();
}

bool WiFiFunctions::performPortCheck(const IPAddress& ip, int port) {
    WiFiClient client;
    if (client.connect(ip, port, 500)) { client.stop(); return true; }
    return false;
}

bool WiFiFunctions::isScanDone() const {
    return networkScanExecuted;
}

int WiFiFunctions::getNetworkCount() const {
    return numberOfNetworks;
}

bool WiFiFunctions::getNetworkInfo(int index, uint8_t* bssidOut, int* channelOut) {
    if (!networkScanExecuted || index < 0 || index >= numberOfNetworks) return false;
    uint8_t* b = WiFi.BSSID(index);
    if (!b) return false;
    memcpy(bssidOut, b, 6);
    *channelOut = WiFi.channel(index);
    return true;
}