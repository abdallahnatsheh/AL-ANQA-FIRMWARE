#include "sdcard_manager.h"
#include "utilities.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include <vector>

extern InputHandling inputHandler;

SDCardManager::SDCardManager(DisplayManager& displayManager)
    : displayManager(displayManager), ready(false) {}

bool SDCardManager::begin() {
    // RADIO_CS_PIN (GPIO9) shares SPI2 with the SD card (GPIO39).
    // If the LoRa radio CS is low or floating it drives MISO simultaneously,
    // corrupting every SD transaction and causing mount to fail.
    pinMode(RADIO_CS_PIN, OUTPUT);
    digitalWrite(RADIO_CS_PIN, HIGH);
    delay(10);

    // Retry up to 3 times — transient failures are common after heavy WiFi/BLE
    // ops (GDMA shared with SPI2 on ESP32-S3) or after any interrupted SPI transaction.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (SD.begin(BOARD_SDCARD_CS, SPI, 4000000)) {
            // Set ready BEFORE ensureDir — ensureDir gates on this flag
            ready = true;
            ensureTreeStructure();
            return true;
        }
        SD.end();
        delay(200);
    }
    ready = false;
    return false;
}

bool SDCardManager::isReady() const { return ready; }
void SDCardManager::lockSD(bool lock) { _sdLocked = lock; }
bool SDCardManager::canAccessSD() const { return ready && !_sdLocked; }

bool SDCardManager::ensureDir(const char* path) {
    if (!canAccessSD()) return false;
    if (SD.exists(path)) {
        File f = SD.open(path);
        if (!f) return false;
        bool isDir = f.isDirectory();
        f.close();
        return isDir;
    }
    return SD.mkdir(path);
}

// Writes a one-time map of /apps/<folder> -> owning command, so files are
// identifiable when the SD card is browsed on a PC. Never overwritten.
void SDCardManager::ensureAppsReadme() {
    if (!canAccessSD()) return;
    if (SD.exists("/apps/README.txt")) return;
    File f = SD.open("/apps/README.txt", FILE_WRITE);
    if (!f) return;
    f.println("T-Rex Firmware -- /apps folder map");
    f.println("====================================");
    f.println("Each subfolder holds the data + logs of one command.");
    f.println("");
    f.println("badusb/     ux          DuckyScript payloads in badusb/scripts/");
    f.println("beaconflood/ bf         custom SSID list (wordlist.txt)");
    f.println("bleinfo/    bi          GATT enum dumps + sniff captures (<mac>.txt)");
    f.println("bmon/       bm          BLE advertisement logs (NNN.csv)");
    f.println("espchat/    ec          contacts.csv, config.conf, pub/, prv/ chat logs");
    f.println("espsniff/   es          ESP-NOW captures (NNN.csv + NNN.pcap)");
    f.println("eviltwin/   et          creds.csv + custom portal HTML in portal/");
    f.println("fastpair/   fp          FastPair keys/pairings/sniff log");
    f.println("hiddenssid/ hs          discovered hidden SSIDs (found.csv)");
    f.println("i2cscan/    isc         I2C bus scan results (results.csv)");
    f.println("pmkid/      pm          PMKID captures (.cap), wordlist.txt, cracked.csv");
    f.println("trackme/    tm          session log, whitelist, custom signatures.csv");
    f.println("wguard/     wg          WiFi IDS session logs (NNN.csv)");
    f.println("wifimon/    wm          raw 802.11 PCAP captures + probe log");
    f.println("wpasniff/   ws          WPA handshake captures (.cap), wordlist.txt,");
    f.println("                        cracked.csv");
    f.println("");
    f.println("/config/ holds device-wide settings: pwrsave, macchanger,");
    f.println("lockscreen, notif, clock; /config/notification/ holds alert MP3s.");
    f.println("");
    f.println("/wpa_supplicant.conf holds saved WiFi credentials.");
    f.close();
}

