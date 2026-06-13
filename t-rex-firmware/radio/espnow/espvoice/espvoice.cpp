// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// ESP-NOW half-duplex walkie-talkie with HD voice. Push-to-talk is a TOGGLE
// (the I2C keyboard reports no key-up, so true hold-to-talk is impossible):
// press SPACE to transmit, press again to return to listening.
//
// Audio: ES7210 mic @ 16 kHz mono (de-dup of ALL_LEFT) → ITU-T G.722 wideband
// codec (Mode 1, 64 kbps) → 160 bytes per 20 ms frame → ESP-NOW broadcast. RX
// decodes straight back to 16 kHz for the MAX98357 speaker. G.722 gives true
// 7 kHz audio bandwidth (HD voice) at the same packet budget as narrowband
// G.711 — twice the fidelity for free. Codec core: vendored public-domain
// libg722 (sippy/libg722, Steve Underwood / CMU); see lib/libg722/.
//
// Mic (ES7210) + speaker exist on BOTH T-Deck and T-Deck Plus — only GPS is
// Plus-exclusive — so this command is NOT board-gated.

#include "espvoice.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "utilities.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <math.h>
#include <driver/i2s.h>
#include <Wire.h>
#include "es7210.h"
#include "g722.h"
#include "g722_encoder.h"
#include "g722_decoder.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

// ── Config ───────────────────────────────────────────────────────────────────
#define MIC_I2S_PORT    I2S_NUM_1
#define SPK_I2S_PORT    I2S_NUM_0
#define AUDIO_RATE      16000          // G.722 wideband input/output rate
#define EV_SAMPLES      320            // PCM samples per frame (20 ms @ 16 kHz)
#define EV_BYTES        160            // G.722 bytes per frame (2 samples/byte)
#define EV_RING         24             // RX jitter buffer (frames)
#define EV_MIC_GAIN     10             // GAIN_30DB default
#define G722_BITRATE    64000          // Mode 1
#define EV_RX_SILENCE   500            // ms of no frames → transmission ended
#define EV_EOT_REPEAT   3              // EOT markers sent on release (lossy net)

// ── Wire format (type byte shared with espsniff/espchat) ─────────────────────
enum { EV_KIND_VOICE = 0, EV_KIND_EOT = 1 };   // end-of-transmission marker

struct EvMsg {
    uint8_t type;          // 0x02 = voice
    uint8_t kind;          // EV_KIND_VOICE | EV_KIND_EOT
    uint8_t seq;
    uint8_t g722[EV_BYTES];
};

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ── Codec contexts (main task only — G.722 state is per-direction) ───────────
static G722_ENC_CTX* s_enc = nullptr;
static G722_DEC_CTX* s_dec = nullptr;

// ── RX ring (recv cb writes, main loop reads) ────────────────────────────────
static uint8_t          s_rxRing[EV_RING][EV_BYTES];
static volatile uint8_t s_rxWr = 0;
static uint8_t          s_rxRd = 0;

static volatile uint32_t s_rxTotal = 0;
static uint32_t          s_txTotal = 0;
static uint8_t           s_txSeq   = 0;
static uint8_t           s_channel = 1;
static uint8_t           s_ownMAC[6];

// Set by recv cb (WiFi task), consumed by main loop — small races are benign.
static volatile uint8_t  s_rxMac[6]    = {0};
static volatile bool     s_eotPending  = false;

// Diagnostics for the intermittent long-session crash (see
// .claude/memory/project_espvoice_crash_watch.md). Shown live on the stats line:
// a climbing s_txDrop = WiFi TX buffer exhaustion; a falling heap = a leak.
static uint32_t s_txDrop = 0;   // esp_now_send() failures

// App-local playback volume (percent). Scales decoded audio + roger beep before
// the speaker. 100% = full clean decoded level (the reference). Capped at 150%
// because more than that hard-clips the speech into distortion and can brown
// out the board on battery. Does NOT touch the global `vol` / notification
// volume — it lives and dies with this command.
static int s_vol = 100;
#define EV_VOL_MIN   0
#define EV_VOL_MAX   150
#define EV_VOL_STEP  25

// App-local TX mic gain (ES7210 ADC). Raising this is the CLEAN way to get
// "louder" on the far end — a hotter source beats boosting the already-full RX
// signal past unity. Session-local; resets to the default each launch.
static int s_gain = EV_MIC_GAIN;   // index into EV_GAIN_DB
#define EV_GAIN_MAX  14            // GAIN_37_5DB
static const char* const EV_GAIN_DB[] = {
    "0", "3", "6", "9", "12", "15", "18", "21",
    "24", "27", "30", "33", "34.5", "36", "37.5"
};

