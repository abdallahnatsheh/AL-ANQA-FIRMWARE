#include "bluetooth_functions.h"
#include "task_manager.h"
#include "utils.h"
#include "input_handling.h"
#include "display_manager.h"
#include "lockscreen_manager.h"
#include "oui_lookup.h"
#include "ble_ident.h"
#include <esp_wifi.h>
#include "board_power.h"

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
        // 16-bit BLE company ID from manufacturer data (0 = none) — survives MAC
        // randomization, so it names the vendor even for `rnd` devices.
        uint16_t cid = 0;
        if (dev->haveManufacturerData()) {
            std::string m = dev->getManufacturerData();
            if (m.size() >= 2) cid = (uint8_t)m[0] | ((uint16_t)(uint8_t)m[1] << 8);
        }
        TaskResult r;
        r.type = TaskResult::INFO;
        snprintf(r.data, sizeof(r.data), "%s|%d|%.18s|%d|%u",
                 dev->getAddress().toString().c_str(),
                 dev->getRSSI(),
                 dev->getName().c_str(),
                 (int)dev->getAddress().getType(),
                 (unsigned)cid);
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

// Drain every queued advert into the shared cache (single copy of the parse the scan
// loop used to inline twice). `time|rssi|name|addrType|companyId` pipe format.
static void bleDrainResultsToCache() {
    if (!TaskManager::resultQueue) return;
    TaskResult r;
    while (xQueueReceive(TaskManager::resultQueue, &r, 0) == pdTRUE) {
        if (r.type != TaskResult::INFO || s_bleCount >= 64) continue;
        char tmp[sizeof(r.data)];
        strncpy(tmp, r.data, sizeof(tmp));
        char* p1 = strchr(tmp, '|');
        char* p2 = p1 ? strchr(p1 + 1, '|') : nullptr;
        char* p3 = p2 ? strchr(p2 + 1, '|') : nullptr;
        char* p4 = p3 ? strchr(p3 + 1, '|') : nullptr;
        if (!p1 || !p2) continue;
        *p1 = '\0'; *p2 = '\0';
        if (p3) *p3 = '\0';
        if (p4) *p4 = '\0';
        int idx = s_bleCount++;
        strncpy(s_bleDevices[idx].addr, tmp, 17);      s_bleDevices[idx].addr[17] = '\0';
        s_bleDevices[idx].rssi      = atoi(p1 + 1);
        strncpy(s_bleDevices[idx].name, p2 + 1, 19);    s_bleDevices[idx].name[19] = '\0';
        s_bleDevices[idx].addrType  = p3 ? (uint8_t)atoi(p3 + 1) : 0;
        s_bleDevices[idx].companyId = p4 ? (uint16_t)atoi(p4 + 1) : 0;
    }
}

static uint16_t bleRssiColor(int rssi) {
    if (rssi >= -60) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    return TFT_ORANGE;
}

// ── BLE scan table (bmon-style) ───────────────────────────────────────────────
#if defined(BOARD_TPAGER)
#define SB_PER       7                     // fewer rows on the shorter 222px screen (footer fits)
#else
#define SB_PER       9                     // rows per page
#endif
#define SB_RY(n)    (outputY + (n) * LINE_HEIGHT)
#define SBX_IDX      2
#define SBX_NAME    24
#define SBX_RSSI   122
#define SBX_AT     150
#define SBX_MAC    176

static void parseMacStr(const char* s, uint8_t* out) {
    out[0]=out[1]=out[2]=out[3]=out[4]=out[5]=0;
    sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]);
}

