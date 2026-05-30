#include "bluetooth_functions.h"
#include "task_manager.h"
#include "utils.h"
#include "input_handling.h"
#include "display_manager.h"
#include "lockscreen_manager.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

BluetoothFunctions::BluetoothFunctions()
    : pBLEScan(nullptr), pScanCallbacks(nullptr),
      bluetoothScanExecuted(false), numberOfDevices(0) {}

// ── BLE device cache (shared with bleinfo) ────────────────────────────────────
BleEntry     s_bleDevices[64];
volatile int s_bleCount = 0;

class BleQueueCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (!TaskManager::resultQueue) return;
        TaskResult r;
        r.type = TaskResult::INFO;
        snprintf(r.data, sizeof(r.data), "%s|%d|%.18s|%d",
                 dev->getAddress().toString().c_str(),
                 dev->getRSSI(),
                 dev->getName().c_str(),
                 (int)dev->getAddress().getType());
        xQueueSend(TaskManager::resultQueue, &r, 0);
    }
};

static void bleScanTaskFn(void* param) {
    NimBLEScan* scan = static_cast<NimBLEScan*>(param);
    scan->start(5000, false);
    // v2.x: start() is async — block here until scan finishes or abort is requested
    while (scan->isScanning() && TaskManager::taskRunning) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    TaskResult done;
    done.type = TaskResult::DONE;
    done.data[0] = '\0';
    if (TaskManager::resultQueue) xQueueSend(TaskManager::resultQueue, &done, 0);
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

static uint16_t bleRssiColor(int rssi) {
    if (rssi >= -60) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    return TFT_RED;
}

static void renderBlePage(int page, int perPage, int total) {
    int totalPages = max(1, (total + perPage - 1) / perPage);
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    char hdr[8]; snprintf(hdr, sizeof(hdr), "%02d/%02d", page + 1, totalPages);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("SCAN");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("BLE");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("]  ");
    displayManager.setTextColor(0x7BEF);     displayManager.println(hdr);
    displayManager.printSeparator();

    int start = page * perPage;
    int end   = min(start + perPage, total);
    for (int i = start; i < end; i++) {
        displayManager.setCursor(10, displayManager.getCursorY());
        char idx[5]; snprintf(idx, sizeof(idx), "[%d]", i);
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText(idx);
        displayManager.setTextColor(TFT_WHITE);
        char mac[20]; snprintf(mac, sizeof(mac), " %s", s_bleDevices[i].addr);
        displayManager.printText(mac);
        char rssiStr[8]; snprintf(rssiStr, sizeof(rssiStr), " %4d", s_bleDevices[i].rssi);
        displayManager.setTextColor(bleRssiColor(s_bleDevices[i].rssi));
        displayManager.printText(rssiStr);
        if (s_bleDevices[i].name[0]) {
            char name[22]; snprintf(name, sizeof(name), " %.9s", s_bleDevices[i].name);
            displayManager.setTextColor(TFT_YELLOW); displayManager.println(name);
        } else { displayManager.println(); }
    }
    displayManager.printSeparator();
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printDefaultTableHelpInstructions();
}

void BluetoothFunctions::showBleResults() {
    if (!bluetoothScanExecuted || s_bleCount == 0) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No scan data. Run scanblue first.");
        displayManager.printCommandScreen();
        return;
    }
    const int perPage = 10;
    int total         = s_bleCount;
    int totalPages    = max(1, (total + perPage - 1) / perPage);
    int currentPage   = 0;
    while (true) {
        renderBlePage(currentPage, perPage, total);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        }
    }
}

