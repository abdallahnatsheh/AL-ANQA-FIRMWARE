// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// csidetect / csi — WiFi CSI motion detector (sweep-style display, NOT a real
// radar — it senses motion energy only, no direction/position/count).
//
// How it works: every received WiFi frame carries Channel State Information
// (amplitude/phase of the OFDM subcarriers). A moving body perturbs the room's
// multipath, so the CSI variance rises. We track that variance, self-calibrate
// a floor/ceiling, and turn it into a 0..1 motion score → presence.
//
// PRO build: instead of averaging all subcarriers to one number, we ALSO split
// the ~56 subcarriers into CSI_BANDS bands and track each band's variance →
// multiple live "contacts" on a double-buffered sprite radar with a sweeping
// cone (COD-style). Smooth 30 fps, snappy response, activity classification.
//
// HONESTY: a single antenna gives NO direction and CANNOT count/locate people —
// CSI is ONE motion-energy field. The band→sector mapping is arbitrary (8
// independent signal measurements shown around the dial), NOT bearing. The
// sweep angle is cosmetic. We never claim positions.
//
// Sources (rule: credit what we learn from):
//   - skizzophrenic/Cardputer-CSI-Human-Detector (MIT) — single-device CSI
//     acquisition config + amplitude/phase variance + asymmetric-EMA + hold/coast.
//   - ruvnet/ruview (concept) — using subcarriers individually instead of one
//     scalar. ruview itself needs a multi-node mesh + off-device ML; only the
//     single-link "use the subcarriers" idea is borrowed here. No code copied.
//   - espressif/esp-csi (Apache-2.0) — official ESP32-S3 CSI reference.

#include "csidetect.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <math.h>
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "wguard.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern LGFX           tft;            // global display from main.ino
extern WGuard         wGuard;

// ── tunables ──────────────────────────────────────────────────────────────────
#define CSI_WINDOW      40            // global variance window (presence core)
#define CSI_BANDS       8             // per-subcarrier bands → radar sectors
#define CSI_HOLD_MS     1200          // presence coast (snappy real-time feel)
#define CSI_FPS_MS      33            // ~30 fps render
#define CSI_SWEEP_MS    2600.0f       // one radar revolution (lively)
#define TAU             6.28318531f

// sprite radar region (pushed below the header)
#define SPR_W           198
#define SPR_H           188
#define SPR_OX          0
#define SPR_OY          40
#define SC_X            99            // disc centre within the sprite
#define SC_Y            94
#define RAD_R           86
#define PANEL_X         204           // right-hand stats (drawn straight to tft)

// palette (RGB565)
#define C_BG            0x0140
#define C_GRID          0x0320
#define C_GRID2         0x0220
#define C_RING          0x05E5

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ── CSI state (written by the WiFi-task CSI callback, read in the loop) ────────
static float            gAmpBuf[CSI_WINDOW];
static float            gPhaBuf[CSI_WINDOW];
static int              gAmpIdx, gAmpFilled;
static float            gVarMax, gVarMin, gPhaVarMax, gPhaVarMin;
static volatile float   gMotion;                 // global blended 0..1 (presence)
static volatile int8_t  gRssi;
static volatile uint32_t gCount;                 // CSI frames seen (bring-up diag)

// per-band (responsive EMA variance → 8 sector intensities)
static float            gBMean[CSI_BANDS], gBVar[CSI_BANDS];
static float            gBVMin[CSI_BANDS], gBVMax[CSI_BANDS];
static volatile float   gBand[CSI_BANDS];        // normalized 0..1 per band

// approach/recede trend (amplitude fast vs slow EMA)
static float            gAmpFast, gAmpSlow;
static volatile float   gTrend;                  // >0 approaching, <0 receding (hint)

// ── UI state ──────────────────────────────────────────────────────────────────
static float    gBDisp[CSI_BANDS];               // peak-hold + decay for blips
static esp_err_t gCsiErr;
static LGFX_Sprite gRadar;