// ── I2S / ES7210 lifecycle (mirrors mictest) ─────────────────────────────────
static bool s_micUp = false;
static bool s_spkUp = false;

static bool micStart(int gainIdx) {
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
    cfg.sample_rate          = AUDIO_RATE;
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

static void micStop() {
    if (!s_micUp) return;
    i2s_driver_uninstall(MIC_I2S_PORT);
    es7210_adc_ctrl_state(AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_STOP);
    s_micUp = false;
}

static bool spkStart() {
    if (s_spkUp) return true;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AUDIO_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
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
    s_spkUp = true;
    return true;
}

static void spkStop() {
    if (!s_spkUp) return;
    i2s_zero_dma_buffer(SPK_I2S_PORT);
    i2s_driver_uninstall(SPK_I2S_PORT);
    s_spkUp = false;
}

// Push the current mic-gain index to the ES7210 (safe to call live).
static void applyGain() {
    es7210_adc_set_gain_all((es7210_gain_value_t)s_gain);
}

// ── ESP-NOW ──────────────────────────────────────────────────────────────────
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(EvMsg) || data[0] != 0x02) return;
    memcpy((void*)s_rxMac, mac, 6);

    if (data[1] == EV_KIND_EOT) {
        s_eotPending = true;            // peer released PTT
        return;
    }
    // voice frame
    uint8_t slot = s_rxWr % EV_RING;
    memcpy(s_rxRing[slot], data + 3, EV_BYTES);   // skip type+kind+seq
    s_rxWr++;
    s_rxTotal++;
}

static void addBcastPeer() {
    esp_now_del_peer(BCAST);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void setChannel(uint8_t ch) {
    s_channel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    addBcastPeer();
}

// ── TX: read one mic frame → G.722, return peak amplitude ────────────────────
// ALL_LEFT delivers 2 int16/sample (L/R dup); de-duplicate to 320 mono samples.
static int captureFrame(int16_t* raw, int16_t* pcm, EvMsg* msg) {
    size_t bytesRead = 0;
    i2s_read(MIC_I2S_PORT, raw, EV_SAMPLES * 2 * sizeof(int16_t), &bytesRead, portMAX_DELAY);
    int peak = 0;
    for (int i = 0; i < EV_SAMPLES; i++) {
        int16_t s = raw[2 * i];            // even raw index = left channel
        pcm[i] = s;
        int amp = s < 0 ? -s : s;
        if (amp > peak) peak = amp;
    }
    g722_encode(s_enc, pcm, EV_SAMPLES, msg->g722);
    return peak;
}

// ── Roger beep — short tone played on the speaker when a transmission ends ───
// (the classic walkie-talkie "over" cue). Speaker must already be up.
static void rogerBeep() {
    if (!s_spkUp) return;
    const int freq  = 1500;
    const int total = AUDIO_RATE * 120 / 1000;   // 120 ms
    static int16_t buf[256 * 2];
    for (int pos = 0; pos < total; ) {
        int chunk = total - pos; if (chunk > 256) chunk = 256;
        for (int i = 0; i < chunk; i++) {
            int idx = pos + i;
            float env = 1.0f;                                  // gentle fade out
            if (idx > total * 7 / 10) env = (float)(total - idx) / (total * 3 / 10);
            int32_t s = (int32_t)(7000.0f * env * sinf(2.0f * M_PI * freq * idx / AUDIO_RATE));
            s = s * s_vol / 100;
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            int16_t v = (int16_t)s;
            buf[i * 2] = v; buf[i * 2 + 1] = v;
        }
        size_t w;
        i2s_write(SPK_I2S_PORT, buf, (size_t)chunk * 2 * sizeof(int16_t), &w, portMAX_DELAY);
        pos += chunk;
    }
    i2s_zero_dma_buffer(SPK_I2S_PORT);
}

// ── RX: decode one G.722 frame → speaker (mono duplicated to stereo) ─────────
static void playFrame(const uint8_t* g722, int16_t* pcm, int16_t* stereo) {
    int n = g722_decode(s_dec, g722, EV_BYTES, pcm);
    if (n <= 0) return;
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)pcm[i] * s_vol / 100;   // app-local volume
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        stereo[2 * i]     = (int16_t)v;
        stereo[2 * i + 1] = (int16_t)v;
    }
    size_t written;
    i2s_write(SPK_I2S_PORT, stereo, (size_t)n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
}

