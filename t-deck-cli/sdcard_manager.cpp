#include "sdcard_manager.h"
#include "utilities.h"
#include "input_handling.h"

extern InputHandling inputHandler;

SDCardManager::SDCardManager(DisplayManager& displayManager)
    : displayManager(displayManager), ready(false) {}

bool SDCardManager::begin() {
    // SPI2_HOST already initialized by LovyanGFX (bus_shared=true).
    // SD.begin() adds a device on the existing bus via its CS pin.
    if (!SD.begin(BOARD_SDCARD_CS, SPI)) {
        ready = false;
        return false;
    }
    ready = true;
    ensureDir("/logs");
    ensureDir(SD_DIR_SCRIPTS);
    ensureDir(SD_DIR_CAPTURES);
    return true;
}

bool SDCardManager::isReady() const {
    return ready;
}

bool SDCardManager::ensureDir(const char* path) {
    if (!ready) return false;
    if (!SD.exists(path)) return SD.mkdir(path);
    return true;
}

void SDCardManager::printInfo() {
    if (!ready) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    uint8_t cardType = SD.cardType();
    const char* typeStr = "UNKNOWN";
    if      (cardType == CARD_MMC)  typeStr = "MMC";
    else if (cardType == CARD_SD)   typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

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