static void csiResetStats() {
    gAmpIdx = 0; gAmpFilled = 0;
    gVarMax = 0.001f; gVarMin = 0.0f;
    gPhaVarMax = 0.001f; gPhaVarMin = 0.0f;
    memset(gAmpBuf, 0, sizeof(gAmpBuf));
    memset(gPhaBuf, 0, sizeof(gPhaBuf));
    for (int b = 0; b < CSI_BANDS; b++) {
        gBMean[b] = 0; gBVar[b] = 0; gBVMin[b] = 0; gBVMax[b] = 0.001f; gBand[b] = 0;
    }
    gAmpFast = gAmpSlow = 0; gTrend = 0;
}

// ── CSI receive callback (WiFi task; IRAM like the reference) ──────────────────
static void IRAM_ATTR csiCb(void*, wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len < 8) return;
    gCount++;
    int8_t* b      = info->buf;
    int     nPairs = info->len / 2;              // I/Q int8 pairs (subcarriers)

    float ampSum = 0.0f, sinSum = 0.0f;
    int   valid  = 0;
    float bAmp[CSI_BANDS] = {0}; int bCnt[CSI_BANDS] = {0};
    for (int i = 0; i < nPairs; i++) {
        float r   = (float)b[2 * i];
        float im  = (float)b[2 * i + 1];
        float amp = sqrtf(r * r + im * im);
        ampSum += amp;
        if (amp > 1e-4f) { sinSum += im / amp; valid++; }
        int bi = (CSI_BANDS * i) / nPairs; if (bi >= CSI_BANDS) bi = CSI_BANDS - 1;
        bAmp[bi] += amp; bCnt[bi]++;
    }
    float meanAmp = ampSum / (float)nPairs;
    float meanSin = valid > 0 ? sinSum / (float)valid : 0.0f;

    // ── global presence core (windowed variance + asymmetric EMA) ─────────────
    gAmpBuf[gAmpIdx] = meanAmp;
    gPhaBuf[gAmpIdx] = meanSin;
    gAmpIdx = (gAmpIdx + 1) % CSI_WINDOW;
    if (gAmpFilled < CSI_WINDOW) gAmpFilled++;
    int n = gAmpFilled;

    float s = 0; for (int i = 0; i < n; i++) s += gAmpBuf[i];
    float mean = s / (float)n;
    float var = 0; for (int i = 0; i < n; i++) { float d = gAmpBuf[i] - mean; var += d * d; }
    var /= (float)n;
    float ps = 0; for (int i = 0; i < n; i++) ps += gPhaBuf[i];
    float pmean = ps / (float)n;
    float pvar = 0; for (int i = 0; i < n; i++) { float d = gPhaBuf[i] - pmean; pvar += d * d; }
    pvar /= (float)n;

    if (gVarMin < 0.0001f) gVarMin = var;
    else gVarMin += (var - gVarMin) * ((var < gVarMin) ? 0.1f : 0.002f);
    if (var > gVarMax) gVarMax = var; else gVarMax += (var - gVarMax) * 0.005f;
    float rng = gVarMax - gVarMin;
    float ampMotion = (rng > 0.0001f) ? (var - gVarMin) / rng : 0.0f;

    if (gPhaVarMin < 0.0001f) gPhaVarMin = pvar;
    else gPhaVarMin += (pvar - gPhaVarMin) * ((pvar < gPhaVarMin) ? 0.1f : 0.002f);
    if (pvar > gPhaVarMax) gPhaVarMax = pvar; else gPhaVarMax += (pvar - gPhaVarMax) * 0.005f;
    float prng = gPhaVarMax - gPhaVarMin;
    float phaMotion = (prng > 0.0001f) ? (pvar - gPhaVarMin) / prng : 0.0f;

    gMotion = clamp01(0.6f * clamp01(ampMotion) + 0.4f * clamp01(phaMotion));
    gRssi   = info->rx_ctrl.rssi;

    // ── per-band responsive variance (drives the radar contacts) ──────────────
    for (int bb = 0; bb < CSI_BANDS; bb++) {
        float x = bCnt[bb] ? bAmp[bb] / (float)bCnt[bb] : 0.0f;
        float d = x - gBMean[bb];
        gBMean[bb] += 0.15f * d;
        float v = d * d;
        gBVar[bb] += 0.18f * (v - gBVar[bb]);
        if (gBVMin[bb] < 1e-6f) gBVMin[bb] = gBVar[bb];
        else gBVMin[bb] += (gBVar[bb] - gBVMin[bb]) * ((gBVar[bb] < gBVMin[bb]) ? 0.1f : 0.003f);
        if (gBVar[bb] > gBVMax[bb]) gBVMax[bb] = gBVar[bb];
        else gBVMax[bb] += (gBVar[bb] - gBVMax[bb]) * 0.01f;
        float br = gBVMax[bb] - gBVMin[bb];
        gBand[bb] = clamp01((br > 1e-6f) ? (gBVar[bb] - gBVMin[bb]) / br : 0.0f);
    }

    // ── approach/recede trend (amplitude fast vs slow) ────────────────────────
    gAmpFast += 0.30f * (meanAmp - gAmpFast);
    gAmpSlow += 0.02f * (meanAmp - gAmpSlow);
    gTrend = gAmpFast - gAmpSlow;
}

