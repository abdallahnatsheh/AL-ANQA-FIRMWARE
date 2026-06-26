// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// csidetect / csi (alias hd) — WiFi CSI human-presence detector with a radar UI.
//
// How it works: every received WiFi frame carries Channel State Information
// (amplitude/phase of the OFDM subcarriers). A moving body perturbs the room's
// multipath, so the CSI variance rises. We track that variance, self-calibrate
// a floor/ceiling, and turn it into a 0..1 motion score → presence.
//
// IMPORTANT (honesty): a single antenna gives NO direction and CANNOT separate
// individuals — CSI collapses the whole room into ONE motion-energy signal. The
// radar is therefore a phosphor scope where the sweep angle = TIME and a blip's
// distance = motion intensity. It is NOT direction finding. (The reference does
// the same: blips get pseudo-random angles, never real bearings.)
//
// Sources (rule: credit what we learn from):
//   - skizzophrenic/Cardputer-CSI-Human-Detector (MIT) — CSI acquisition config,
//     the amplitude/phase variance + asymmetric-EMA normalization algorithm, and
//     the hold/coast presence logic are adapted from its single-device CSI path.
//   - espressif/esp-csi (Apache-2.0) — official ESP32-S3 CSI reference.
//   No code copied verbatim; algorithm + constants reimplemented for T-REX.

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

// ── tunables (from the reference) ─────────────────────────────────────────────
#define CSI_WINDOW      50            // sliding-window frames for variance
#define CSI_SLOTS       90            // radar phosphor history slots (one per 4deg)
#define CSI_HOLD        150           // ~10 s of coast at 15 Hz
#define CSI_HZ_MS       66            // ~15 Hz service tick
#define CSI_SWEEP_MS    6000.0f       // one radar revolution
#define TAU             6.28318531f

// radar geometry (320x240, status bar 0-30, content from outputY=38)
#define RAD_CX          106
#define RAD_CY          142
#define RAD_R           78
#define PANEL_X         196

// green phosphor palette (RGB565)
#define C_RING          0x0BA8
#define C_DIM           0x0320
#define C_MID           0x05E5
#define C_BRT           0x5FF6
#define C_SWEEP         0x9FF8

// ── CSI state (written by the WiFi-task CSI callback, read in the loop) ────────
static float            gAmpBuf[CSI_WINDOW];
static float            gPhaBuf[CSI_WINDOW];
static int              gAmpIdx     = 0;
static int              gAmpFilled  = 0;
static float            gVarMax     = 0.001f, gVarMin    = 0.0f;
static float            gPhaVarMax  = 0.001f, gPhaVarMin = 0.0f;
static volatile float   gMotion     = 0.0f;   // blended 0..1
static volatile int8_t  gRssi       = 0;
static volatile uint32_t gCount     = 0;      // CSI frames seen (bring-up diag)

// ── UI state ──────────────────────────────────────────────────────────────────
static float    gSlot[CSI_SLOTS];             // phosphor motion per angular slot
static esp_err_t gCsiErr = ESP_OK;            // result of esp_wifi_set_csi(true)

// Reset the adaptive baseline + window (init AND [c] calibrate).
static void csiResetStats() {
    gAmpIdx = 0; gAmpFilled = 0;
    gVarMax = 0.001f; gVarMin = 0.0f;
    gPhaVarMax = 0.001f; gPhaVarMin = 0.0f;
    memset(gAmpBuf, 0, sizeof(gAmpBuf));
    memset(gPhaBuf, 0, sizeof(gPhaBuf));
}

