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
#define SD_DIR_SCRIPTS      "/scripts"
#define SD_DIR_CAPTURES     "/captures"
#define SD_LOG_HIDDEN_SSIDS "/logs/hidden_ssids.csv"

class SDCardManager {
public:
    SDCardManager(DisplayManager& displayManager);
    bool begin();
    bool isReady() const;
    void printInfo();
    void listDirectory(const char* path);
    void readFile(const char* path);
    bool removeFile(const char* path);
    bool appendLine(const char* path, const String& line);
    bool ensureDir(const char* path);
    void formatSDCard();
    bool performFormat();
    bool initializeTDeckStructure();
    void formatCommand(char* args);

private:
    DisplayManager& displayManager;
    bool ready;
    void listDirRecursive(File dir, int depth);
};

#endif // SDCARD_MANAGER_H
