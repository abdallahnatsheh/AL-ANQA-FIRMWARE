#include "bluetooth_functions.h"
#include "utils.h"
#include "input_handling.h"
#include "display_manager.h"

extern DisplayManager displayManager;
extern InputHandling inputHandler;
extern Utils utils;
BluetoothFunctions::BluetoothFunctions() : pBLEScan(nullptr), pScanCallbacks(nullptr), bluetoothScanExecuted(false), numberOfDevices(0) {}


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {}
};

void BluetoothFunctions::scanBluetoothDevices() {
    const int devicesPerPage = 6;
    int currentPage = 0;
    int totalPages = 0;
    char incomingKey = 0;
    bool updateInProgress = false;
    numberOfDevices = 0;

    BLEDevice::init("");
    displayManager.setBtActive(true);
    displayManager.updateStatusBar();
    pBLEScan = BLEDevice::getScan();
    delete pScanCallbacks;
    pScanCallbacks = new MyAdvertisedDeviceCallbacks();
    pBLEScan->setAdvertisedDeviceCallbacks(pScanCallbacks);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    BLEScanResults foundDevices;

    while (true) {
        if (updateInProgress || numberOfDevices == 0) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Scanning Bluetooth Devices:");
            displayManager.println();
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Press 'q' to exit");
            displayManager.println();
            updateInProgress = false;
            foundDevices = pBLEScan->start(scanTime, false);
            numberOfDevices = foundDevices.getCount();
            totalPages = (numberOfDevices + devicesPerPage - 1) / devicesPerPage;
            bluetoothScanExecuted = true;
            currentPage = 0;
        }

        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextSize(1);
        displayManager.printText("Page ");
        displayManager.printText(currentPage + 1);
        displayManager.printText("/");
        displayManager.println(totalPages);
        displayManager.println("-----------------------------------------------------");
        displayManager.println("| Index |       Address       | RSSI  |    Name    |");
        displayManager.println("-----------------------------------------------------");

        int startIndex = currentPage * devicesPerPage;
        int endIndex = min(startIndex + devicesPerPage, numberOfDevices);

        for (int i = startIndex; i < endIndex; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);
            displayManager.printText("|   ");
            displayManager.printText(i);
            displayManager.printText("   | ");
            displayManager.printText(device.getAddress().toString().c_str());
            for (int j = 0; j < 19 - device.getAddress().toString().length(); j++) {
                displayManager.printText(" ");
            }
            displayManager.printText(" |  ");
            displayManager.printText(device.getRSSI());
            displayManager.printText("   | ");

            String name = device.getName().c_str();
            if (name.length() > 7) {
                name = name.substring(0, 7);
                name += "...";
            }

            displayManager.printText(name);
            for (int j = 0; j < 12 - name.length(); j++) {
                displayManager.printText(" ");
            }
            displayManager.println("|");
        }

        displayManager.println("-----------------------------------------------------");
        displayManager.printDefaultTableHelpInstructions();

        while (true) {
            incomingKey = inputHandler.getKeyboardInput();
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
                delete pScanCallbacks;
                pScanCallbacks = nullptr;
                displayManager.setBtActive(false);
                displayManager.printCommandScreen();
                return;
            } else if (incomingKey == 'u' || incomingKey == 'U') {
                updateInProgress = true;
                break;
            }
        }
    }
}