// sel = page-local highlighted row (-1 = none). Rows sorted strongest-first;
// the [idx] column shows the original cache index (what `bi N` expects).
static void renderBlePage(int page, int perPage, int total, int sel) {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    // sort indices by RSSI desc (display only — cache order untouched)
    static int order[64];
    for (int i = 0; i < total; i++) order[i] = i;
    for (int i = 0; i < total - 1; i++)
        for (int j = i + 1; j < total; j++)
            if (s_bleDevices[order[j]].rssi > s_bleDevices[order[i]].rssi) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }

    int totalPages = max(1, (total + perPage - 1) / perPage);

    // row 0 — header
    dm.setCursor(SBX_IDX, SB_RY(0));
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("SCAN");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("BLE");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    char hb[28]; snprintf(hb, sizeof(hb), "%d dev%s   %d/%d",
                          total, total == 1 ? "" : "s", page + 1, totalPages);
    dm.setTextColor(TFT_WHITE);  dm.printText(hb);

    // row 1 — column headers
    dm.setTextColor(0x7BEF);
    dm.setCursor(SBX_IDX,  SB_RY(1)); dm.printText("#");
    dm.setCursor(SBX_NAME, SB_RY(1)); dm.printText("NAME / VENDOR");
    dm.setCursor(SBX_RSSI, SB_RY(1)); dm.printText("RSSI");
    dm.setCursor(SBX_AT,   SB_RY(1)); dm.printText("AT");
    dm.setCursor(SBX_MAC,  SB_RY(1)); dm.printText("MAC");

    // row 2 — separator
    dm.setCursor(SBX_IDX, SB_RY(2)); dm.printSeparator();

    int start = page * perPage;
    int end   = min(start + perPage, total);
    for (int si = start; si < end; si++) {
        int16_t ry = SB_RY(3 + (si - start));
        int oi = order[si];
        BleEntry& d = s_bleDevices[oi];
        bool seld = ((si - start) == sel);
        if (seld) dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);

        // index (used by `bi N`)
        dm.setCursor(SBX_IDX, ry);
        dm.setTextColor(seld ? TFT_YELLOW : TFT_CYAN);
        char ib[5]; snprintf(ib, sizeof(ib), "%2d", oi); dm.printText(ib);

        // name: advertised name → BLE company (mfr-data, beats MAC randomization)
        //       → OUI vendor (public MACs) → unknown
        uint8_t mac[6]; parseMacStr(d.addr, mac);
        const char* co   = bleCompanyName(d.companyId);
        const char* vend = ouiVendor(mac);
        const char* lbl  = d.name[0] ? d.name : (co ? co : (vend ? vend : "(unknown)"));
        char nm[18]; snprintf(nm, sizeof(nm), "%-16.16s", lbl);
        dm.setCursor(SBX_NAME, ry);
        dm.setTextColor(seld ? TFT_WHITE : (d.name[0] ? TFT_WHITE : 0xC618));
        dm.printText(nm);

        // rssi
        char rs[6]; snprintf(rs, sizeof(rs), "%4d", d.rssi);
        dm.setCursor(SBX_RSSI, ry);
        dm.setTextColor(bleRssiColor(d.rssi)); dm.printText(rs);

        // addr type
        dm.setCursor(SBX_AT, ry);
        dm.setTextColor(d.addrType == 0 ? 0xC618 : 0x7BEF);
        dm.printText(d.addrType == 0 ? "pub" : "rnd");

        // mac
        dm.setCursor(SBX_MAC, ry);
        dm.setTextColor(seld ? TFT_YELLOW : 0x7BEF); dm.printText(d.addr);
    }

    if (total == 0) {
        dm.setCursor(SBX_IDX, SB_RY(3));
        dm.setTextColor(0x7BEF);
        dm.printText("No devices found - press [u] to rescan.");
    }

    // separator + footer
    dm.setCursor(SBX_IDX, SB_RY(perPage + 3)); dm.printSeparator();
    dm.setCursor(SBX_IDX, SB_RY(perPage + 4));
    dm.setTextColor(TFT_DARKGREY);
    dm.printText("trackpad=sel [a/l]=page [u]rescan [q]quit  (bi # = info)");
}

