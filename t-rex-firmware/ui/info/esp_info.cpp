#include "esp_info.h"
#include <SD.h>
#include <esp_system.h>
#include "mac_changer.h"
#include "lockscreen_manager.h"
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>   // IDF 5.x — esp_read_mac moved here
#endif

#ifdef BOARD_TDECK_PLUS
#include "gps_manager.h"
#endif

extern InputHandling inputHandler;

// ── Constructor ───────────────────────────────────────────────────────────────

ESPInfoPrinter::ESPInfoPrinter(DisplayManager& dm)
    : displayManager(dm), batteryManager(dm) {}

// ── Static display helpers ────────────────────────────────────────────────────

static void iRow(DisplayManager& dm, const char* lbl, const char* val, uint16_t col = TFT_WHITE) {
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.printText(lbl);
    dm.setTextColor(col);
    dm.println(val);
}

static void iSep(DisplayManager& dm) {
    dm.printSeparator();
}

static void iHeader(DisplayManager& dm, const char* name, int page, int pages) {
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(10, outputY);
    char pgBuf[8]; snprintf(pgBuf, sizeof(pgBuf), "%02d/%02d", page + 1, pages);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("INFO");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText(name);
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    dm.setTextColor(0x7BEF);     dm.println(pgBuf);
    iSep(dm);
}

