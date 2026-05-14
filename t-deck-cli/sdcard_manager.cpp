#include "sdcard_manager.h"
#include "utilities.h"
#include "input_handling.h"

extern InputHandling inputHandler;

SDCardManager::SDCardManager(DisplayManager& displayManager)
    : displayManager(displayManager), ready(false) {}

bool SDCardManager::begin() {
    if (!SD.begin(BOARD_SDCARD_CS, SPI, 4000000)) {
        ready = false;
        return false;
    }
    // Set ready BEFORE ensureDir — ensureDir gates on this flag
    ready = true;
    ensureDir("/logs");
    ensureDir("/evilportal");
    ensureDir(SD_DIR_SCRIPTS);
    ensureDir(SD_DIR_CAPTURES);
    return true;
}

bool SDCardManager::isReady() const {
    return ready;
}

bool SDCardManager::ensureDir(const char* path) {
    if (!ready) return false;
    if (SD.exists(path)) {
        File f = SD.open(path);
        if (!f) return false;
        bool isDir = f.isDirectory();
        f.close();
        return isDir;
    }
    return SD.mkdir(path);
}

void SDCardManager::printInfo() {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    uint8_t cardType = SD.cardType();
    const char* typeStr = (cardType == CARD_MMC)  ? "MMC"  :
                          (cardType == CARD_SD)    ? "SD"   :
                          (cardType == CARD_SDHC)  ? "SDHC" : "UNKNOWN";

    uint64_t totalMB = SD.totalBytes() / (1024ULL * 1024ULL);
    uint64_t usedMB  = SD.usedBytes()  / (1024ULL * 1024ULL);

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Type:  "); displayManager.println(typeStr);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Total: "); displayManager.printText((int)totalMB); displayManager.println(" MB");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Used:  "); displayManager.printText((int)usedMB);  displayManager.println(" MB");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Free:  "); displayManager.printText((int)(totalMB - usedMB)); displayManager.println(" MB");
    displayManager.printCommandScreen();
}

void SDCardManager::listDirRecursive(File dir, int depth) {
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        displayManager.setCursor(10, displayManager.getCursorY());
        for (int i = 0; i < depth; i++) displayManager.printText("  ");

        if (entry.isDirectory()) {
            displayManager.setTextColor(TFT_CYAN);
            displayManager.printText("[");
            displayManager.printText(entry.name());
            displayManager.println("]");
            displayManager.setTextColor(TFT_WHITE);
            listDirRecursive(entry, depth + 1);
        } else {
            displayManager.printText(entry.name());
            displayManager.printText("  ");
            uint32_t sz = entry.size();
            if (sz < 1024) {
                displayManager.printText((int)sz); displayManager.println("B");
            } else {
                displayManager.printText((int)(sz / 1024)); displayManager.println("KB");
            }
        }
        entry.close();

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }
}

void SDCardManager::listDirectory(const char* path) {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    File root = SD.open(path);
    if (!root || !root.isDirectory()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Not found: ");
        displayManager.println(path);
        displayManager.printCommandScreen();
        return;
    }

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.printText("SD: ");
    displayManager.println(path);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("--------------------");
    listDirRecursive(root, 0);
    root.close();
    displayManager.println("--------------------");
    displayManager.printCommandScreen();
}

void SDCardManager::readFile(const char* path) {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    File file = SD.open(path);
    if (!file) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Not found: ");
        displayManager.println(path);
        displayManager.printCommandScreen();
        return;
    }

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println(path);
    displayManager.setTextColor(TFT_WHITE);

    const int linesPerPage = 10;
    String lines[linesPerPage];
    int lineCount = 0;
    int pageNum   = 0;

    while (file.available()) {
        lines[lineCount++] = file.readStringUntil('\n');

        if (lineCount == linesPerPage) {
            if (pageNum > 0) {
                displayManager.setTextColor(TFT_GREEN);
                displayManager.println("-- any key = next, q = quit --");
                displayManager.setTextColor(TFT_WHITE);
                char k = 0;
                while (k == 0) k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') { file.close(); displayManager.printCommandScreen(); return; }
                displayManager.clearScreen();
                displayManager.setCursor(10, outputY);
            }
            for (int i = 0; i < linesPerPage; i++) {
                displayManager.setCursor(10, displayManager.getCursorY());
                displayManager.println(lines[i]);
            }
            lineCount = 0;
            pageNum++;
        }
    }

    for (int i = 0; i < lineCount; i++) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println(lines[i]);
    }

    file.close();
    displayManager.printCommandScreen();
}

bool SDCardManager::removeFile(const char* path) {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return false;
    }

    if (!SD.exists(path)) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Not found: ");
        displayManager.println(path);
        displayManager.printCommandScreen();
        return false;
    }

    bool ok = SD.remove(path);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(ok ? TFT_GREEN : TFT_RED);
    displayManager.println(ok ? "Deleted." : "Delete failed.");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
    return ok;
}

bool SDCardManager::appendLine(const char* path, const String& line) {
    if (!ready) return false;
    File file = SD.open(path, FILE_APPEND);
    if (!file) return false;
    file.println(line);
    file.close();
    return true;
}