static void IRAM_ATTR csiPromiscCb(void*, wifi_promiscuous_pkt_type_t) {}

// ── CSI radio setup / teardown ────────────────────────────────────────────────
static void csiStart() {
    csiResetStats();
    gMotion = 0.0f; gCount = 0;
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t pf = {};
    pf.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&pf);
    esp_wifi_set_promiscuous_rx_cb(csiPromiscCb);
    // wifi_csi_config_t — IDF 4.4 fields (platform = espressif32 6.x).
    wifi_csi_config_t cfg = {};
    cfg.lltf_en = true; cfg.htltf_en = true; cfg.stbc_htltf2_en = true;
    cfg.ltf_merge_en = true; cfg.channel_filter_en = true;
    cfg.manu_scale = false; cfg.shift = 0;
    esp_wifi_set_csi_config(&cfg);
    esp_wifi_set_csi_rx_cb(csiCb, nullptr);
    gCsiErr = esp_wifi_set_csi(true);
}

static void csiStop() {
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
}

// ── colour ramps ──────────────────────────────────────────────────────────────
// green→yellow→red by intensity 0..1
static uint16_t heatColor(float v) {
    v = clamp01(v);
    if (v > 0.85f) return 0xF904;   // hot red-orange
    if (v > 0.65f) return 0xFEC0;   // amber
    if (v > 0.40f) return 0x6FE8;   // bright green
    if (v > 0.18f) return 0x05E8;   // mid green
    return 0x0303;                  // faint green
}
static uint16_t sweepShade(float br) {
    br = clamp01(br);
    if (br > 0.80f) return 0x9FF8;
    if (br > 0.55f) return 0x2F8A;
    if (br > 0.30f) return 0x0BC6;
    return 0x0322;
}