void BluetoothFunctions::showBleResults() {
    if (!bluetoothScanExecuted || s_bleCount == 0) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No scan data. Run scanblue first.");
        displayManager.printCommandScreen();
        return;
    }
    const int perPage = SB_PER;
    int total       = s_bleCount;
    int totalPages  = max(1, (total + perPage - 1) / perPage);
    int currentPage = 0, sel = 0;
    bool redraw = true;
    while (true) {
        int pageCount = min(perPage, max(0, total - currentPage * perPage));
        if (sel >= pageCount && pageCount > 0) sel = pageCount - 1;
        if (sel < 0) sel = 0;
        if (redraw) { renderBlePage(currentPage, perPage, total, sel); redraw = false; }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) { currentPage++; sel = 0; redraw = true; } }
        else if (k == 'a' || k == 'A') { if (currentPage > 0) { currentPage--; sel = 0; redraw = true; } }
        else if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
        if (tb == TBALL_DOWN && sel < pageCount - 1) { sel++; redraw = true; }
        if (tb == TBALL_UP   && sel > 0)             { sel--; redraw = true; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// One blocking BLE scan → shared cache. Returns count, or -1 on [q] abort.
// (Extracted so `ux ble`'s clone picker gets the identical live list.)
int BluetoothFunctions::scanBleIntoCache() {
    // init("") is idempotent:
    //   - Stack already up (e.g. btkbd left it alive): no-op, scan runs on
    //     the existing idle stack — this is intentional. btkbd deliberately
    //     skips deinit because the ESP32 BT controller can't be fully reset
    //     in software after HID; deinit+reinit breaks subsequent scanning.
    //   - Stack down (after buddy/ble_spam/fast_pair which call deinit): fresh
    //     init from clean state.
    // Do NOT add a deinit cycle here — it would tear down the stack that btkbd
    // intentionally left alive, causing the same scan failure we're fixing.
    //
    // T-Pager: release WiFi so the BT controller can claim its internal RAM (no-op
    // on T-Deck / when WiFi is already down). Without this, sbl after karma/pwn crashes.
    boardBleRadioPrepare();
    NimBLEDevice::init("");
    displayManager.setBtActive(true);
    displayManager.updateStatusBar();
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    s_bleCount = 0;
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
        bleDrainResultsToCache();
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

    if (!aborted) bleDrainResultsToCache();   // final sweep of anything left queued

    TaskManager::cleanup();
    numberOfDevices       = s_bleCount;
    bluetoothScanExecuted = true;

    if (aborted) { stopBleScan(); return -1; }
    return s_bleCount;
}

// Idle the scanner — safe no-op when nothing is scanning.
void BluetoothFunctions::stopBleScan() {
    if (pBLEScan) {
        if (pBLEScan->isScanning()) pBLEScan->stop();
        pBLEScan->setScanCallbacks(nullptr);
        pBLEScan->clearResults();
    }
    if (pScanCallbacks) { delete pScanCallbacks; pScanCallbacks = nullptr; }
    displayManager.setBtActive(false);
}

void BluetoothFunctions::scanBluetoothDevices() {
    const int perPage = SB_PER;
    int currentPage   = 0;
    int sel           = 0;
    bool needScan     = true;

    while (true) {
        if (needScan) {
            currentPage = 0;
            sel         = 0;
            needScan    = false;
            if (scanBleIntoCache() < 0) {          // [q] during the scan
                displayManager.printCommandScreen();
                return;
            }
        }

        renderBlePage(currentPage, perPage, numberOfDevices, sel);

        while (true) {
            char k = inputHandler.getKeyboardInput();
            TrackballEvent tb = inputHandler.getTrackballEvent();
            int totalPages = max(1, (numberOfDevices + perPage - 1) / perPage);
            int pageCount  = min(perPage, max(0, numberOfDevices - currentPage * perPage));
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) { currentPage++; sel = 0; } break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              { currentPage--; sel = 0; } break; }
            if (k == 'u' || k == 'U') { needScan = true; sel = 0; break; }
            if (tb == TBALL_DOWN && sel < pageCount - 1) { sel++; renderBlePage(currentPage, perPage, numberOfDevices, sel); }
            if (tb == TBALL_UP   && sel > 0)             { sel--; renderBlePage(currentPage, perPage, numberOfDevices, sel); }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
            if (k == 'q' || k == 'Q') {
                stopBleScan();
                displayManager.printCommandScreen();
                return;
            }
        }
    }
}