void BluetoothFunctions::scanBluetoothDevices() {
    const int perPage = 10;
    int currentPage   = 0;
    bool needScan     = true;

    // init("") is idempotent:
    //   - Stack already up (e.g. btkbd left it alive): no-op, scan runs on
    //     the existing idle stack — this is intentional. btkbd deliberately
    //     skips deinit because the ESP32 BT controller can't be fully reset
    //     in software after HID; deinit+reinit breaks subsequent scanning.
    //   - Stack down (after buddy/ble_spam/fast_pair which call deinit): fresh
    //     init from clean state.
    // Do NOT add a deinit cycle here — it would tear down the stack that btkbd
    // intentionally left alive, causing the same scan failure we're fixing.
    NimBLEDevice::init("");
    displayManager.setBtActive(true);
    displayManager.updateStatusBar();
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    while (true) {
        if (needScan) {
            s_bleCount  = 0;
            currentPage = 0;
            needScan    = false;

            delete pScanCallbacks;
            pScanCallbacks = new BleQueueCallbacks();
            pBLEScan->setScanCallbacks(pScanCallbacks);
            pBLEScan->clearResults();

            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.println("Scanning BLE...  [q]=abort");

            TaskManager::start(bleScanTaskFn, "blescan", pBLEScan, TASK_STACK_DEFAULT, 0);

            uint32_t frame = 0;
            const char spinner[] = "|/-\\";
            bool aborted = false;

            while (TaskManager::isRunning() ||
                   uxQueueMessagesWaiting(TaskManager::resultQueue) > 0) {
                TaskResult r;
                while (xQueueReceive(TaskManager::resultQueue, &r, 0) == pdTRUE) {
                    if (r.type == TaskResult::INFO && s_bleCount < 64) {
                        char tmp[sizeof(r.data)];
                        strncpy(tmp, r.data, sizeof(tmp));
                        char* p1 = strchr(tmp, '|');
                        char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
                        char* p3 = p2 ? strchr(p2 + 1, '|') : nullptr;
                        if (p1 && p2) {
                            *p1 = '\0'; *p2 = '\0';
                            if (p3) *p3 = '\0';
                            int idx = s_bleCount++;
                            strncpy(s_bleDevices[idx].addr, tmp, 17);
                            s_bleDevices[idx].addr[17] = '\0';
                            s_bleDevices[idx].rssi = atoi(p1 + 1);
                            strncpy(s_bleDevices[idx].name, p2 + 1, 19);
                            s_bleDevices[idx].name[19] = '\0';
                            s_bleDevices[idx].addrType = p3 ? (uint8_t)atoi(p3 + 1) : 0;
                        }
                    }
                }
                char buf[28];
                snprintf(buf, sizeof(buf), "Scanning BLE... %c  found:%d",
                         spinner[frame++ % 4], (int)s_bleCount);
                displayManager.fillRect(10, outputY, SCREEN_WIDTH - 10, LINE_HEIGHT, TFT_BLACK);
                displayManager.setCursor(10, outputY);
                displayManager.setTextColor(TFT_CYAN);
                displayManager.printText(buf);
                vTaskDelay(pdMS_TO_TICKS(100));
                if (inputHandler.getKeyboardInput() == 'q') {
                    pBLEScan->stop();
                    TaskManager::requestStop();
                    aborted = true;
                    break;
                }
            }

            if (!aborted && TaskManager::resultQueue) {
                TaskResult r;
                while (xQueueReceive(TaskManager::resultQueue, &r, 0) == pdTRUE) {
                    if (r.type == TaskResult::INFO && s_bleCount < 64) {
                        char tmp[sizeof(r.data)];
                        strncpy(tmp, r.data, sizeof(tmp));
                        char* p1 = strchr(tmp, '|');
                        char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
                        char* p3 = p2 ? strchr(p2 + 1, '|') : nullptr;
                        if (p1 && p2) {
                            *p1 = '\0'; *p2 = '\0';
                            if (p3) *p3 = '\0';
                            int idx = s_bleCount++;
                            strncpy(s_bleDevices[idx].addr, tmp, 17);
                            s_bleDevices[idx].addr[17] = '\0';
                            s_bleDevices[idx].rssi = atoi(p1 + 1);
                            strncpy(s_bleDevices[idx].name, p2 + 1, 19);
                            s_bleDevices[idx].name[19] = '\0';
                            s_bleDevices[idx].addrType = p3 ? (uint8_t)atoi(p3 + 1) : 0;
                        }
                    }
                }
            }

            TaskManager::cleanup();
            numberOfDevices       = s_bleCount;
            bluetoothScanExecuted = true;

            if (aborted) {
                pBLEScan->clearResults();
                pBLEScan->setScanCallbacks(nullptr);
                delete pScanCallbacks; pScanCallbacks = nullptr;
                displayManager.setBtActive(false);
                displayManager.printCommandScreen();
                return;
            }
        }

        renderBlePage(currentPage, perPage, numberOfDevices);

        while (true) {
            char k = inputHandler.getKeyboardInput();
            int totalPages = max(1, (numberOfDevices + perPage - 1) / perPage);
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0) currentPage--; break; }
            if (k == 'u' || k == 'U') { needScan = true; break; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
            if (k == 'q' || k == 'Q') {
                pBLEScan->clearResults();
                pBLEScan->setScanCallbacks(nullptr);
                delete pScanCallbacks; pScanCallbacks = nullptr;
                displayManager.setBtActive(false);
                displayManager.printCommandScreen();
                return;
            }
        }
    }
}
