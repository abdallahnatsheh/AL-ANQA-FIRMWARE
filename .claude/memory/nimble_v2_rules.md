---
name: NimBLE v2.x Rules
description: All NimBLE-Arduino v2.x gotchas — apply to every BLE command
type: project
---

## 1 — Peripheral name visibility (scan response)
v2.x dropped auto-name in advertisement. Any peripheral that must be discoverable by name:
```cpp
pAdv->enableScanResponse(true);
pAdv->setName(deviceName);
pAdv->startAdvertising();
```
Without this, the device is invisible to name-filtering scanners (e.g. Claude Desktop).

## 2 — BLE cleanup: never re-init after deinit
`deinit(true)` + then `init("T-REX")` in cleanup = stale named host task left alive → next BLE command's startup tears it down → Windows bond corrupted, btkbd reconnects fail.

**Correct cleanup for commands that own their init/deinit (buddy, btkbd, ble_spam, fast_pair):**
```cpp
NimBLEDevice::deinit(true);
vTaskDelay(pdMS_TO_TICKS(100));
// DO NOT reinit — next BLE command inits from clean state
SD.begin(39);
```
Commands that do NOT deinit (scanblue, bluetooth_functions.cpp): call `NimBLEDevice::init("")` at start (idempotent).

**Status 2026-05-29:** buddy ✅ ble_spam ✅ fast_pair ✅ btkbd ✅

## 3 — Scan is non-blocking
`NimBLEScan::start()` returns **immediately** in v2.x. Old FreeRTOS-task wrapper exits in <1s.

**Correct pattern:**
```cpp
pScan->start(0);  // 0 = continuous; drive timeout yourself
uint32_t t0 = millis();
while (millis() - t0 < SCAN_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (inputHandler.getKeyboardInput() == 'q') { aborted = true; break; }
}
pScan->stop();
pScan->setScanCallbacks(nullptr);
pScan->clearResults();
```

## 4 — Auto-bond-delete on auth failure (btkbd pattern)
Prevents stale-LTK loops without user removing device from BT Settings:
```cpp
void onDisconnect(..., int reason) {
    if (reason == 0x05 || reason == 0x06)  // AUTH_FAIL / KEY_MISSING
        NimBLEDevice::deleteBond(connInfo.getAddress());
}
void onAuthenticationComplete(NimBLEConnInfo& info) {
    if (!info.isEncrypted())
        NimBLEDevice::deleteBond(info.getAddress());
}
```