// ── radar render (into the sprite) ────────────────────────────────────────────
static void csiDrawRadar(bool present, float sweepFrac) {
    auto& g = gRadar;
    g.fillScreen(C_BG);

    // range rings + crosshair + radial spokes (game grid)
    g.drawCircle(SC_X, SC_Y, RAD_R, C_RING);
    g.drawCircle(SC_X, SC_Y, (RAD_R * 2) / 3, C_GRID);
    g.drawCircle(SC_X, SC_Y, RAD_R / 3, C_GRID);
    for (int s = 0; s < CSI_BANDS; s++) {
        float a = (float)s / CSI_BANDS * TAU;
        g.drawLine(SC_X, SC_Y, SC_X + (int)(cosf(a) * RAD_R), SC_Y + (int)(sinf(a) * RAD_R), C_GRID2);
    }

    // sweeping cone — fan of fading triangles behind a bright leading edge
    float sa = sweepFrac * TAU;
    const int FAN = 9;
    const float step = 0.065f;
    for (int i = FAN; i >= 1; i--) {
        float a0 = sa - (float)i * step;
        float a1 = sa - (float)(i - 1) * step;
        uint16_t col = sweepShade(1.0f - (float)i / FAN);
        g.fillTriangle(SC_X, SC_Y,
                       SC_X + (int)(cosf(a0) * RAD_R), SC_Y + (int)(sinf(a0) * RAD_R),
                       SC_X + (int)(cosf(a1) * RAD_R), SC_Y + (int)(sinf(a1) * RAD_R), col);
    }
    g.drawLine(SC_X, SC_Y, SC_X + (int)(cosf(sa) * RAD_R), SC_Y + (int)(sinf(sa) * RAD_R), 0xBFFA);

    // band contacts — each sector flashes brighter as the sweep crosses it
    for (int bd = 0; bd < CSI_BANDS; bd++) {
        float inten = gBDisp[bd];
        if (inten < 0.06f) continue;
        float ang = (float)bd / CSI_BANDS * TAU;
        // sweep proximity boost (classic "sweep reveals contact")
        float da = fabsf(ang - sa);
        while (da > TAU) da -= TAU;
        if (da > TAU / 2) da = TAU - da;
        float prox = 1.0f - clamp01(da / (TAU / (CSI_BANDS)));   // 1 when aligned
        float show = clamp01(inten * (0.55f + 0.55f * prox));
        float rad  = RAD_R * (0.82f - 0.55f * inten);   // stronger reaction → nearer centre
        int x = SC_X + (int)(cosf(ang) * rad);
        int y = SC_Y + (int)(sinf(ang) * rad);
        uint16_t col = heatColor(show);
        int sz = inten > 0.6f ? 4 : (inten > 0.3f ? 3 : 2);
        if (show > 0.5f) g.drawCircle(x, y, sz + 3, col);        // contact halo
        g.fillCircle(x, y, sz, col);
    }

    // centre reticle — CLEAR green / CONTACT red (pulses)
    uint16_t cc = present ? TFT_RED : 0x2FE8;
    if (present) {
        int pr = 4 + (int)(3 * (0.5f + 0.5f * sinf((float)millis() / 120.0f)));
        g.drawCircle(SC_X, SC_Y, pr, TFT_RED);
    }
    g.drawLine(SC_X - 4, SC_Y, SC_X + 4, SC_Y, cc);
    g.drawLine(SC_X, SC_Y - 4, SC_X, SC_Y + 4, cc);
    g.fillCircle(SC_X, SC_Y, 2, cc);

    g.pushSprite(&tft, SPR_OX, SPR_OY);
}

// ── header / panel / footer (straight to tft) ─────────────────────────────────
static void csiHeader() {
    auto& dm = displayManager;
    dm.setCursor(6, 32);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("CSI");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("MOTION");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    dm.setTextColor(0x4A66);     dm.printText("exp - motion only, not a radar");
}

static const char* activityWord(float m) {
    if (m < 0.12f) return "STILL ";
    if (m < 0.32f) return "FIDGET";
    if (m < 0.62f) return "WALK  ";
    return "RUN   ";
}

