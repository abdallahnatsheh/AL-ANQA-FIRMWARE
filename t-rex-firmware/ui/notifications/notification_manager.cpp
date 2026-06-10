#include "notification_manager.h"
#include "powersave_manager.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include <SD.h>
#include <driver/i2s.h>
#include <math.h>

// Pin fallbacks — audio lib headers may shadow the utilities.h defines
#ifndef BOARD_I2S_BCK
#  define BOARD_I2S_BCK   7
#  define BOARD_I2S_WS    5
#  define BOARD_I2S_DOUT  6
#endif

#define NM_I2S_PORT  I2S_NUM_0
#define NM_SR        22050

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;

static NotificationManager* s_nm = nullptr;

NotificationManager& NotificationManager::getInstance() {
    if (!s_nm) s_nm = new NotificationManager();
    return *s_nm;
}

void NotificationManager::begin() {
    memset(_mp3File, 0, sizeof(_mp3File));
    loadConfig();
}

// ── Default tone engine ───────────────────────────────────────────────────────

// freq=0 in sequence = silent gap
void NotificationManager::playTones(const int* freqs, const int* durs, int count) {
    if (_notifVol == 0) return;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = NM_SR;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 128;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    if (i2s_driver_install(NM_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = BOARD_I2S_BCK;
    pins.ws_io_num    = BOARD_I2S_WS;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(NM_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(NM_I2S_PORT);
        return;
    }

    int16_t amp = (int16_t)(_notifVol / 100.0f * 22000.0f);

    for (int t = 0; t < count; t++) {
        int freq = freqs[t];
        int dur  = durs[t];
        if (freq == 0) {
            vTaskDelay(pdMS_TO_TICKS(dur));
            continue;
        }
        int cycLen = NM_SR / freq;
        if (cycLen > 256) cycLen = 256;
        int16_t buf[512];
        for (int i = 0; i < cycLen; i++) {
            int16_t v = (int16_t)(amp * sinf(2.0f * M_PI * i / cycLen));
            buf[i * 2] = v; buf[i * 2 + 1] = v;
        }
        size_t wr;
        uint32_t t0 = millis();
        while ((int32_t)(millis() - t0) < dur)
            i2s_write(NM_I2S_PORT, buf, (size_t)cycLen * 4, &wr, pdMS_TO_TICKS(50));
        i2s_zero_dma_buffer(NM_I2S_PORT);
    }

    i2s_driver_uninstall(NM_I2S_PORT);
}

// ── WAV playback (no external library — raw PCM streamed to I2S) ─────────────
// Supports 8/16-bit PCM WAV, mono or stereo, any sample rate.
// Convert sounds with: ffmpeg -i input.mp3 -ar 22050 -ac 1 -acodec pcm_s16le out.wav

struct WavHeader {
    char     riff[4];       // "RIFF"
    uint32_t fileSize;
    char     wave[4];       // "WAVE"
    char     fmt[4];        // "fmt "
    uint32_t fmtSize;
    uint16_t audioFormat;   // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

bool NotificationManager::playWav(const char* path) {
    if (!SD.exists(path)) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    WavHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) { f.close(); return false; }
    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4)) { f.close(); return false; }
    if (hdr.audioFormat != 1) { f.close(); return false; } // PCM only

    // Skip to "data" chunk
    char chunkId[4]; uint32_t chunkSize;
    while (f.available()) {
        if (f.read((uint8_t*)chunkId, 4) != 4) break;
        if (f.read((uint8_t*)&chunkSize, 4) != 4) break;
        if (memcmp(chunkId, "data", 4) == 0) break;
        f.seek(f.position() + chunkSize); // skip non-data chunks
    }
    if (!f.available()) { f.close(); return false; }

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = hdr.sampleRate;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)hdr.bitsPerSample;
    cfg.channel_format       = (hdr.numChannels == 1)
                                ? I2S_CHANNEL_FMT_ONLY_LEFT
                                : I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    if (i2s_driver_install(NM_I2S_PORT, &cfg, 0, NULL) != ESP_OK) { f.close(); return false; }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = BOARD_I2S_BCK;
    pins.ws_io_num    = BOARD_I2S_WS;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(NM_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(NM_I2S_PORT);
        f.close();
        return false;
    }

    // Apply volume by scaling samples
    float gain = _notifVol / 100.0f;
    uint8_t buf[512];
    size_t  wr;
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        // Scale 16-bit samples by gain
        if (hdr.bitsPerSample == 16) {
            int16_t* s = (int16_t*)buf;
            for (int i = 0; i < n / 2; i++) s[i] = (int16_t)(s[i] * gain);
        }
        i2s_write(NM_I2S_PORT, buf, (size_t)n, &wr, pdMS_TO_TICKS(100));
    }

    i2s_zero_dma_buffer(NM_I2S_PORT);
    i2s_driver_uninstall(NM_I2S_PORT);
    f.close();
    return true;
}