bool SDCardManager::performFormat() {
    ready = false;

    // Full dismount — resets Arduino SD library's internal pdrv to 0xFF
    SD.end();
    delay(100);

    // Remount so FatFs has a live disk I/O handle (required before f_unmount/f_mkfs)
    if (!SD.begin(BOARD_SDCARD_CS, SPI, 4000000)) {
        return false;
    }

    // Detach FatFs from the volume without touching the SPI driver.
    // Drive "0:" is what esp_vfs_fat_sdspi_mount registers for the first SD card.
    f_unmount("0:");

    // Format FAT32. Work buffer on heap — FF_MAX_SS can be up to 4096, avoid stack overflow.
    void* work = malloc(4096);
    if (!work) { SD.end(); return false; }
    FRESULT res = f_mkfs("0:", FM_FAT32, 0, work, 4096);
    free(work);

    if (res != FR_OK) { SD.end(); return false; }

    // Full clean dismount so Arduino SD library re-registers the freshly formatted volume
    SD.end();
    delay(200);

    if (!SD.begin(BOARD_SDCARD_CS, SPI, 4000000)) {
        return false;
    }

    ready = true;
    ensureDir("/logs");
    ensureDir("/evilportal");
    ensureDir(SD_DIR_SCRIPTS);
    ensureDir(SD_DIR_CAPTURES);
    return true;
}

bool SDCardManager::formatSDCard() {
    if (!ready) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No SD card inserted.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return false;
    }

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_RED);
    displayManager.println("======== WARNING ========");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("");
    displayManager.println("This will FORMAT the SD card.");
    displayManager.println("ALL DATA WILL BE LOST!");
    displayManager.println("");
    displayManager.println("Press 'y' to confirm.");
    displayManager.println("Press any other key to cancel.");
    displayManager.println("");
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Waiting for input...");
    displayManager.setTextColor(TFT_WHITE);

    char key = 0;
    while (key == 0) {
        key = inputHandler.getKeyboardInput();
        delay(50);
    }

    if (key != 'y' && key != 'Y') {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.println("Format cancelled.");
        displayManager.printCommandScreen();
        return false;
    }

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Formatting SD card...");
    displayManager.println("Please wait, do NOT remove card.");
    displayManager.setTextColor(TFT_WHITE);

    if (performFormat()) {
        displayManager.println("");
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Format successful!");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return true;
    } else {
        displayManager.println("");
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Format failed!");
        displayManager.setTextColor(TFT_WHITE);
        ready = false;
        displayManager.printCommandScreen();
        return false;
    }
}

bool SDCardManager::initializeTDeckStructure() {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return false;
    }

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Initializing T-DECK structure...");
    displayManager.setTextColor(TFT_WHITE);

    bool ok = true;
    if (!ensureDir("/logs"))          { displayManager.println("Failed: /logs");      ok = false; }
    if (!ensureDir("/evilportal"))    { displayManager.println("Failed: /evilportal");ok = false; }
    if (!ensureDir(SD_DIR_SCRIPTS))  { displayManager.println("Failed: /scripts");   ok = false; }
    if (!ensureDir(SD_DIR_CAPTURES)) { displayManager.println("Failed: /captures");  ok = false; }

    if (!ok) {
        displayManager.printCommandScreen();
        return false;
    }

    if (!SD.exists("/pwrsave.conf")) {
        File f = SD.open("/pwrsave.conf", FILE_WRITE);
        if (f) {
            f.println("# pwrsave.conf — generated by T-Rex");
            f.println("# Power save configuration (key=value)");
            f.println("enabled=true");
            f.println("timeoutMs=120000");
            f.println("screenOffTimeoutMs=300000");
            f.println("screenOffEnabled=true");
            f.println("dimBrightness=32");
            f.println("fullBrightness=128");
            f.println("batteryAwareDimEnabled=true");
            f.println("batteryThreshold=20");
            f.println("batteryDimBrightness=30");
            f.close();
            displayManager.println("Created /pwrsave.conf");
        }
    }

    if (!SD.exists("/macchanger.conf")) {
        File f = SD.open("/macchanger.conf", FILE_WRITE);
        if (f) {
            f.println("# macchanger.conf — generated by T-Rex");
            f.println("# MAC address changer configuration (key=value)");
            f.println("enabled=false");
            f.println("mode=random");
            f.println("restore_on_exit=true");
            f.println("target=wifi");
            f.close();
            displayManager.println("Created /macchanger.conf");
        }
    }

    if (!SD.exists("/signatures.csv")) {
        File f = SD.open("/signatures.csv", FILE_WRITE);
        if (f) {
            f.println("Name,Signature,Confidence");
            f.println("# Add custom BLE tracker signatures below");
            f.close();
            displayManager.println("Created /signatures.csv");
        }
    }

    displayManager.println("");
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("T-DECK structure initialized!");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
    return true;
}

void SDCardManager::formatCommand(char* args) {
    if (args && *args) {
        if (strcmp(args, "init") == 0) {
            if (formatSDCard()) {
                initializeTDeckStructure();
            }
        } else {
            displayManager.println("Usage:");
            displayManager.println("  sdf        - Format SD card");
            displayManager.println("  sdf init   - Format + initialize structure");
            displayManager.printCommandScreen();
        }
    } else {
        formatSDCard();
    }
}
