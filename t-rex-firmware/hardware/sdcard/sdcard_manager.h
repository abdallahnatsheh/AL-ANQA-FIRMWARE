#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "display_manager.h"
#include <ff.h>

#define SD_DIR_CONFIG       "/config"
#define SD_DIR_CONFIG_NOTIF "/config/notification"
#define SD_DIR_APPS         "/apps"

// Per-app /apps/<tool> subfolders + files shared across multiple source files.
// (Tools that only reference their own folder define it locally instead.)
#define SD_DIR_TRACKME       "/apps/trackme"
#define SD_LOG_TRACKME       "/apps/trackme/session.csv"
#define SD_LOG_TRACKME_KNOWN "/apps/trackme/known.csv"
#define SD_CFG_SIGNATURES    "/apps/trackme/signatures.csv"

#define SD_DIR_EVILTWIN       "/apps/eviltwin"
#define SD_LOG_EVILTWIN_CREDS "/apps/eviltwin/creds.csv"
#define SD_DIR_EVILPORTAL     "/apps/eviltwin/portal"

#define SD_DIR_HIDDENSSID    "/apps/hiddenssid"
#define SD_LOG_HIDDEN_SSIDS  "/apps/hiddenssid/found.csv"

#define SD_DIR_WPASNIFF      "/apps/wpasniff"
#define SD_CFG_WORDLIST_WS   "/apps/wpasniff/wordlist.txt"
#define SD_LOG_CRACKED_WS    "/apps/wpasniff/cracked.csv"

#define SD_DIR_PMKID         "/apps/pmkid"
#define SD_CFG_WORDLIST_PM   "/apps/pmkid/wordlist.txt"
#define SD_LOG_CRACKED_PM    "/apps/pmkid/cracked.csv"

#define SD_DIR_WIFIMON       "/apps/wifimon"
#define SD_LOG_PROBES        "/apps/wifimon/probes.csv"

#define SD_DIR_WGUARD        "/apps/wguard"

#define SD_DIR_BEACONFLOOD   "/apps/beaconflood"
#define SD_CFG_WORDLIST_BCN  "/apps/beaconflood/wordlist.txt"

#define SD_DIR_BMON          "/apps/bmon"

#define SD_DIR_I2CSCAN       "/apps/i2cscan"
#define SD_LOG_I2CSCAN       "/apps/i2cscan/results.csv"

#define SD_DIR_FASTPAIR      "/apps/fastpair"
#define SD_DIR_ESPSNIFF      "/apps/espsniff"
#define SD_DIR_BLEINFO       "/apps/bleinfo"

#define SD_DIR_ESPCHAT       "/apps/espchat"
#define SD_DIR_ESPCHAT_PUB   "/apps/espchat/pub"
#define SD_DIR_ESPCHAT_PRV   "/apps/espchat/prv"

#define SD_DIR_BADUSB         "/apps/badusb"
#define SD_DIR_BADUSB_SCRIPTS "/apps/badusb/scripts"

#define SD_DIR_SSH            "/apps/ssh"            // ssh client — reserved for
#define SD_SSH_KNOWNHOSTS     "/apps/ssh/known_hosts" // host-key pinning (planned)
#define SD_SSH_HOSTS          "/apps/ssh/hosts.csv"   // saved host profiles (planned)

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
    void ensureAppsReadme();   // writes /apps/README.txt mapping folders to commands, if missing
    void ensureTreeStructure(); // creates the full /config + /apps/<tool> tree
};

#endif // SDCARD_MANAGER_H
