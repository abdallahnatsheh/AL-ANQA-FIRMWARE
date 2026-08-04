#ifndef BLE_INFO_H
#define BLE_INFO_H

void runBleInfo(char* arg);

// Name/MAC of the last device bi connected to and read a GAP Device Name (0x2A00) from
// — lets `ux ble` offer a device's real name as a spoof name after an [i] inspect.
// Name is "" if none was read. Compare bleInfoLastMac() to confirm it's the same device.
const char* bleInfoLastName();
const char* bleInfoLastMac();

#endif // BLE_INFO_H