// Creates the full /config + /apps/<tool> directory tree on first boot/format.
// Idempotent — ensureDir() is a no-op if the directory already exists.
void SDCardManager::ensureTreeStructure() {
    if (!canAccessSD()) return;

    ensureDir(SD_DIR_CONFIG);
    ensureDir(SD_DIR_CONFIG_NOTIF);

    ensureDir(SD_DIR_APPS);
    ensureDir(SD_DIR_TRACKME);
    ensureDir(SD_DIR_EVILTWIN);
    ensureDir(SD_DIR_EVILPORTAL);
    ensureDir(SD_DIR_HIDDENSSID);
    ensureDir(SD_DIR_WPASNIFF);
    ensureDir(SD_DIR_PMKID);
    ensureDir(SD_DIR_WIFIMON);
    ensureDir(SD_DIR_WGUARD);
    ensureDir(SD_DIR_BEACONFLOOD);
    ensureDir(SD_DIR_BMON);
    ensureDir(SD_DIR_I2CSCAN);
    ensureDir(SD_DIR_FASTPAIR);
    ensureDir(SD_DIR_ESPSNIFF);
    ensureDir(SD_DIR_BLEINFO);
    ensureDir(SD_DIR_ESPCHAT);
    ensureDir(SD_DIR_ESPCHAT_PUB);
    ensureDir(SD_DIR_ESPCHAT_PRV);
    ensureDir(SD_DIR_BADUSB);
    ensureDir(SD_DIR_BADUSB_SCRIPTS);

    ensureAppsReadme();
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

void SDCardManager::resolvePath(const char* input, char* out, size_t outLen) const {
    if (!input || !*input) {
        strncpy(out, _cwd, outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }
    char tmp[256];
    if (input[0] == '/') {
        strncpy(tmp, input, sizeof(tmp) - 1);
    } else {
        if (_cwd[1] == '\0')  // _cwd is "/"
            snprintf(tmp, sizeof(tmp), "/%s", input);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", _cwd, input);
    }
    tmp[sizeof(tmp) - 1] = '\0';

    // Normalize ".." and "." segments
    char parts[12][64];
    int  n = 0;
    char* tok = strtok(tmp, "/");
    while (tok) {
        if (strcmp(tok, "..") == 0) {
            if (n > 0) n--;
        } else if (strcmp(tok, ".") != 0 && *tok) {
            if (n < 12) { strncpy(parts[n++], tok, 63); parts[n-1][63] = '\0'; }
        }
        tok = strtok(nullptr, "/");
    }
    if (n == 0) {
        strncpy(out, "/", outLen - 1);
    } else {
        out[0] = '\0';
        for (int i = 0; i < n; i++) {
            strncat(out, "/", outLen - strlen(out) - 1);
            strncat(out, parts[i], outLen - strlen(out) - 1);
        }
    }
    out[outLen - 1] = '\0';
}

void SDCardManager::cdCommand(const char* path) {
    if (!canAccessSD()) {
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }
    char resolved[128];
    if (!path || !*path || strcmp(path, "/") == 0) {
        strncpy(resolved, "/", sizeof(resolved) - 1);
    } else {
        resolvePath(path, resolved, sizeof(resolved));
    }
    File f = SD.open(resolved);
    if (!f || !f.isDirectory()) {
        if (f) f.close();
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.printText("Not a dir: ");
        displayManager.println(resolved);
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }
    f.close();
    strncpy(_cwd, resolved, sizeof(_cwd) - 1);
    _cwd[sizeof(_cwd) - 1] = '\0';
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println(_cwd);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
}

int SDCardManager::listCompletions(const char* searchDir, const char* filePrefix,
                                   char out[][64], int maxCount,
                                   bool dirsOnly, bool filesOnly) {
    if (!canAccessSD()) return 0;
    File dir = SD.open(searchDir);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    int count  = 0;
    size_t fpLen = strlen(filePrefix);
    while (count < maxCount) {
        File entry = dir.openNextFile();
        if (!entry) break;
        const char* ename = entry.name();
        bool entryIsDir = entry.isDirectory();
        // Apply type filter
        if (dirsOnly  && !entryIsDir) { entry.close(); continue; }
        if (filesOnly &&  entryIsDir) { entry.close(); continue; }
        if (fpLen == 0 || strncmp(ename, filePrefix, fpLen) == 0) {
            strncpy(out[count], ename, 63);
            out[count][63] = '\0';
            if (entryIsDir) strncat(out[count], "/", 63 - strlen(out[count]));
            count++;
        }
        entry.close();
    }
    dir.close();
    return count;
}

void SDCardManager::listDirRecursive(File dir, int depth) {
    // kept for internal use only — listDirectory no longer calls this
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        entry.close();
    }
}

void SDCardManager::listDirectory(const char* path) {
    if (!canAccessSD()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    // Resolve path
    char resolved[128];
    if (!path || !*path) {
        strncpy(resolved, _cwd, sizeof(resolved) - 1);
    } else {
        resolvePath(path, resolved, sizeof(resolved));
    }
    resolved[sizeof(resolved) - 1] = '\0';

    File root = SD.open(resolved);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Not found: ");
        displayManager.println(resolved);
        displayManager.printCommandScreen();
        return;
    }

    // Header
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);    displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);  displayManager.printText("SD");
    displayManager.setTextColor(0x7BEF);    displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW);displayManager.printText("LS");
    displayManager.setTextColor(0x7BEF);    displayManager.println("]");
    displayManager.printSeparator();

    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println(resolved);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printSeparator();

    // List one level (non-recursive), paginated
    const int perPage = 8;
    int lineOnPage = 0;
    bool quit = false;

    while (!quit) {
        File entry = root.openNextFile();
        if (!entry) break;

        const char* ename = entry.name();
        bool isDir = entry.isDirectory();

        // Page break before printing if page is full
        if (lineOnPage == perPage) {
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(0x4208);
            displayManager.println("-- any key / q --");
            displayManager.setTextColor(TFT_WHITE);
            char k = 0;
            while (k == 0) {
                k = inputHandler.getKeyboardInput();
                if (k == 0 && LockScreenManager::getInstance().consumeJustUnlocked()) {
                    // Unlock cleared the screen — redraw the pause indicator
                    displayManager.clearScreen();
                    displayManager.setCursor(10, outputY);
                    displayManager.setTextColor(0x7BEF);    displayManager.printText("[");
                    displayManager.setTextColor(TFT_CYAN);  displayManager.printText("SD::LS");
                    displayManager.setTextColor(0x7BEF);    displayManager.println("] unlocked — any key / q");
                    displayManager.setTextColor(TFT_WHITE);
                }
            }
            if (k == 'q' || k == 'Q') { entry.close(); quit = true; break; }
            // Redraw header for next page
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(0x7BEF);    displayManager.printText("[");
            displayManager.setTextColor(TFT_CYAN);  displayManager.printText("SD");
            displayManager.setTextColor(0x7BEF);    displayManager.printText("::");
            displayManager.setTextColor(TFT_YELLOW);displayManager.printText("LS");
            displayManager.setTextColor(0x7BEF);    displayManager.println("]");
            displayManager.printSeparator();
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_YELLOW); displayManager.println(resolved);
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printSeparator();
            lineOnPage = 0;
        }

        displayManager.setCursor(10, displayManager.getCursorY());
        if (isDir) {
            displayManager.setTextColor(TFT_CYAN);
            displayManager.printText(ename);
            displayManager.println("/");
            displayManager.setTextColor(TFT_WHITE);
        } else {
            uint32_t sz = entry.size();
            char line[48];
            if (sz < 1024)
                snprintf(line, sizeof(line), "%-24s %uB", ename, (unsigned)sz);
            else
                snprintf(line, sizeof(line), "%-24s %uKB", ename, (unsigned)(sz / 1024));
            displayManager.println(line);
        }
        entry.close();
        lineOnPage++;
    }
    root.close();
    if (!quit) {
        displayManager.printSeparator();
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x4208); displayManager.println("[q] back");
        displayManager.setTextColor(TFT_WHITE);
    }
    displayManager.printCommandScreen();
}