// ── CSI receive callback (runs in the WiFi task; kept in IRAM like the ref) ────
static void IRAM_ATTR csiCb(void*, wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len < 4) return;
    gCount++;
    int8_t* b      = info->buf;
    int     nPairs = info->len / 2;          // I/Q int8 pairs per subcarrier

    float ampSum = 0.0f, sinSum = 0.0f;
    int   valid  = 0;
    for (int i = 0; i < nPairs; i++) {
        float r   = (float)b[2 * i];
        float im  = (float)b[2 * i + 1];
        float amp = sqrtf(r * r + im * im);
        ampSum += amp;
        if (amp > 1e-4f) { sinSum += im / amp; valid++; }
    }
    float meanAmp = ampSum / (float)nPairs;
    float meanSin = valid > 0 ? sinSum / (float)valid : 0.0f;

    gAmpBuf[gAmpIdx] = meanAmp;
    gPhaBuf[gAmpIdx] = meanSin;
    gAmpIdx = (gAmpIdx + 1) % CSI_WINDOW;
    if (gAmpFilled < CSI_WINDOW) gAmpFilled++;
    int n = gAmpFilled;

    // amplitude variance over the window
    float s = 0.0f; for (int i = 0; i < n; i++) s += gAmpBuf[i];
    float mean = s / (float)n;
    float var = 0.0f; for (int i = 0; i < n; i++) { float d = gAmpBuf[i] - mean; var += d * d; }
    var /= (float)n;

    // phase variance over the window
    float ps = 0.0f; for (int i = 0; i < n; i++) ps += gPhaBuf[i];
    float pmean = ps / (float)n;
    float pvar = 0.0f; for (int i = 0; i < n; i++) { float d = gPhaBuf[i] - pmean; pvar += d * d; }
    pvar /= (float)n;

    // asymmetric-EMA self-calibration → normalize each to 0..1
    if (gVarMin < 0.0001f) gVarMin = var;
    else gVarMin += (var - gVarMin) * ((var < gVarMin) ? 0.1f : 0.002f);
    if (var > gVarMax) gVarMax = var; else gVarMax += (var - gVarMax) * 0.005f;
    float range = gVarMax - gVarMin;
    float ampMotion = (range > 0.0001f) ? (var - gVarMin) / range : 0.0f;
    if (ampMotion < 0.0f) ampMotion = 0.0f; if (ampMotion > 1.0f) ampMotion = 1.0f;

    if (gPhaVarMin < 0.0001f) gPhaVarMin = pvar;
    else gPhaVarMin += (pvar - gPhaVarMin) * ((pvar < gPhaVarMin) ? 0.1f : 0.002f);
    if (pvar > gPhaVarMax) gPhaVarMax = pvar; else gPhaVarMax += (pvar - gPhaVarMax) * 0.005f;
    float prange = gPhaVarMax - gPhaVarMin;
    float phaMotion = (prange > 0.0001f) ? (pvar - gPhaVarMin) / prange : 0.0f;
    if (phaMotion < 0.0f) phaMotion = 0.0f; if (phaMotion > 1.0f) phaMotion = 1.0f;

    gMotion = 0.6f * ampMotion + 0.4f * phaMotion;
    gRssi   = info->rx_ctrl.rssi;
}

// no-op promiscuous sink — CSI needs promiscuous enabled to see all frames
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

    // wifi_csi_config_t — IDF 4.4 field set (platform = espressif32 6.x). If a
    // future core bump fails to compile here, the struct was renamed in IDF 5.2+.
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
    // keep the STA connection up — only undo what we added.
}

// ── header (cyberpunk style) ──────────────────────────────────────────────────
static void csiHeader() {
    auto& dm = displayManager;
    dm.setCursor(6, 34);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("CSI");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("RADAR");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    dm.setTextColor(0x4A66);     dm.printText("motion, no bearing");
}

// pick a phosphor shade for a 0..1 brightness level
static uint16_t csiShade(float level) {
    if (level > 0.66f) return C_BRT;
    if (level > 0.40f) return TFT_GREEN;
    if (level > 0.16f) return C_MID;
    return C_DIM;
}