static void fmtMac(const uint8_t* m, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

// ── Page 0: System ────────────────────────────────────────────────────────────

void ESPInfoPrinter::drawPageSystem() {
    DisplayManager& dm = displayManager;
    char buf[40];

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    const char* model;
    switch (chip.model) {
        case CHIP_ESP32S3: model = "ESP32-S3"; break;
        case CHIP_ESP32S2: model = "ESP32-S2"; break;
        case CHIP_ESP32C3: model = "ESP32-C3"; break;
        case CHIP_ESP32:   model = "ESP32";    break;
        default:           model = "Unknown";  break;
    }
    iRow(dm, "Chip    ", model);

    snprintf(buf, sizeof(buf), "v%d", chip.revision);
    iRow(dm, "Rev     ", buf);

    snprintf(buf, sizeof(buf), "%d cores", chip.cores);
    iRow(dm, "CPU     ", buf);

    snprintf(buf, sizeof(buf), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
    iRow(dm, "Freq    ", buf);

    snprintf(buf, sizeof(buf), "%u MB", (unsigned)(ESP.getFlashChipSize() >> 20));
    iRow(dm, "Flash   ", buf);

    uint32_t psram = ESP.getPsramSize();
    if (psram > 0) snprintf(buf, sizeof(buf), "%u MB", (unsigned)(psram >> 20));
    else           strcpy(buf, "none");
    iRow(dm, "PSRAM   ", buf, psram > 0 ? TFT_WHITE : 0x7BEF);

    uint32_t freeH  = ESP.getFreeHeap();
    uint32_t totalH = ESP.getHeapSize();
    snprintf(buf, sizeof(buf), "%uK free / %uK total", freeH >> 10, totalH >> 10);
    iRow(dm, "Heap    ", buf, freeH > 50000 ? TFT_GREEN : TFT_YELLOW);

    uint32_t s = millis() / 1000;
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;
    snprintf(buf, sizeof(buf), "%uh %02um %02us", h, m, s);
    iRow(dm, "Uptime  ", buf);
}

// ── Page 1: Radio ─────────────────────────────────────────────────────────────

void ESPInfoPrinter::drawPageRadio() {
    DisplayManager& dm = displayManager;
    char buf[40];
    uint8_t mac[6];
    char    macStr[18];

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    fmtMac(mac, macStr);
    iRow(dm, "STA MAC ", macStr);
    if (MacChanger::getInstance().isEnabled()) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        dm.println(MacChanger::getInstance().isCustom() ? "  ^ CUSTOM MAC" : "  ^ RANDOMIZED");
        dm.setTextColor(TFT_WHITE);
    }

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    fmtMac(mac, macStr);
    iRow(dm, "AP MAC  ", macStr);

    esp_read_mac(mac, ESP_MAC_BT);
    fmtMac(mac, macStr);
    iRow(dm, "BT MAC  ", macStr);

    iSep(dm);

    wifi_mode_t wmode = WiFi.getMode();
    const char* modeStr;
    switch (wmode) {
        case WIFI_MODE_STA:   modeStr = "STA";    break;
        case WIFI_MODE_AP:    modeStr = "AP";     break;
        case WIFI_MODE_APSTA: modeStr = "AP+STA"; break;
        default:              modeStr = "OFF";    break;
    }
    iRow(dm, "WiFi    ", modeStr);

    bool conn = (WiFi.status() == WL_CONNECTED);

    iRow(dm, "SSID    ",
         conn ? WiFi.SSID().c_str() : "---",
         conn ? TFT_GREEN : 0x7BEF);

    iRow(dm, "IP      ",
         conn ? WiFi.localIP().toString().c_str() : "---",
         conn ? TFT_GREEN : 0x7BEF);

    if (conn) {
        int rssi = WiFi.RSSI();
        uint16_t rc = rssi >= -60 ? TFT_GREEN : (rssi >= -75 ? TFT_YELLOW : TFT_RED);
        snprintf(buf, sizeof(buf), "%d dBm  ch%d", rssi, WiFi.channel());
        iRow(dm, "RSSI    ", buf, rc);
    } else {
        iRow(dm, "RSSI    ", "---", 0x7BEF);
    }
}

// ── Page 2: Hardware ──────────────────────────────────────────────────────────

void ESPInfoPrinter::drawPageHardware() {
    DisplayManager& dm = displayManager;
    char buf[48];

    // Battery
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.printText("Battery ");
    batteryManager.printBatteryInfo();

    iSep(dm);

    // SD card
    uint8_t ct = SD.cardType();
    if (ct == CARD_NONE) {
        iRow(dm, "SD      ", "not present", TFT_RED);
    } else {
        const char* typeStr;
        switch (ct) {
            case CARD_MMC:  typeStr = "MMC";  break;
            case CARD_SD:   typeStr = "SD";   break;
            case CARD_SDHC: typeStr = "SDHC"; break;
            default:        typeStr = "UNKN"; break;
        }
        uint32_t totalMB = (uint32_t)(SD.totalBytes() >> 20);
        snprintf(buf, sizeof(buf), "%s  %u MB total", typeStr, totalMB);
        iRow(dm, "SD      ", buf);
    }

    iSep(dm);

    // LoRa (SX1262 wired but RadioLib not yet initialised)
    iRow(dm, "LoRa    ", "SX1262", 0x7BEF);
    snprintf(buf, sizeof(buf), "CS:%d RST:%d DIO1:%d BUSY:%d",
             RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN, RADIO_BUSY_PIN);
    iRow(dm, "  Pins  ", buf, 0x7BEF);

    iSep(dm);

    // GPS
#ifdef BOARD_TDECK_PLUS
    GpsManager& gps = GpsManager::instance();
    if (!gps.isRunning()) {
        iRow(dm, "GPS     ", "off  (run: gps on)", TFT_YELLOW);
    } else if (!gps.isValid()) {
        snprintf(buf, sizeof(buf), "searching  sats:%u  mod:%s",
                 gps.satellites(), gps.moduleName());
        iRow(dm, "GPS     ", buf, TFT_YELLOW);
    } else {
        snprintf(buf, sizeof(buf), "%.6f  %.6f", (double)gps.lat(), (double)gps.lon());
        iRow(dm, "GPS     ", buf, TFT_GREEN);
        snprintf(buf, sizeof(buf), "sats:%u  %02u:%02u:%02u UTC",
                 gps.satellites(), gps.hour(), gps.minute(), gps.second());
        iRow(dm, "        ", buf, TFT_GREEN);
    }
#else
    iRow(dm, "GPS     ", "T-Deck Plus only", 0x7BEF);
#endif
}

// ── Entry point ───────────────────────────────────────────────────────────────

void ESPInfoPrinter::printESPInfo() {
    static const int PAGES = 3;
    static const char* const TITLES[PAGES] = {
        "SYS", "RADIO", "HW"
    };

    int  page   = 0;
    bool redraw = true;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;

        if (redraw) {
            iHeader(displayManager, TITLES[page], page, PAGES);
            switch (page) {
                case 0: drawPageSystem();   break;
                case 1: drawPageRadio();    break;
                case 2: drawPageHardware(); break;
            }
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("[a]prev [l]next [q]quit");
            redraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (!k) continue;
        if (k == 'q' || k == 'Q') break;
        if ((k == 'l' || k == 'L') && page < PAGES - 1) { page++; redraw = true; }
        if ((k == 'a' || k == 'A') && page > 0)          { page--; redraw = true; }
    }

    displayManager.printCommandScreen();
}