// ── Public notify ─────────────────────────────────────────────────────────────

void NotificationManager::notify(NotifLevel level) {
    if (!_enabled[level]) return;
    if (_wakeCallback) _wakeCallback();

    // Try custom WAV from SD first; fall back to built-in tones
    if (_mp3File[level][0] && playWav(_mp3File[level])) return;

    switch (level) {
        case NOTIF_ALERT: {
            const int fr[] = {1000, 0, 1000, 0, 1000};
            const int du[] = { 150,50,  150,50,  150};
            playTones(fr, du, 5);
            break;
        }
        case NOTIF_WARNING: {
            const int fr[] = {700, 0, 700};
            const int du[] = {150,50, 150};
            playTones(fr, du, 3);
            break;
        }
        case NOTIF_SUCCESS: {
            const int fr[] = {500, 650, 800};
            const int du[] = {100, 100, 150};
            playTones(fr, du, 3);
            break;
        }
        case NOTIF_INFO: {
            const int fr[] = {500};
            const int du[] = {200};
            playTones(fr, du, 1);
            break;
        }
        case NOTIF_PING: {
            const int fr[] = {600};
            const int du[] = {100};
            playTones(fr, du, 1);
            break;
        }
    }
}

void NotificationManager::setNotifVol(uint8_t vol) {
    _notifVol = vol > 100 ? 100 : vol;
}

void NotificationManager::enable(NotifLevel level, bool on) {
    _enabled[level] = on;
}

void NotificationManager::enableAll(bool on) {
    for (int i = 0; i < NOTIF_COUNT; i++) _enabled[i] = on;
}

// ── SD config (/config/notif.conf, key=value) ────────────────────────────────

static const char* NM_CONFIG   = "/config/notif.conf";
static const char* NM_NOTIF_DIR = SD_DIR_CONFIG_NOTIF;

static const char* levelName(int i) {
    switch (i) {
        case NOTIF_ALERT:   return "alert";
        case NOTIF_WARNING: return "warning";
        case NOTIF_SUCCESS: return "success";
        case NOTIF_INFO:    return "info";
        case NOTIF_PING:    return "ping";
    }
    return "";
}

void NotificationManager::loadConfig() {
    File f = SD.open(NM_CONFIG, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim(); val.trim();

        if (key == "notif_volume") {
            _notifVol = (uint8_t)constrain(val.toInt(), 0, 100);
        } else if (key.endsWith("_file")) {
            String lvlName = key.substring(0, key.length() - 5);  // strip "_file"
            for (int i = 0; i < NOTIF_COUNT; i++) {
                if (lvlName == levelName(i)) {
                    // Prepend /config/notification/ if path has no slash
                    if (val.length() > 0 && val[0] != '/') {
                        String full = String(NM_NOTIF_DIR) + "/" + val;
                        strncpy(_mp3File[i], full.c_str(), sizeof(_mp3File[i]) - 1);
                    } else {
                        strncpy(_mp3File[i], val.c_str(), sizeof(_mp3File[i]) - 1);
                    }
                    break;
                }
            }
        } else {
            for (int i = 0; i < NOTIF_COUNT; i++) {
                if (key == levelName(i))
                    _enabled[i] = (val == "on" || val == "1");
            }
        }
    }
    f.close();
}

void NotificationManager::saveConfig() {
    sdCardManager.ensureDir("/config");
    File f = SD.open(NM_CONFIG, FILE_WRITE);
    if (!f) return;
    f.printf("notif_volume=%d\n", _notifVol);
    for (int i = 0; i < NOTIF_COUNT; i++) {
        f.printf("%s=%s\n", levelName(i), _enabled[i] ? "on" : "off");
        if (_mp3File[i][0])
            f.printf("%s_file=%s\n", levelName(i), _mp3File[i]);
    }
    f.close();
}

// ── Status display ────────────────────────────────────────────────────────────

void NotificationManager::printStatus() {
    NotificationManager& nm = getInstance();

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("Notifications");
    displayManager.setTextColor(0x4208);
    displayManager.println("─────────────────────────────");

    displayManager.setTextColor(0x7BEF);
    displayManager.printText("Notif vol  ");
    displayManager.setTextColor(TFT_WHITE);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", nm._notifVol);
    displayManager.println(buf);

    displayManager.setTextColor(0x4208);
    displayManager.println("─────────────────────────────");

    for (int i = 0; i < NOTIF_COUNT; i++) {
        displayManager.setCursor(10, displayManager.getCursorY());

        // Level name
        displayManager.setTextColor(0x7BEF);
        char nameBuf[10];
        snprintf(nameBuf, sizeof(nameBuf), "%-8s", levelName(i));
        displayManager.printText(nameBuf);

        // State
        if (nm._enabled[i]) {
            displayManager.setTextColor(TFT_GREEN);
            displayManager.printText("ON  ");
        } else {
            displayManager.setTextColor(TFT_RED);
            displayManager.printText("OFF ");
        }

        // MP3 filename (basename only) or "tone"
        if (nm._mp3File[i][0]) {
            const char* fname = strrchr(nm._mp3File[i], '/');
            fname = fname ? fname + 1 : nm._mp3File[i];
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println(fname);
        } else {
            displayManager.setTextColor(0x4208);
            displayManager.println("tone");
        }
    }
}

