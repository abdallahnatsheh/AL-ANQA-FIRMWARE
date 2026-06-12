#include "test_mic.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "utilities.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <string.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

#ifndef BOARD_TDECK_PLUS

void runMicTest() {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Mic test: T-Deck Plus only (ES7210)");
    displayManager.printCommandScreen();
}

#else // BOARD_TDECK_PLUS

#include <Wire.h>
#include "es7210.h"

// ── Config ───────────────────────────────────────────────────────────────────
#define MIC_I2S_PORT    I2S_NUM_1
#define SPK_I2S_PORT    I2S_NUM_0
#define MIC_SAMPLE_RATE 16000
#define MIC_CHUNK       320            // samples per read (~20ms @ 16kHz)
#define MIC_REC_SECONDS 3
#define MIC_REC_SAMPLES (MIC_SAMPLE_RATE * MIC_REC_SECONDS)
#define MIC_DIAG_COLS   32
#define MIC_VAD_PEAK    900             // peak amplitude considered "voice"
#define MIC_LEVEL_MAX   8000            // peak amplitude mapped to 100% bar

// ── Layout (fixed pixel positions for partial redraws) ──────────────────────
#define Y_HEADER    (outputY)
#define Y_STATUS    (Y_HEADER + LINE_HEIGHT * 2)
#define Y_LEVELLBL  (Y_STATUS + LINE_HEIGHT)
#define Y_BAR       (Y_LEVELLBL + LINE_HEIGHT)
#define BAR_X       70
#define BAR_W       220
#define BAR_H       10
#define Y_DIAGTOP   (Y_BAR + 18)
#define DIAG_X      10
#define DIAG_W      300
#define DIAG_H      64
#define DIAG_COL_W  (DIAG_W / MIC_DIAG_COLS)
#define Y_VOICE     (Y_DIAGTOP + DIAG_H + 8)
#define Y_STATUSMSG (Y_VOICE + LINE_HEIGHT)
#define Y_FOOTER    (Y_STATUSMSG + LINE_HEIGHT)

static const char* const GAIN_LABELS[] = {
    "0dB","3dB","6dB","9dB","12dB","15dB","18dB","21dB",
    "24dB","27dB","30dB","33dB","34.5dB","36dB","37.5dB"
};
#define GAIN_MAX 14

// ── I2S / ES7210 lifecycle ───────────────────────────────────────────────────
static bool s_micUp = false;

static bool startMicI2S(int gainIdx) {
    if (s_micUp) return true;

    audio_hal_codec_config_t codec = {};
    codec.adc_input  = AUDIO_HAL_ADC_INPUT_ALL;
    codec.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
    codec.i2s_iface.mode    = AUDIO_HAL_MODE_SLAVE;
    codec.i2s_iface.fmt     = AUDIO_HAL_I2S_NORMAL;
    codec.i2s_iface.samples = AUDIO_HAL_16K_SAMPLES;
    codec.i2s_iface.bits    = AUDIO_HAL_BIT_LENGTH_16BITS;

    if (es7210_adc_init(&Wire, &codec) != ESP_OK) return false;
    es7210_adc_config_i2s(codec.codec_mode, &codec.i2s_iface);
    es7210_adc_set_gain_all((es7210_gain_value_t)gainIdx);
    es7210_adc_ctrl_state(codec.codec_mode, AUDIO_HAL_CTRL_START);

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ALL_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = 256;
    cfg.use_apll             = false;

    if (i2s_driver_install(MIC_I2S_PORT, &cfg, 0, NULL) != ESP_OK) {
        es7210_adc_ctrl_state(codec.codec_mode, AUDIO_HAL_CTRL_STOP);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = BOARD_ES7210_MCLK;
    pins.bck_io_num   = BOARD_ES7210_SCK;
    pins.ws_io_num    = BOARD_ES7210_LRCK;
    pins.data_in_num  = BOARD_ES7210_DIN;
    pins.data_out_num = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(MIC_I2S_PORT);
        es7210_adc_ctrl_state(codec.codec_mode, AUDIO_HAL_CTRL_STOP);
        return false;
    }
    i2s_zero_dma_buffer(MIC_I2S_PORT);
    s_micUp = true;
    return true;
}

static void stopMicI2S() {
    if (!s_micUp) return;
    i2s_driver_uninstall(MIC_I2S_PORT);
    es7210_adc_ctrl_state(AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_STOP);
    s_micUp = false;
}

// ── Speaker playback (separate I2S port — reuses BOARD_I2S_* pins) ───────────
static bool startSpkI2S() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = 256;
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
    return true;
}

static void stopSpkI2S() {
    i2s_zero_dma_buffer(SPK_I2S_PORT);
    i2s_driver_uninstall(SPK_I2S_PORT);
}

