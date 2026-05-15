#ifndef BLUETOOTH_FUNCTIONS_H
#define BLUETOOTH_FUNCTIONS_H

#include <NimBLEDevice.h>
#include "display_manager.h"
#include "task_manager.h"

class BluetoothFunctions {
public:
    BluetoothFunctions();
    void scanBluetoothDevices();
    void showBleResults();

private:
    NimBLEScan*                       pBLEScan;
    NimBLEAdvertisedDeviceCallbacks*  pScanCallbacks;
    bool bluetoothScanExecuted;
    int  numberOfDevices = 0;
    const int scanTime   = 5;
};

#endif // BLUETOOTH_FUNCTIONS_H