// ── Command handler ───────────────────────────────────────────────────────────
// notif [on|off|vol <0-100>|<level> on|off|<level> file <path>|status]

void NotificationManager::handleNotifCmd(char* args) {
    NotificationManager& nm = getInstance();

    if (!args || !*args || strcmp(args, "status") == 0) {
        nm.printStatus();
        return;
    }

    if (strcmp(args, "on") == 0) {
        nm.enableAll(true);
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Notifications: ON");
        displayManager.setTextColor(TFT_WHITE);
        nm.saveConfig();
        displayManager.printCommandScreen();
        return;
    }
    if (strcmp(args, "off") == 0) {
        nm.enableAll(false);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Notifications: OFF");
        displayManager.setTextColor(TFT_WHITE);
        nm.saveConfig();
        displayManager.printCommandScreen();
        return;
    }

    // "vol [<0-100>]"
    if (strncmp(args, "vol", 3) == 0 && (args[3] == ' ' || args[3] == '\0')) {
        if (args[3] != '\0') {
            int v = atoi(args + 4);
            nm.setNotifVol((uint8_t)constrain(v, 0, 100));
            nm.saveConfig();
        }
        displayManager.setTextColor(0x7BEF);
        displayManager.printText("Notif vol  ");
        displayManager.setTextColor(TFT_WHITE);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", nm._notifVol);
        displayManager.println(buf);
        displayManager.printCommandScreen();
        return;
    }

    // "<level> on|off"  or  "<level> file [<path>]"
    char lvlBuf[16] = {};
    char valBuf[64] = {};
    if (sscanf(args, "%15s %63[^\n]", lvlBuf, valBuf) < 2) {
        displayManager.setTextColor(0x7BEF);
        displayManager.println("notif [on|off|vol <n>|<lvl> on|off|<lvl> file <f>]");
        displayManager.printCommandScreen();
        return;
    }

    int lvlIdx = -1;
    for (int i = 0; i < NOTIF_COUNT; i++) {
        if (strcmp(lvlBuf, levelName(i)) == 0) { lvlIdx = i; break; }
    }
    if (lvlIdx < 0) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Unknown level");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    if (strcmp(valBuf, "on") == 0 || strcmp(valBuf, "off") == 0) {
        bool on = strcmp(valBuf, "on") == 0;
        nm.enable((NotifLevel)lvlIdx, on);
        displayManager.setTextColor(0x7BEF);
        displayManager.printText(levelName(lvlIdx));
        displayManager.printText(": ");
        displayManager.setTextColor(on ? TFT_GREEN : TFT_RED);
        displayManager.println(on ? "ON" : "OFF");
        displayManager.setTextColor(TFT_WHITE);
    } else if (strncmp(valBuf, "file", 4) == 0) {
        char* pathStart = valBuf + 4;
        while (*pathStart == ' ') pathStart++;
        if (*pathStart == '\0') {
            nm._mp3File[lvlIdx][0] = '\0';
            displayManager.setTextColor(0x7BEF);
            displayManager.printText(levelName(lvlIdx));
            displayManager.println(": cleared (using tone)");
        } else {
            if (pathStart[0] != '/') {
                char full[64];
                snprintf(full, sizeof(full), "%s/%s", NM_NOTIF_DIR, pathStart);
                strncpy(nm._mp3File[lvlIdx], full, sizeof(nm._mp3File[lvlIdx]) - 1);
            } else {
                strncpy(nm._mp3File[lvlIdx], pathStart, sizeof(nm._mp3File[lvlIdx]) - 1);
            }
            displayManager.setTextColor(0x7BEF);
            displayManager.printText(levelName(lvlIdx));
            displayManager.printText(": ");
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println(nm._mp3File[lvlIdx]);
        }
        displayManager.setTextColor(TFT_WHITE);
    } else {
        displayManager.setTextColor(0x7BEF);
        displayManager.println("notif <level> [on|off|file <path>]");
        displayManager.printCommandScreen();
        return;
    }

    nm.saveConfig();
    displayManager.printCommandScreen();
}