// ── UI ───────────────────────────────────────────────────────────────────────
#define Y_HEAD   (outputY)
#define Y_MAC    (outputY + LINE_HEIGHT * 2)
#define Y_STATE  (outputY + LINE_HEIGHT * 3)
#define Y_BARLBL (outputY + LINE_HEIGHT * 5)
#define Y_BAR    (Y_BARLBL + LINE_HEIGHT)
#define Y_VOL    (Y_BAR + 22)
#define Y_GAIN   (Y_VOL + LINE_HEIGHT)
#define Y_STATS  (Y_GAIN + LINE_HEIGHT)
#define Y_FOOT   (Y_STATS + LINE_HEIGHT)
#define Y_FOOT2  (Y_FOOT + LINE_HEIGHT)
#define BAR_X    8
#define BAR_W    300
#define BAR_H    14

static void drawStatic() {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, Y_HEAD);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("ESPVOICE");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("PTT");
    dm.setTextColor(0x7BEF);     dm.printText("]  G.722 ch");
    char ch[4]; snprintf(ch, sizeof(ch), "%d", s_channel);
    dm.setTextColor(TFT_WHITE);  dm.println(ch);
    dm.printSeparator();

    char mac[26];
    snprintf(mac, sizeof(mac), "MAC %02X:%02X:%02X:%02X:%02X:%02X",
             s_ownMAC[0], s_ownMAC[1], s_ownMAC[2],
             s_ownMAC[3], s_ownMAC[4], s_ownMAC[5]);
    dm.printText(mac, 4, Y_MAC, 0x4208);

    dm.printText("Mic level:", 4, Y_BARLBL, 0x7BEF);
    dm.fillRect(BAR_X - 1, Y_BAR - 1, BAR_W + 2, BAR_H + 2, 0x4208);

    dm.printText("[space] talk / listen        [q] quit", 4, Y_FOOT,  0x7BEF);
    dm.printText("vol [+/-]   gain [o/p]   ch [,/.]",     4, Y_FOOT2, 0x7BEF);
}

// App-local RX volume readout (does not affect global vol).
static void drawVol() {
    char buf[28];
    snprintf(buf, sizeof(buf), "RX volume: %d%%", s_vol);
    displayManager.fillRect(4, Y_VOL, 220, LINE_HEIGHT, TFT_BLACK);
    uint16_t col = (s_vol > 100) ? TFT_YELLOW : TFT_WHITE;
    displayManager.printText(buf, 4, Y_VOL, col);
}

// App-local TX mic-gain readout.
static void drawGain() {
    char buf[28];
    snprintf(buf, sizeof(buf), "TX mic gain: %s dB", EV_GAIN_DB[s_gain]);
    displayManager.fillRect(4, Y_GAIN, 220, LINE_HEIGHT, TFT_BLACK);
    uint16_t col = (s_gain > EV_MIC_GAIN) ? TFT_YELLOW : TFT_WHITE;
    displayManager.printText(buf, 4, Y_GAIN, col);
}