static void csiDrawPanel(float thresh, float disp, bool present, int zones) {
    auto& dm = displayManager;
    dm.fillRect(PANEL_X, 44, SCREEN_WIDTH - PANEL_X, 184, TFT_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(present ? TFT_RED : 0x2FE8);
    tft.setCursor(PANEL_X, 48);
    tft.print(present ? "CONTACT" : " CLEAR");
    tft.setTextSize(1);
    dm.setDefaultTextSize();

    char b[24];
    // activity + trend
    dm.setTextColor(present ? TFT_YELLOW : 0x7BEF);
    dm.setCursor(PANEL_X, 74); dm.printText(activityWord(disp));
    float tr = gTrend;
    const char* arrow = (tr > 0.6f) ? "^near" : (tr < -0.6f) ? "vfar " : "~hold";
    dm.setTextColor(0x6E6C); dm.setCursor(PANEL_X + 60, 74); dm.printText(arrow);

    // zones (active signal sectors — NOT a person count)
    snprintf(b, sizeof(b), "zones:%d", zones);
    dm.setTextColor(zones > 0 ? 0x6FE8 : 0x7BEF);
    dm.setCursor(PANEL_X, 90); dm.printText(b);

    int mp = (int)(disp * 100.0f), tp = (int)(thresh * 100.0f);
    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 110); dm.printText("MOTION");
    snprintf(b, sizeof(b), "%3d%%", mp);
    dm.setTextColor(mp >= tp ? 0x6FE8 : 0xC618);
    dm.setCursor(PANEL_X + 70, 110); dm.printText(b);
    dm.fillRect(PANEL_X, 122, 108, 8, 0x0c22);
    dm.fillRect(PANEL_X, 122, (108 * mp) / 100, 8, 0x6FE8);

    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 138); dm.printText("THRESH");
    snprintf(b, sizeof(b), "%3d%%", tp);
    dm.setTextColor(TFT_YELLOW);
    dm.setCursor(PANEL_X + 70, 138); dm.printText(b);
    dm.fillRect(PANEL_X, 150, 108, 8, 0x2104);
    dm.fillRect(PANEL_X, 150, (108 * tp) / 100, 8, 0x8400);
    int mx = PANEL_X + (108 * tp) / 100;
    tft.drawLine(mx, 148, mx, 159, TFT_YELLOW);

    // bring-up diagnostics
    uint32_t fr = gCount;
    snprintf(b, sizeof(b), "fr:%lu %ddBm", (unsigned long)fr, (int)gRssi);
    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 180); dm.printText(b);
    if (gCsiErr != ESP_OK) {
        dm.setTextColor(TFT_RED);
        snprintf(b, sizeof(b), "CSI ERR %d", (int)gCsiErr);
        dm.setCursor(PANEL_X, 196); dm.printText(b);
    } else if (fr == 0) {
        dm.setTextColor(TFT_ORANGE);
        dm.setCursor(PANEL_X, 196); dm.printText("no frames-");
        dm.setCursor(PANEL_X, 208); dm.printText("need traffic");
    } else {
        dm.setTextColor(0x6FE8);
        dm.setCursor(PANEL_X, 196); dm.printText("CSI live");
    }
}

static void csiDrawFooter() {
    auto& dm = displayManager;
    dm.fillRect(0, 229, SCREEN_WIDTH, 11, TFT_BLACK);
    dm.setTextColor(TFT_DARKGREY);
    dm.setCursor(6, 230); dm.printText("a/l/ball=sens  c=cal  h=help  q=quit");
}