// ── radar render ──────────────────────────────────────────────────────────────
static void csiDrawRadar(float dispMotion, bool present, float sweepFrac) {
    // disc backdrop + range rings + crosshair
    tft.fillCircle(RAD_CX, RAD_CY, RAD_R, TFT_BLACK);
    tft.drawCircle(RAD_CX, RAD_CY, RAD_R, C_RING);
    tft.drawCircle(RAD_CX, RAD_CY, (RAD_R * 2) / 3, 0x0320);
    tft.drawCircle(RAD_CX, RAD_CY, RAD_R / 3, 0x0320);
    tft.drawLine(RAD_CX - RAD_R, RAD_CY, RAD_CX + RAD_R, RAD_CY, 0x0220);
    tft.drawLine(RAD_CX, RAD_CY - RAD_R, RAD_CX, RAD_CY + RAD_R, 0x0220);

    int curSlot = (int)(sweepFrac * CSI_SLOTS) % CSI_SLOTS;

    // phosphor blips: angle = the time the sweep laid them, radius = intensity
    for (int i = 0; i < CSI_SLOTS; i++) {
        float m = gSlot[i];
        if (m < 0.06f) continue;
        float ang = (float)i / CSI_SLOTS * TAU - TAU / 4.0f;
        float rad = RAD_R * (0.16f + 0.80f * (m > 1.0f ? 1.0f : m));
        int   recent = (curSlot - i + CSI_SLOTS) % CSI_SLOTS;
        float fresh = 1.0f - (float)recent / CSI_SLOTS;     // 1=just drawn
        int x = RAD_CX + (int)(cosf(ang) * rad);
        int y = RAD_CY + (int)(sinf(ang) * rad);
        uint16_t col = csiShade(fresh * (0.45f + 0.55f * m));
        tft.fillCircle(x, y, m > 0.55f ? 2 : 1, col);
    }

    // sweep arm + short trailing fade
    float sa = sweepFrac * TAU - TAU / 4.0f;
    for (int t = 2; t >= 0; t--) {
        float a = sa - t * 0.13f;
        uint16_t col = (t == 0) ? C_SWEEP : (t == 1 ? TFT_GREEN : C_MID);
        tft.drawLine(RAD_CX, RAD_CY,
                     RAD_CX + (int)(cosf(a) * RAD_R),
                     RAD_CY + (int)(sinf(a) * RAD_R), col);
    }

    // centre reticle
    uint16_t cc = present ? TFT_RED : TFT_GREEN;
    tft.drawLine(RAD_CX - 3, RAD_CY, RAD_CX + 3, RAD_CY, cc);
    tft.drawLine(RAD_CX, RAD_CY - 3, RAD_CX, RAD_CY + 3, cc);
    tft.fillCircle(RAD_CX, RAD_CY, 2, cc);
}

// ── side panel ────────────────────────────────────────────────────────────────
static void csiDrawPanel(float thresh, float dispMotion, bool present) {
    auto& dm = displayManager;
    dm.fillRect(PANEL_X, 60, SCREEN_WIDTH - PANEL_X, 158, TFT_BLACK);

    // big presence state
    tft.setTextSize(2);
    tft.setTextColor(present ? TFT_RED : TFT_GREEN);
    tft.setCursor(PANEL_X, 66);
    tft.print(present ? "CONTACT" : " CLEAR");
    tft.setTextSize(1);
    dm.setDefaultTextSize();         // restore dm text state after the size-2 label

    int mp = (int)(dispMotion * 100.0f);
    int tp = (int)(thresh * 100.0f);

    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 96);  dm.printText("MOTION");
    char b[20];
    snprintf(b, sizeof(b), "%3d%%", mp);
    dm.setTextColor(mp >= tp ? TFT_GREEN : 0xC618);
    dm.setCursor(PANEL_X + 78, 96); dm.printText(b);
    dm.fillRect(PANEL_X, 108, 116, 8, 0x0c22);
    dm.fillRect(PANEL_X, 108, (116 * mp) / 100, 8, TFT_GREEN);

    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 124); dm.printText("THRESH");
    snprintf(b, sizeof(b), "%3d%%", tp);
    dm.setTextColor(TFT_YELLOW);
    dm.setCursor(PANEL_X + 78, 124); dm.printText(b);
    dm.fillRect(PANEL_X, 136, 116, 8, 0x2104);
    dm.fillRect(PANEL_X, 136, (116 * tp) / 100, 8, 0x8400);
    int mx = PANEL_X + (116 * tp) / 100;
    tft.drawLine(mx, 134, mx, 145, TFT_YELLOW);

    // bring-up diagnostics: frames + rssi + CSI enable status
    uint32_t fr = gCount;
    snprintf(b, sizeof(b), "fr:%lu  %ddBm", (unsigned long)fr, (int)gRssi);
    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 168); dm.printText(b);

    if (gCsiErr != ESP_OK) {
        dm.setTextColor(TFT_RED);
        snprintf(b, sizeof(b), "CSI ERR %d", (int)gCsiErr);
        dm.setCursor(PANEL_X, 184); dm.printText(b);
    } else if (fr == 0) {
        dm.setTextColor(TFT_ORANGE);
        dm.setCursor(PANEL_X, 184); dm.printText("CSI on, no frames");
        dm.setCursor(PANEL_X, 196); dm.printText("need wifi traffic");
    } else {
        dm.setTextColor(TFT_GREEN);
        dm.setCursor(PANEL_X, 184); dm.printText("CSI live");
    }
}