void SDCardManager::readFile(const char* path) {
    if (!canAccessSD()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    char resolved[128];
    resolvePath(path, resolved, sizeof(resolved));
    path = resolved;

    File file = SD.open(path);
    if (!file || file.isDirectory()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.printText("Not found: ");
        displayManager.println(path);
        displayManager.printCommandScreen();
        if (file) file.close();
        return;
    }

    // ── Read all lines into RAM (capped at 400) ───────────────────────────────
    const int MAX_LINES = 400;
    std::vector<String> lines;
    lines.reserve(32);
    while (file.available() && (int)lines.size() < MAX_LINES) {
        String s = file.readStringUntil('\n');
        // strip Windows \r
        if (s.length() > 0 && s[s.length() - 1] == '\r') s.remove(s.length() - 1);
        lines.push_back(s);
    }
    bool truncated = file.available();
    file.close();

    if (lines.empty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("(empty file)");
        displayManager.printCommandScreen();
        return;
    }

    // ── Viewer state ──────────────────────────────────────────────────────────
    const int CAT_VISIBLE = 9;
    int total     = (int)lines.size();
    int scrollTop = 0;

    // Build short display name (truncate if > 20 chars)
    const char* fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    char fdisp[24];
    if (strlen(fname) > 20) {
        strncpy(fdisp, fname, 17); fdisp[17] = '\0'; strcat(fdisp, "...");
    } else {
        strncpy(fdisp, fname, sizeof(fdisp) - 1); fdisp[sizeof(fdisp) - 1] = '\0';
    }

    bool needsRedraw = true;

    while (true) {
        // Restore viewer after unlock
        if (LockScreenManager::getInstance().consumeJustUnlocked()) needsRedraw = true;

        if (needsRedraw && !displayManager.isBlocked()) {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);

            // ── Header ───────────────────────────────────────────────────────
            displayManager.setTextColor(0x7BEF);    displayManager.printText("[");
            displayManager.setTextColor(TFT_CYAN);  displayManager.printText("CAT");
            displayManager.setTextColor(0x7BEF);    displayManager.printText("] ");
            displayManager.setTextColor(TFT_YELLOW);displayManager.printText(fdisp);
            char lnbuf[20];
            snprintf(lnbuf, sizeof(lnbuf), truncated ? "  %d+ ln" : "  %d ln", total);
            displayManager.setTextColor(0x7BEF);    displayManager.println(lnbuf);
            displayManager.printSeparator();

            int contentTop = displayManager.getCursorY();
            int y          = contentTop;

            // ── Content lines ─────────────────────────────────────────────────
            for (int i = scrollTop; i < scrollTop + CAT_VISIBLE && i < total; i++) {
                displayManager.setCursor(10, y);
                displayManager.setTextColor(TFT_WHITE);
                displayManager.println(lines[i].c_str());
                y += LINE_HEIGHT;
            }

            // ── Scrollbar ─────────────────────────────────────────────────────
            if (total > CAT_VISIBLE) {
                int barX   = SCREEN_WIDTH - 5;
                int barH   = CAT_VISIBLE * LINE_HEIGHT;
                int thumbH = max(4, barH * CAT_VISIBLE / total);
                int maxTop = total - CAT_VISIBLE;
                int thumbY = contentTop + (maxTop > 0
                                ? (barH - thumbH) * scrollTop / maxTop : 0);
                displayManager.fillRect(barX, contentTop, 3, barH,   0x2104);
                displayManager.fillRect(barX, thumbY,     3, thumbH, TFT_CYAN);
            }

            // ── Nav bar ───────────────────────────────────────────────────────
            int navY = contentTop + CAT_VISIBLE * LINE_HEIGHT + 2;
            displayManager.fillRect(5, navY, 310, 1, TFT_CYAN);
            displayManager.setCursor(10, navY + 3);
            displayManager.setTextColor(0x7BEF);    displayManager.printText("tpad ");
            displayManager.setTextColor(TFT_GREEN); displayManager.printText("UP/DN");
            displayManager.setTextColor(0x7BEF);    displayManager.printText(" scroll  [");
            displayManager.setTextColor(TFT_GREEN); displayManager.printText("q");
            displayManager.setTextColor(0x7BEF);    displayManager.printText("] quit");
            displayManager.setTextColor(TFT_WHITE);

            needsRedraw = false;
        }

        char           k   = inputHandler.getKeyboardInput();
        TrackballEvent evt = inputHandler.getTrackballEvent();

        if (k == 'q' || k == 'Q') { displayManager.clearInputText(); return; }
        if (evt == TBALL_UP   && scrollTop > 0)                   { scrollTop--; needsRedraw = true; }
        if (evt == TBALL_DOWN && scrollTop < total - CAT_VISIBLE) { scrollTop++; needsRedraw = true; }
    }
}

