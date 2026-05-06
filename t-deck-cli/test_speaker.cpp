#include "display_manager.h"
#include "input_handling.h"
#include "utilities.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

#define SPK_I2S_PORT  I2S_NUM_0
#define SPK_SAMPLE_RATE 22050

// ── I2S lifecycle ─────────────────────────────────────────────────────────────
static bool s_i2sUp = false;

static bool startI2S() {
    if (s_i2sUp) return true;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = SPK_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 128;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;

    if (i2s_driver_install(SPK_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = BOARD_I2S_BCK;
    pins.ws_io_num    = BOARD_I2S_WS;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(SPK_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(SPK_I2S_PORT);
        return false;
    }
    i2s_zero_dma_buffer(SPK_I2S_PORT);
    s_i2sUp = true;
    return true;
}

static void stopI2S() {
    if (!s_i2sUp) return;
    i2s_zero_dma_buffer(SPK_I2S_PORT);
    i2s_driver_uninstall(SPK_I2S_PORT);
    s_i2sUp = false;
}

// ── Sine-wave tone generator ──────────────────────────────────────────────────
// Writes one cycle of a sine wave at `freq` Hz for `durationMs` milliseconds.
static void playTone(int freq, int durationMs) {
    if (!s_i2sUp) return;

    // Samples for one full cycle (capped to avoid stack overflow on very low freq)
    int cycLen = SPK_SAMPLE_RATE / freq;
    if (cycLen > 256) cycLen = 256;

    int16_t buf[512]; // 256 stereo samples × 2 channels × 2 bytes = 1 KB max
    for (int i = 0; i < cycLen; i++) {
        int16_t v = (int16_t)(22000 * sinf(2.0f * M_PI * i / cycLen));
        buf[i * 2]     = v;  // left
        buf[i * 2 + 1] = v;  // right
    }
    size_t blockBytes = (size_t)cycLen * 4; // 4 bytes per stereo sample
    size_t written;
    uint32_t t0 = millis();
    while ((int32_t)(millis() - t0) < durationMs) {
        i2s_write(SPK_I2S_PORT, buf, blockBytes, &written, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(SPK_I2S_PORT);
}

static void playScale() {
    // C major: C4 D4 E4 F4 G4 A4 B4 C5
    const int notes[] = { 262, 294, 330, 349, 392, 440, 494, 523 };
    for (int n : notes) {
        playTone(n, 200);
        delay(30);
    }
}

// ── UI ────────────────────────────────────────────────────────────────────────
static void drawSpeakerMenu(bool i2sOk, const char* lastPlayed) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("SPEAKER TEST  (I2S PCM)");

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(i2sOk ? TFT_GREEN : TFT_RED);
    displayManager.println(i2sOk ? "I2S: OK" : "I2S: INIT FAILED");

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("");

    const struct { char key; const char* label; } items[] = {
        { '1', "[1] 220 Hz   low bass" },
        { '2', "[2] 440 Hz   A4 concert" },
        { '3', "[3] 880 Hz   high" },
        { '4', "[4] 1000 Hz  beep" },
        { '5', "[5] 2000 Hz  alert" },
        { '6', "[6] 4000 Hz  sharp" },
        { 's', "[s] C major scale" },
        { 'q', "[q] quit" },
    };
    for (auto& item : items) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(item.key == 'q' ? 0x7BEF : TFT_WHITE);
        displayManager.println(item.label);
    }

    if (lastPlayed[0]) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        char msg[40];
        snprintf(msg, sizeof(msg), ">> %s", lastPlayed);
        displayManager.println(msg);
    }
}

// ── entry point ───────────────────────────────────────────────────────────────
void runSpeakerTest() {
    bool i2sOk = startI2S();
    char lastPlayed[32] = {};
    drawSpeakerMenu(i2sOk, lastPlayed);

    const struct { char key; const char* label; int freq; int dur; } tones[] = {
        { '1', "220 Hz low bass",    220,  700 },
        { '2', "440 Hz A4",          440,  700 },
        { '3', "880 Hz high",        880,  700 },
        { '4', "1000 Hz beep",      1000,  300 },
        { '5', "2000 Hz alert",     2000,  300 },
        { '6', "4000 Hz sharp",     4000,  300 },
    };

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (!k) continue;
        if (k == 'q' || k == 'Q') break;

        if (k == 's' || k == 'S') {
            strncpy(lastPlayed, "C major scale", 31);
            drawSpeakerMenu(i2sOk, lastPlayed);
            playScale();
            continue;
        }

        for (auto& t : tones) {
            if (k == t.key) {
                strncpy(lastPlayed, t.label, 31);
                drawSpeakerMenu(i2sOk, lastPlayed);
                playTone(t.freq, t.dur);
                break;
            }
        }
    }

    stopI2S();
    displayManager.printCommandScreen();
}
