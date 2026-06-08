#ifndef BLUETOOTH_FUNCTIONS_H
#define BLUETOOTH_FUNCTIONS_H

#include <NimBLEDevice.h>
#include "display_manager.h"
#include "task_manager.h"

// Shared scan cache — populated by scanblue, read by bleinfo
struct BleEntry { char addr[18]; int rssi; char name[20]; uint8_t addrType; };
extern BleEntry      s_bleDevices[64];
extern volatile int  s_bleCount;

class BluetoothFunctions {
public:
    BluetoothFunctions();
    void scanBluetoothDevices();
    void showBleResults();

private:
    NimBLEScan*                       pBLEScan;
    NimBLEScanCallbacks*  pScanCallbacks;
    bool bluetoothScanExecuted;
    int  numberOfDevices = 0;
    const int scanTime   = 5;
};

#endif // BLUETOOTH_FUNCTIONS_H
