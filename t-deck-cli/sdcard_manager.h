#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "display_manager.h"
#include <ff.h>

#define SD_LOG_WIFI         "/logs/wifi.txt"
#define SD_LOG_BT           "/logs/bt.txt"
#define SD_LOG_PORTS        "/logs/ports.txt"
#define SD_LOG_PACKETS      "/logs/packets.txt"
#define SD_DIR_SCRIPTS      "/badusb"
#define SD_DIR_CAPTURES     "/captures"
#define SD_LOG_HIDDEN_SSIDS "/logs/hidden_ssids.csv"

class SDCardManager {
public:
    SDCardManager(DisplayManager& displayManager);
    bool begin();
    bool tryReinit();    // attempt hot-insert mount; no-op if already ready
    bool isReady() const;
    bool canAccessSD() const;   // false while USB MSC/HID is active
    void lockSD(bool lock);     // called by USBManager
    void printInfo();
    void listDirectory(const char* path);
    void readFile(const char* path);
    bool removeFile(const char* path);
    bool appendLine(const char* path, const String& line);
    bool ensureDir(const char* path);
    bool formatSDCard();
    bool performFormat();
    bool initializeTDeckStructure();
    void formatCommand(char* args);

    // CWD / path resolution
    const char* getCwd() const { return _cwd; }
    void resolvePath(const char* input, char* out, size_t outLen) const;
    void cdCommand(const char* path);

    // Autocomplete: list entries in searchDir whose names start with filePrefix.
    // Directories get a trailing '/'. Returns the number of matches written to out.
    int listCompletions(const char* searchDir, const char* filePrefix,
                        char out[][64], int maxCount,
                        bool dirsOnly = false, bool filesOnly = false);

private:
    DisplayManager& displayManager;
    bool ready;
    bool _sdLocked = false;
    char _cwd[128]  = {'/'};
    void listDirRecursive(File dir, int depth);
};

#endif // SDCARD_MANAGER_H