// ── Static UI ────────────────────────────────────────────────────────────────
static void drawStaticUI(bool micOk) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, Y_HEADER);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("MIC");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("TEST");
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();

    dm.printText(micOk ? "ES7210: OK" : "ES7210: INIT FAILED",
                  4, Y_STATUS, micOk ? TFT_GREEN : TFT_RED);

    dm.printText("Level:", 4, Y_LEVELLBL, 0x7BEF);

    // Bar outline
    dm.fillRect(BAR_X - 1, Y_BAR - 1, BAR_W + 2, BAR_H + 2, 0x4208);

    // Diagram outline
    dm.fillRect(DIAG_X - 1, Y_DIAGTOP - 1, DIAG_W + 2, DIAG_H + 2, 0x4208);
    dm.fillRect(DIAG_X, Y_DIAGTOP, DIAG_W, DIAG_H, TFT_BLACK);

    dm.printText("[r]ecord 3s  [p]lay  [+/-]gain  [q]uit", 4, Y_FOOTER, 0x7BEF);
}

// ── Level bar + diagram redraw ───────────────────────────────────────────────
static void drawLevelBar(int peak) {
    // Peak-hold meter: jump up instantly, fall back gradually. Without this the
    // reading bounces wildly on every tick because the raw peak is noisy.
    static int held = 0;
    if (peak > held) held = peak;
    else              held -= (held - peak) / 4 + 1;   // ease toward current
    if (held < 0) held = 0;

    int pct = (held * 100) / MIC_LEVEL_MAX;
    if (pct > 100) pct = 100;
    int filled = (pct * BAR_W) / 100;

    uint16_t col = TFT_GREEN;
    if (pct > 80) col = TFT_RED;
    else if (pct > 50) col = TFT_YELLOW;

    if (filled > 0) displayManager.fillRect(BAR_X, Y_BAR, filled, BAR_H, col);
    if (filled < BAR_W) displayManager.fillRect(BAR_X + filled, Y_BAR, BAR_W - filled, BAR_H, TFT_BLACK);

    // Repaint the % text only when the displayed value changes — redrawing it
    // every ~20ms tick made it flicker/jitter on the noise floor.
    static int lastPct = -1;
    if (pct != lastPct) {
        lastPct = pct;
        char buf[8];
        snprintf(buf, sizeof(buf), "%3d%%", pct);
        displayManager.fillRect(BAR_X + BAR_W + 4, Y_LEVELLBL, 40, LINE_HEIGHT, TFT_BLACK);
        displayManager.printText(buf, BAR_X + BAR_W + 6, Y_LEVELLBL, TFT_WHITE);
    }
}

static void drawDiagram(uint8_t* hist) {
    displayManager.fillRect(DIAG_X, Y_DIAGTOP, DIAG_W, DIAG_H, TFT_BLACK);
    for (int i = 0; i < MIC_DIAG_COLS; i++) {
        int h = hist[i];
        if (h <= 0) continue;
        if (h > DIAG_H) h = DIAG_H;
        uint16_t col = TFT_GREEN;
        if (h > DIAG_H * 80 / 100) col = TFT_RED;
        else if (h > DIAG_H * 50 / 100) col = TFT_YELLOW;
        int x = DIAG_X + i * DIAG_COL_W;
        displayManager.fillRect(x, Y_DIAGTOP + (DIAG_H - h), DIAG_COL_W - 1, h, col);
    }
}