static void csiDrawFooter() {
    auto& dm = displayManager;
    dm.fillRect(0, 224, SCREEN_WIDTH, 14, TFT_BLACK);
    dm.setTextColor(TFT_DARKGREY);
    dm.setCursor(6, 226); dm.printText("[ []thr  [c]cal  [q]quit   sweep=time");
}

// ── entry ─────────────────────────────────────────────────────────────────────
void runCsiDetect(char* /*args*/) {
    auto& dm = displayManager;

    // CSI needs WiFi frames → must be connected (STA). Stay on the AP's channel.
    if (WiFi.status() != WL_CONNECTED) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.println("CSI radar needs WiFi.");
        dm.setTextColor(TFT_WHITE); dm.println("Connect first:  cw <ssid>");
        dm.printCommandScreen();
        return;
    }
    // wguard bg owns the promiscuous rx cb — we'd clobber it. Tell the user.
    if (wGuard.isBackground()) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.println("Stop wguard bg first:");
        dm.setTextColor(TFT_WHITE); dm.println("  wg stop");
        dm.printCommandScreen();
        return;
    }

    memset(gSlot, 0, sizeof(gSlot));
    dm.clearScreen();
    dm.updateStatusBar();
    dm.setDefaultTextSize();
    csiHeader();
    csiStart();

    float    thresh   = 0.15f;       // reference default
    float    held     = 0.0f;
    int      hold     = 0;
    uint32_t lastTick = 0;

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == '[')                 { thresh -= 0.05f; if (thresh < 0.05f) thresh = 0.05f; }
        else if (k == ']')            { thresh += 0.05f; if (thresh > 0.95f) thresh = 0.95f; }
        else if (k == 'c' || k == 'C') csiResetStats();

        uint32_t now = millis();
        if (now - lastTick < CSI_HZ_MS) { delay(4); continue; }
        lastTick = now;

        // hold/coast presence (reference logic)
        float raw = gMotion;
        if (raw > thresh) { hold = CSI_HOLD; held = raw; }
        else if (hold > 0) hold--;
        float disp = (raw > thresh) ? raw
                   : (hold > 0 ? held * (0.10f + 0.90f * (float)hold / CSI_HOLD) : 0.0f);
        bool present = (hold > 0);

        // lay the (coasted) motion into the slot the sweep is passing
        float sweepFrac = (float)(now % (uint32_t)CSI_SWEEP_MS) / CSI_SWEEP_MS;
        gSlot[(int)(sweepFrac * CSI_SLOTS) % CSI_SLOTS] = disp;

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize(); csiHeader();
        }
        if (!dm.isBlocked()) {
            csiHeader();
            csiDrawRadar(disp, present, sweepFrac);
            csiDrawPanel(thresh, disp, present);
            csiDrawFooter();
        }
    }

    csiStop();
    dm.printCommandScreen();
}
