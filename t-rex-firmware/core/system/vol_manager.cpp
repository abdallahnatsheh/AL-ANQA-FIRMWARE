#include "vol_manager.h"
#include "display_manager.h"
#include <SD.h>

#define VOL_CONF "/config/vol.conf"

extern DisplayManager displayManager;

static uint8_t s_vol = 70;

static void saveConf() {
    File f = SD.open(VOL_CONF, FILE_WRITE);
    if (!f) return;
    f.printf("vol=%d\n", s_vol);
    f.close();
}

void loadVolConf() {
    File f = SD.open(VOL_CONF, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("vol=")) {
            int v = line.substring(4).toInt();
            if (v >= 0 && v <= 100) s_vol = (uint8_t)v;
        }
    }
    f.close();
}

uint8_t getMasterVolume() { return s_vol; }

void setMasterVolume(uint8_t v) {
    if (v > 100) v = 100;
    if (v == s_vol) return;
    s_vol = v;
    saveConf();
}

void volCmd(char* args) {
    if (args && *args) {
        uint8_t prev = s_vol;
        if      (strcmp(args, "up")   == 0) s_vol = min(100, (int)s_vol + 10);
        else if (strcmp(args, "down") == 0) s_vol = s_vol > 10 ? s_vol - 10 : 0;
        else if (strcmp(args, "off")  == 0) s_vol = 0;
        else { int v = atoi(args); if (v >= 0 && v <= 100) s_vol = (uint8_t)v; }
        if (s_vol != prev) saveConf();
    }
    displayManager.setTextColor(0x7BEF); displayManager.printText("Volume  ");
    displayManager.setTextColor(TFT_WHITE);
    char b[16]; snprintf(b, sizeof(b), "%d%%", s_vol); displayManager.println(b);
    displayManager.printCommandScreen();
}
