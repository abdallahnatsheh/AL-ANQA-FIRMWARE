#ifndef BLUETOOTH_FUNCTIONS_H
#define BLUETOOTH_FUNCTIONS_H

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "display_manager.h"

class BluetoothFunctions {
public:
    BluetoothFunctions();
    void scanBluetoothDevices();

private:
    BLEScan* pBLEScan;
    bool bluetoothScanExecuted;
    int numberOfDevices = 0;
    const int scanTime = 5;
};

#endif // BLUETOOTH_FUNCTIONS_H