// full-screen help overlay ([h]); any key / click returns
static void csiHelp() {
    auto& dm = displayManager;
    if (dm.isBlocked()) return;
    auto line = [&](int n, uint16_t col, const char* t) {
        dm.setTextColor(col); dm.setCursor(6, outputY + n * LINE_HEIGHT); dm.printText(t);
    };
    bool redraw = true;
    while (true) {
        if (redraw && !dm.isBlocked()) {
            dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
            dm.setCursor(6, outputY);
            dm.setTextColor(0x7BEF);     dm.printText("[");
            dm.setTextColor(TFT_CYAN);   dm.printText("CSI");
            dm.setTextColor(0x7BEF);     dm.printText("::");
            dm.setTextColor(TFT_YELLOW); dm.printText("HELP");
            dm.setTextColor(0x7BEF);     dm.printText("]");
            line(1,  TFT_WHITE,    "WiFi motion detector.");
            line(2,  TFT_WHITE,    "Senses movement via WiFi echoes.");
            line(3,  TFT_CYAN,     "USE: cw <ssid>   then   csi");
            line(4,  0x6FE8,       "STILL  = CLEAR (green)");
            line(5,  TFT_RED,      "MOVING = CONTACT (red)");
            line(6,  TFT_WHITE,    "MOTION% = amount of movement");
            line(7,  TFT_WHITE,    "blips  = signal sectors (rough)");
            line(8,  TFT_CYAN,     "KEYS: a/l or trackball = sens");
            line(9,  TFT_WHITE,    "      c=recalibrate    q=quit");
            line(10, 0x7BEF,       "Motion only: a still person can");
            line(11, 0x7BEF,       "read CLEAR. Sectors are approx,");
            line(12, 0x7BEF,       "not a real compass bearing.");
            line(13, TFT_DARKGREY, "press any key to return");
            redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k || tb == TBALL_CLICK) break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── entry ─────────────────────────────────────────────────────────────────────
void runCsiDetect(char* /*args*/) {
    auto& dm = displayManager;

    if (WiFi.status() != WL_CONNECTED) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.println("CSI radar needs WiFi.");
        dm.setTextColor(TFT_WHITE); dm.println("Connect first:  cw <ssid>");
        dm.printCommandScreen();
        return;
    }
    if (wGuard.isBackground()) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.println("Stop wguard bg first:");
        dm.setTextColor(TFT_WHITE); dm.println("  wg stop");
        dm.printCommandScreen();
        return;
    }

    // double-buffer sprite for the radar (PSRAM — radar region only)
    gRadar.setPsram(true);
    gRadar.setColorDepth(16);
    if (!gRadar.createSprite(SPR_W, SPR_H)) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED); dm.println("Radar: sprite alloc failed");
        dm.printCommandScreen();
        return;
    }
    memset(gBDisp, 0, sizeof(gBDisp));

    dm.clearScreen();
    dm.updateStatusBar();
    dm.setDefaultTextSize();
    csiHeader();
    csiDrawFooter();
    csiStart();

    float    thresh   = 0.15f;
    float    held     = 0.0f;
    uint32_t holdUntil = 0;
    uint32_t lastTick = 0;

    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') break;
        // sensitivity: 'l' / trackball up = more sensitive, 'a' / down = less
        if (k == 'l' || k == 'L' || tb == TBALL_UP)        { thresh -= 0.05f; if (thresh < 0.05f) thresh = 0.05f; }
        else if (k == 'a' || k == 'A' || tb == TBALL_DOWN) { thresh += 0.05f; if (thresh > 0.95f) thresh = 0.95f; }
        else if (k == 'c' || k == 'C')                      csiResetStats();
        else if (k == 'h' || k == 'H') {
            csiHelp();                                       // modal; redraw statics after
            dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
            csiHeader(); csiDrawFooter();
        }

        uint32_t now = millis();
        if (now - lastTick < CSI_FPS_MS) { delay(3); continue; }
        lastTick = now;

        // snappy presence: trip instantly, coast a short while
        float raw = gMotion;
        if (raw > thresh) { holdUntil = now + CSI_HOLD_MS; held = raw; }
        bool present = (now < holdUntil);
        float disp = (raw > thresh) ? raw
                   : (present ? held * (0.15f + 0.85f * (float)(holdUntil - now) / CSI_HOLD_MS) : 0.0f);

        // Sector contacts: only when there's real (global) motion, and only the
        // bands reacting ABOVE the average light up → the standout sectors give a
        // rough, repeatable "where" (not true bearing). Quiet room → empty scope.
        float bsum = 0.0f;
        for (int bd = 0; bd < CSI_BANDS; bd++) bsum += gBand[bd];
        float bmean = bsum / CSI_BANDS;
        int zones = 0;
        for (int bd = 0; bd < CSI_BANDS; bd++) {
            float rel = gBand[bd] - bmean;                       // standout reaction
            float v = present ? clamp01(rel * 2.4f) : 0.0f;      // gate by real motion
            gBDisp[bd] = v > gBDisp[bd] ? v : gBDisp[bd] * 0.86f;
            if (gBDisp[bd] > 0.20f) zones++;
        }

        float sweepFrac = (float)(now % (uint32_t)CSI_SWEEP_MS) / CSI_SWEEP_MS;

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
            csiHeader(); csiDrawFooter();
        }
        if (!dm.isBlocked()) {
            csiDrawRadar(present, sweepFrac);
            csiDrawPanel(thresh, disp, present, zones);
        }
    }

    csiStop();
    gRadar.deleteSprite();
    dm.clearScreen();                 // wipe the sweep display before returning to CLI
    dm.printCommandScreen();
}
