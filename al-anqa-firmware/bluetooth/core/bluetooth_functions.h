#ifndef BLUETOOTH_FUNCTIONS_H
#define BLUETOOTH_FUNCTIONS_H

#include <NimBLEDevice.h>
#include "display_manager.h"
#include "task_manager.h"

// Shared scan cache — populated by scanblue, read by bleinfo
struct BleEntry { char addr[18]; int rssi; char name[20]; uint8_t addrType; uint16_t companyId; };
extern BleEntry      s_bleDevices[64];
extern volatile int  s_bleCount;

class BluetoothFunctions {
public:
    BluetoothFunctions();
    void scanBluetoothDevices();
    void showBleResults();
    // Run one blocking BLE scan (spinner UI), (re)populating s_bleDevices/s_bleCount.
    // Returns device count, or -1 if the user aborted with [q]. Shared by `sbl` and the
    // `ux ble` clone picker so both get the same live list (no stale-cache surprises).
    int  scanBleIntoCache();
    // Idle the scanner (stop, drop callbacks, clear results, BT status off). Safe to call
    // when no scan is active. Reused by `sbl` [q] and the `ux ble` picker before HID starts.
    void stopBleScan();

private:
    NimBLEScan*                       pBLEScan;
    NimBLEScanCallbacks*  pScanCallbacks;
    bool bluetoothScanExecuted;
    int  numberOfDevices = 0;
    const int scanTime   = 5;
};

extern BluetoothFunctions bluetoothFunctions;   // global instance (defined in main)

#endif // BLUETOOTH_FUNCTIONS_H
