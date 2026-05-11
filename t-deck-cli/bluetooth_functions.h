#ifndef BLUETOOTH_FUNCTIONS_H
#define BLUETOOTH_FUNCTIONS_H

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "display_manager.h"
#include "task_manager.h"

class BluetoothFunctions {
public:
    BluetoothFunctions();
    void scanBluetoothDevices();
    void showBleResults();

private:
    BLEScan* pBLEScan;
    BLEAdvertisedDeviceCallbacks* pScanCallbacks;
    bool bluetoothScanExecuted;
    int numberOfDevices = 0;
    const int scanTime = 5;
};

#endif // BLUETOOTH_FUNCTIONS_H