static void drawState(bool tx) {
    displayManager.fillRect(0, Y_STATE, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
    if (tx) {
        displayManager.fillRect(4, Y_STATE, 200, LINE_HEIGHT, TFT_RED);
        displayManager.printText("  TRANSMITTING  (press space)", 8, Y_STATE, TFT_WHITE);
    } else {
        displayManager.printText("LISTENING...  press space to talk", 4, Y_STATE, TFT_GREEN);
    }
}

// Incoming-transmission banner (receiver side) — shows who is talking.
static void drawReceiving() {
    displayManager.fillRect(0, Y_STATE, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
    displayManager.fillRect(4, Y_STATE, 250, LINE_HEIGHT, 0x001F);   // blue
    char buf[40];
    snprintf(buf, sizeof(buf), "  >> RECEIVING  %02X:%02X:%02X",
             s_rxMac[3], s_rxMac[4], s_rxMac[5]);
    displayManager.printText(buf, 8, Y_STATE, TFT_WHITE);
}

static void drawMicBar(int peak) {
    static int held = 0;
    if (peak > held) held = peak;
    else             held -= (held - peak) / 4 + 1;
    if (held < 0) held = 0;

    int pct = (held * 100) / 8000;
    if (pct > 100) pct = 100;
    int filled = (pct * BAR_W) / 100;

    uint16_t col = TFT_GREEN;
    if (pct > 80) col = TFT_RED;
    else if (pct > 50) col = TFT_YELLOW;

    if (filled > 0)      displayManager.fillRect(BAR_X, Y_BAR, filled, BAR_H, col);
    if (filled < BAR_W)  displayManager.fillRect(BAR_X + filled, Y_BAR, BAR_W - filled, BAR_H, TFT_BLACK);
}

static void drawStats() {
    char buf[64];
    // drp climbing = WiFi TX exhaustion; heap falling = leak (crash diagnostics)
    snprintf(buf, sizeof(buf), "TX:%lu RX:%lu drp:%lu heap:%uk",
             (unsigned long)s_txTotal, (unsigned long)s_rxTotal,
             (unsigned long)s_txDrop, (unsigned)(ESP.getFreeHeap() / 1024));
    displayManager.fillRect(4, Y_STATS, 312, LINE_HEIGHT, TFT_BLACK);
    displayManager.printText(buf, 4, Y_STATS, 0x4208);
}

// ── Mode switches ─────────────────────────────────────────────────────────────
// Both I2S ports stay installed for the whole session (mic = I2S1, speaker =
// I2S0 — separate peripherals). Switching the *drivers* on every PTT toggle
// while ESP-NOW/WiFi DMA is live was crashing; instead we just gate which port
// we read/write and flush stale buffers on transition.
static void enterTx() {
    // discard mic audio buffered while we were listening (non-blocking drain)
    static uint8_t junk[512];
    size_t br = 0;
    while (i2s_read(MIC_I2S_PORT, junk, sizeof(junk), &br, 0) == ESP_OK && br > 0) {}
}

static void enterRx() {
    // flush stale audio + EOT captured before we started listening
    s_rxRd       = s_rxWr;
    s_eotPending = false;
    i2s_zero_dma_buffer(SPK_I2S_PORT);
}

// ── Entry point ───────────────────────────────────────────────────────────────
void runEspVoice(char* args) {
    int initCh = (args && *args) ? atoi(args) : 1;
    if (initCh < 1 || initCh > 13) initCh = 1;

    s_rxWr = s_rxRd = 0;
    s_rxTotal = s_txTotal = 0;
    s_txSeq   = 0;
    s_channel = (uint8_t)initCh;
    s_vol     = 100;
    s_gain    = EV_MIC_GAIN;
    s_txDrop  = 0;

    s_enc = g722_encoder_new(G722_BITRATE, G722_DEFAULT);
    s_dec = g722_decoder_new(G722_BITRATE, G722_DEFAULT);
    if (!s_enc || !s_dec) {
        if (s_enc) { g722_encoder_destroy(s_enc); s_enc = nullptr; }
        if (s_dec) { g722_decoder_destroy(s_dec); s_dec = nullptr; }
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("G.722 codec alloc failed");
        displayManager.printCommandScreen();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    // Disable WiFi modem sleep — sustained ESP-NOW (50 pkt/s) gets flaky and can
    // wedge over minutes if the radio is dozing between beacons.
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        g722_encoder_destroy(s_enc); s_enc = nullptr;
        g722_decoder_destroy(s_dec); s_dec = nullptr;
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("ESP-NOW init failed");
        displayManager.printCommandScreen();
        return;
    }
    esp_now_register_recv_cb(onRecv);
    addBcastPeer();
    WiFi.macAddress(s_ownMAC);

    // Install BOTH I2S ports up front and keep them resident (start listening).
    bool tx = false;
    if (!spkStart() || !micStart(s_gain)) {
        micStop(); spkStop();
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Audio I2S init failed");
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        g722_encoder_destroy(s_enc); s_enc = nullptr;
        g722_decoder_destroy(s_dec); s_dec = nullptr;
        WiFi.mode(WIFI_STA);
        displayManager.printCommandScreen();
        return;
    }

    drawStatic();
    drawState(false);
    drawStats();
    drawVol();
    drawGain();

    // Audio scratch kept off the stack — DRAM statics.
    static int16_t raw[EV_SAMPLES * 2];   // mic capture (ALL_LEFT, 2x)
    static int16_t pcm[EV_SAMPLES];       // mono PCM frame
    static int16_t stereo[EV_SAMPLES * 2];// speaker (L/R)
    EvMsg    msg = {};
    msg.type = 0x02;
    msg.kind = EV_KIND_VOICE;

    uint32_t lastUi      = 0;
    bool     receiving   = false;   // a transmission is currently incoming
    uint32_t lastFrameMs = 0;       // when we last played an RX frame
    uint32_t lastBar     = 0;       // throttle mic-bar redraw (DMA contention)
    int      peakAccum   = 0;       // max mic peak since last bar redraw

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawStatic();
            if (receiving) drawReceiving(); else drawState(tx);
            drawStats();
            drawVol();
            drawGain();
        }

        if (tx) {
            // ── TRANSMIT ──────────────────────────────────────────────────────
            msg.seq  = s_txSeq++;
            msg.kind = EV_KIND_VOICE;
            int peak = captureFrame(raw, pcm, &msg);
            if (esp_now_send(BCAST, (uint8_t*)&msg, sizeof(msg)) != ESP_OK) s_txDrop++;
            s_txTotal++;
            if (peak > peakAccum) peakAccum = peak;

            uint32_t now = millis();
            // Throttle display DMA: redraw the bar at ~15 Hz, not every 20 ms TX
            // frame — 50 Hz of fillRects fights WiFi+I2S DMA over a long session.
            if (!displayManager.isBlocked() && now - lastBar >= 66) {
                drawMicBar(peakAccum);
                peakAccum = 0;
                lastBar   = now;
                if (now - lastUi >= 400) { drawStats(); lastUi = now; }
            }
        } else {
            // ── LISTEN ────────────────────────────────────────────────────────
            uint8_t pending = (uint8_t)(s_rxWr - s_rxRd);
            if (pending > EV_RING) { s_rxRd = s_rxWr - EV_RING; pending = EV_RING; }

            uint32_t now = millis();
            if (pending > 0) {
                playFrame(s_rxRing[s_rxRd % EV_RING], pcm, stereo);
                s_rxRd++;
                lastFrameMs = now;
                if (!receiving) {                // start of an incoming transmission
                    receiving = true;
                    if (!displayManager.isBlocked()) drawReceiving();
                }
                if (!displayManager.isBlocked() && now - lastUi >= 400) {
                    drawStats(); lastUi = now;
                }
            } else {
                // Ring empty. Keep the mic's RX DMA cycling — it runs unread the
                // whole time we listen; draining a little each idle tick prevents
                // any descriptor buildup over a long session. Non-blocking.
                size_t br = 0;
                i2s_read(MIC_I2S_PORT, raw, sizeof(raw), &br, 0);
                vTaskDelay(pdMS_TO_TICKS(5));   // yield
            }

            // End of transmission: explicit EOT marker, or silence timeout if lost.
            if (receiving && (s_eotPending || (now - lastFrameMs > EV_RX_SILENCE))) {
                receiving    = false;
                s_eotPending = false;
                rogerBeep();                     // "over" cue to the listener
                if (!displayManager.isBlocked()) drawState(false);
            }
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (k == ' ') {
            tx = !tx;
            if (tx) {
                enterTx();
            } else {
                // Tell listeners we released PTT (repeat — ESP-NOW is lossy).
                EvMsg eot = {}; eot.type = 0x02; eot.kind = EV_KIND_EOT; eot.seq = s_txSeq++;
                for (int i = 0; i < EV_EOT_REPEAT; i++) esp_now_send(BCAST, (uint8_t*)&eot, sizeof(eot));
                enterRx();
                rogerBeep();                      // talker also hears the "over"
            }
            receiving = false;
            drawState(tx);
        } else if (k == '+' || k == '=') {
            if (s_vol < EV_VOL_MAX) { s_vol += EV_VOL_STEP; drawVol(); }
        } else if (k == '-' || k == '_') {
            if (s_vol > EV_VOL_MIN) { s_vol -= EV_VOL_STEP; drawVol(); }
        } else if (k == 'p' || k == 'P') {
            if (s_gain < EV_GAIN_MAX) { s_gain++; applyGain(); drawGain(); }
        } else if (k == 'o' || k == 'O') {
            if (s_gain > 0)           { s_gain--; applyGain(); drawGain(); }
        } else if (k == '.' || k == '>') {
            if (s_channel < 13) {
                setChannel(s_channel + 1);
                drawStatic();
                if (receiving) drawReceiving(); else drawState(tx);
                drawStats(); drawVol(); drawGain();
            }
        } else if (k == ',' || k == '<') {
            if (s_channel > 1) {
                setChannel(s_channel - 1);
                drawStatic();
                if (receiving) drawReceiving(); else drawState(tx);
                drawStats(); drawVol(); drawGain();
            }
        }
    }

    micStop();
    spkStop();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    g722_encoder_destroy(s_enc); s_enc = nullptr;
    g722_decoder_destroy(s_dec); s_dec = nullptr;
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);   // restore default modem sleep

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.printCommandScreen();
}