static void drawVoice(bool voice) {
    displayManager.fillRect(4, Y_VOICE, 200, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(voice ? "*** VOICE DETECTED ***" : "...",
                              4, Y_VOICE, voice ? TFT_RED : 0x4208);
}

static void drawStatusMsg(const char* msg, uint16_t color) {
    displayManager.fillRect(4, Y_STATUSMSG, 312, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(msg, 4, Y_STATUSMSG, color);
}

static void drawGain(int gainIdx) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Gain: %s", GAIN_LABELS[gainIdx]);
    displayManager.fillRect(150, Y_STATUS, 166, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(buf, 150, Y_STATUS, TFT_WHITE);
}

// ── Recording ────────────────────────────────────────────────────────────────
// ALL_LEFT format: ES7210 RX delivers 2 int16 per audio sample (L/R slots,
// both identical) — chunkBuf must hold MIC_CHUNK*2 raw int16s. Reads one
// MIC_CHUNK block (= MIC_CHUNK unique samples after de-duplication), returns
// peak abs amplitude, optionally stores de-duplicated samples into dst.
static int readChunk(int16_t* chunkBuf, int16_t* dst) {
    size_t bytesRead = 0;
    i2s_read(MIC_I2S_PORT, chunkBuf, MIC_CHUNK * 2 * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    int nRaw = bytesRead / sizeof(int16_t);
    int peak = 0;
    int outIdx = 0;
    for (int i = 0; i < nRaw; i += 2) {
        int v = chunkBuf[i];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
        if (dst) dst[outIdx] = chunkBuf[i];
        outIdx++;
    }
    return peak;
}

static void playRecording(int16_t* recBuf, uint32_t recCount) {
    stopMicI2S();
    if (!startSpkI2S()) { startMicI2S(0); return; }

    int16_t stereo[MIC_CHUNK * 2];
    for (uint32_t pos = 0; pos < recCount; pos += MIC_CHUNK) {
        uint32_t n = recCount - pos;
        if (n > MIC_CHUNK) n = MIC_CHUNK;
        for (uint32_t i = 0; i < n; i++) {
            stereo[i * 2]     = recBuf[pos + i];
            stereo[i * 2 + 1] = recBuf[pos + i];
        }
        size_t written;
        i2s_write(SPK_I2S_PORT, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }
    i2s_zero_dma_buffer(SPK_I2S_PORT);
    stopSpkI2S();
}

// ── Entry point ───────────────────────────────────────────────────────────────
void runMicTest() {
    int gainIdx = 10; // GAIN_30DB — reasonable default for ES7210 mic array
    bool micOk = startMicI2S(gainIdx);
    drawStaticUI(micOk);
    drawGain(gainIdx);
    drawStatusMsg("idle — press [r] to record", 0x7BEF);

    uint8_t hist[MIC_DIAG_COLS] = {0};
    int16_t chunkBuf[MIC_CHUNK * 2];

    int16_t* recBuf   = nullptr;
    uint32_t recCount = 0;

    int    voiceHold = 0;
    bool   voiceShown = false;
    int    diagTick  = 0;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawStaticUI(micOk);
            drawGain(gainIdx);
            drawStatusMsg(recCount ? "recorded — [p] play" : "idle — press [r] to record", 0x7BEF);
        }

        if (!micOk) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') break;
            continue;
        }

        int peak = readChunk(chunkBuf, nullptr);

        // Voice activity detection (debounced)
        if (peak > MIC_VAD_PEAK) voiceHold = 8;       // ~160ms hold
        else if (voiceHold > 0) voiceHold--;
        bool voice = voiceHold > 0;
        if (voice != voiceShown) { drawVoice(voice); voiceShown = voice; }

        drawLevelBar(peak);

        // Scroll diagram history
        memmove(hist, hist + 1, MIC_DIAG_COLS - 1);
        hist[MIC_DIAG_COLS - 1] = (uint8_t)((peak * DIAG_H) / MIC_LEVEL_MAX);
        if (++diagTick >= 2) { drawDiagram(hist); diagTick = 0; }

        char k = inputHandler.getKeyboardInput();
        if (!k) continue;

        if (k == 'q' || k == 'Q') break;

        if ((k == '+' || k == '=') && gainIdx < GAIN_MAX) {
            gainIdx++;
            es7210_adc_set_gain_all((es7210_gain_value_t)gainIdx);
            drawGain(gainIdx);
        } else if (k == '-' && gainIdx > 0) {
            gainIdx--;
            es7210_adc_set_gain_all((es7210_gain_value_t)gainIdx);
            drawGain(gainIdx);
        } else if (k == 'r' || k == 'R') {
            if (!recBuf) recBuf = (int16_t*)ps_malloc(MIC_REC_SAMPLES * sizeof(int16_t));
            if (!recBuf) {
                drawStatusMsg("PSRAM alloc failed", TFT_RED);
                continue;
            }
            uint32_t pos = 0;
            bool aborted = false;
            while (pos < MIC_REC_SAMPLES) {
                int rp = readChunk(chunkBuf, recBuf + pos);
                pos += MIC_CHUNK;

                memmove(hist, hist + 1, MIC_DIAG_COLS - 1);
                hist[MIC_DIAG_COLS - 1] = (uint8_t)((rp * DIAG_H) / MIC_LEVEL_MAX);
                drawDiagram(hist);
                drawLevelBar(rp);

                char buf[40];
                snprintf(buf, sizeof(buf), "recording... %.1f/%ds",
                         (double)pos / MIC_SAMPLE_RATE, MIC_REC_SECONDS);
                drawStatusMsg(buf, TFT_YELLOW);

                char rk = inputHandler.getKeyboardInput();
                if (rk == 'q' || rk == 'Q') { aborted = true; break; }
            }
            recCount = aborted ? 0 : pos;
            drawStatusMsg(recCount ? "recorded — [p] play" : "idle — press [r] to record", 0x7BEF);
        } else if (k == 'p' || k == 'P') {
            if (recCount > 0) {
                drawStatusMsg("playing...", TFT_CYAN);
                playRecording(recBuf, recCount);
                micOk = startMicI2S(gainIdx);
                drawStatusMsg("recorded — [p] play", 0x7BEF);
            } else {
                drawStatusMsg("nothing recorded yet", TFT_RED);
            }
        }
    }

    stopMicI2S();
    if (recBuf) free(recBuf);
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.printCommandScreen();
}

#endif // BOARD_TDECK_PLUS