bool SDCardManager::removeFile(const char* path) {
    if (!canAccessSD()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return false;
    }

    char resolved[128];
    resolvePath(path, resolved, sizeof(resolved));
    path = resolved;

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
    if (!canAccessSD()) return false;
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
    ensureTreeStructure();
    return true;
}

bool SDCardManager::formatSDCard() {
    if (!canAccessSD()) {
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

    static const char* dirs[] = {
        SD_DIR_CONFIG, SD_DIR_CONFIG_NOTIF,
        SD_DIR_APPS, SD_DIR_TRACKME, SD_DIR_EVILTWIN, SD_DIR_EVILPORTAL,
        SD_DIR_HIDDENSSID, SD_DIR_WPASNIFF, SD_DIR_PMKID, SD_DIR_WIFIMON,
        SD_DIR_WGUARD, SD_DIR_BEACONFLOOD, SD_DIR_BMON, SD_DIR_I2CSCAN,
        SD_DIR_FASTPAIR, SD_DIR_ESPSNIFF, SD_DIR_BLEINFO, SD_DIR_ESPCHAT,
        SD_DIR_ESPCHAT_PUB, SD_DIR_ESPCHAT_PRV, SD_DIR_BADUSB, SD_DIR_BADUSB_SCRIPTS
    };
    bool ok = true;
    for (const char* d : dirs) {
        if (!ensureDir(d)) {
            displayManager.printText("Failed: ");
            displayManager.println(d);
            ok = false;
        }
    }

    if (!ok) {
        displayManager.printCommandScreen();
        return false;
    }

    ensureAppsReadme();

    if (!SD.exists("/config/pwrsave.conf")) {
        File f = SD.open("/config/pwrsave.conf", FILE_WRITE);
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
            displayManager.println("Created /config/pwrsave.conf");
        }
    }

    if (!SD.exists("/config/macchanger.conf")) {
        File f = SD.open("/config/macchanger.conf", FILE_WRITE);
        if (f) {
            f.println("# macchanger.conf — generated by T-Rex");
            f.println("# MAC address changer configuration (key=value)");
            f.println("enabled=false");
            f.println("mode=random");
            f.println("restore_on_exit=true");
            f.println("target=wifi");
            f.close();
            displayManager.println("Created /config/macchanger.conf");
        }
    }

    if (!SD.exists(SD_CFG_SIGNATURES)) {
        File f = SD.open(SD_CFG_SIGNATURES, FILE_WRITE);
        if (f) {
            f.println("Name,Signature,Confidence");
            f.println("# Add custom BLE tracker signatures below");
            f.close();
            displayManager.println("Created " SD_CFG_SIGNATURES);
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
