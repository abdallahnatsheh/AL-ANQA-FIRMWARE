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
//   - espressif/esp-csi (Apache-2.0) — official ESP32-S3 CSI reference. NOTE:
//     Espressif's newer esp_wifi_sensing / esp-radar components need IDF >= 5.4;
//     this project is IDF 4.4 (Arduino core 2.x) so they cannot be used — the
//     ideas below are re-implemented as portable math instead.
//   - francescopace/ESPectre (concept/method, GPL-3.0) — methodology reference,
//     no code copied: (1) NBVI-style auto subcarrier weighting (emphasise the
//     most motion-responsive subcarriers via normalized per-subcarrier variance,
//     instead of averaging all equally); (2) Hampel outlier rejection on the
//     motion stream; (3) fully PASSIVE operation — sense off any AP's beacons
//     with no association. Our `csi auto` mode + single-source MAC lock follow it.

#include "csidetect.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include "esp_wifi.h"
#include <math.h>
#include "display_manager.h"
#include "utils.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include "wifi_sd_guard.h"            // ScopedPromiscPause — GDMA-safe SD writes
#include "wguard.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;
extern LGFX           tft;            // global display from main.ino
extern WGuard         wGuard;

// ── tunables ──────────────────────────────────────────────────────────────────
#define CSI_WINDOW      40            // global variance window (presence core)
#define CSI_BANDS       8             // per-subcarrier bands → radar sectors
#define CSI_MAXSC       128           // max subcarriers tracked (HT40 ~128, HT20 ~64)
#define CSI_HOLD_MS     1200          // presence coast (snappy real-time feel)
#define CSI_FPS_MS      33            // ~30 fps render
#define CSI_SWEEP_MS    2600.0f       // one radar revolution (lively)
#define CSI_HAMPEL_N    7             // Hampel outlier-reject window (motion stream)
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

// ── NBVI auto subcarrier weighting (ESPectre method, re-implemented) ───────────
// Per-subcarrier EMA mean/var → weight = normalized variance (scVar/scMean^2).
// Responsive subcarriers (high normalized variance) dominate the motion mean;
// static ones contribute ~0. Falls back to a uniform mean until warmed up.
static float            gScMean[CSI_MAXSC], gScVar[CSI_MAXSC];
static bool             gScWarm;                 // enough frames for weights to be valid
static volatile bool    gNbviOn = true;          // [n] toggles weighting on/off (A/B)

// ── single-source lock: only accept CSI from one AP (auto mode) ───────────────
// Avoids the "blend different transmitters" false-motion trap. Set before csiStart.
static volatile bool    gLockActive = false;
static uint8_t          gLockMac[6];
static char             gLockSsid[33];           // AP name for the log (full mac+ssid)
static bool             gAutoMode   = false;     // passive (beacon) vs connected link
static uint8_t          gChan       = 0;         // channel to park on in auto mode

// ── Hampel outlier filter state (motion stream; main-loop side) ───────────────
static float            gHampBuf[CSI_HAMPEL_N];
static int              gHampN, gHampIdx;

// ── UI state ──────────────────────────────────────────────────────────────────
static float    gBDisp[CSI_BANDS];               // peak-hold + decay for blips
static esp_err_t gCsiErr;
static LGFX_Sprite gRadar;

// ── adaptive threshold ([t] toggles) ──────────────────────────────────────────
// When on, the trip threshold tracks the quiet-room noise floor + a margin, so a
// noisier source (e.g. csi auto's beacon-rate jitter) auto-raises the bar.
static bool     gAdaptive = false;
static float    gNoiseEMA = 0.0f;                // EMA of the score during quiet
static float    gMargin   = 0.12f;               // adaptive headroom (a/l nudges it)

// ── SD logging ([s] toggles) — /apps/csidetect/NNN.csv, presence transitions ──
static bool     gLogOn = false;
static char     gLogPath[40];
static uint32_t gLogCount;
static bool     gPrevPresent = false;            // edge-detect CLEAR<->CONTACT

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
    memset(gScMean, 0, sizeof(gScMean));
    memset(gScVar,  0, sizeof(gScVar));
    gScWarm = false;
    memset(gHampBuf, 0, sizeof(gHampBuf));
    gHampN = 0; gHampIdx = 0;
    gNoiseEMA = 0.0f;                            // re-learn the quiet-room floor
}

// Hampel outlier filter on the motion stream — replaces a lone spike with the
// window median (median + MAD test). Runs on the main task, not the IRAM cb.
static float csiHampel(float x) {
    gHampBuf[gHampIdx] = x;
    gHampIdx = (gHampIdx + 1) % CSI_HAMPEL_N;
    if (gHampN < CSI_HAMPEL_N) gHampN++;
    if (gHampN < 5) return x;                       // not enough samples yet
    float t[CSI_HAMPEL_N];
    for (int i = 0; i < gHampN; i++) t[i] = gHampBuf[i];
    for (int i = 1; i < gHampN; i++) { float k = t[i]; int j = i - 1;
        while (j >= 0 && t[j] > k) { t[j+1] = t[j]; j--; } t[j+1] = k; }
    float med = t[gHampN / 2];
    float dev[CSI_HAMPEL_N];
    for (int i = 0; i < gHampN; i++) dev[i] = fabsf(t[i] - med);
    for (int i = 1; i < gHampN; i++) { float k = dev[i]; int j = i - 1;
        while (j >= 0 && dev[j] > k) { dev[j+1] = dev[j]; j--; } dev[j+1] = k; }
    float mad = dev[gHampN / 2];
    float thr = 3.0f * 1.4826f * mad;               // 3-sigma equivalent
    if (thr > 1e-4f && fabsf(x - med) > thr) return med;
    return x;
}

// ── CSI receive callback (WiFi task; IRAM like the reference) ──────────────────
static void IRAM_ATTR csiCb(void*, wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len < 8) return;
    // Single-source lock (auto/passive mode): only accept frames from the chosen
    // AP. Mixing CSI from different transmitters reads as motion even when still.
    if (gLockActive) {
        const uint8_t* m = info->mac;
        if (m[0]!=gLockMac[0]||m[1]!=gLockMac[1]||m[2]!=gLockMac[2]||
            m[3]!=gLockMac[3]||m[4]!=gLockMac[4]||m[5]!=gLockMac[5]) return;
    }
    gCount++;
    int8_t* b      = info->buf;
    int     nPairs = info->len / 2;              // I/Q int8 pairs (subcarriers)
    if (nPairs > CSI_MAXSC) nPairs = CSI_MAXSC;  // bound per-subcarrier arrays

    float ampSum = 0.0f, sinSum = 0.0f;
    int   valid  = 0;
    float wsum = 0.0f, wamp = 0.0f;              // NBVI-weighted accumulation
    float bAmp[CSI_BANDS] = {0}; int bCnt[CSI_BANDS] = {0};
    for (int i = 0; i < nPairs; i++) {
        float r   = (float)b[2 * i];
        float im  = (float)b[2 * i + 1];
        float amp = sqrtf(r * r + im * im);
        ampSum += amp;
        if (amp > 1e-4f) { sinSum += im / amp; valid++; }
        // per-subcarrier EMA mean/variance → normalized-variance weight (NBVI)
        float d = amp - gScMean[i];
        gScMean[i] += 0.05f * d;
        gScVar[i]  += 0.05f * (d * d - gScVar[i]);
        float w = gScVar[i] / (gScMean[i] * gScMean[i] + 0.5f);
        if (w > 4.0f) w = 4.0f;                  // cap so one subcarrier can't dominate
        wsum += w; wamp += w * amp;
        int bi = (CSI_BANDS * i) / nPairs; if (bi >= CSI_BANDS) bi = CSI_BANDS - 1;
        bAmp[bi] += amp; bCnt[bi]++;
    }
    float meanUnif = ampSum / (float)nPairs;     // plain average (fallback)
    // NBVI weighting once warm; emphasises the motion-responsive subcarriers.
    float meanAmp  = (gNbviOn && gScWarm && wsum > 1e-3f) ? (wamp / wsum) : meanUnif;
    float meanSin  = valid > 0 ? sinSum / (float)valid : 0.0f;
    if (!gScWarm && gCount > (uint32_t)(CSI_WINDOW * 2)) gScWarm = true;

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

// Passive auto-source scout — pick the strongest nearby AP (no association),
// return its BSSID + channel. Used by `csi auto` so it works against ANY router.
static bool csiAutoScout(uint8_t outMac[6], uint8_t* outCh, char* outSsid, size_t ssidSz) {
    WiFi.mode(WIFI_STA);                          // scan needs STA; do NOT connect
    int n = WiFi.scanNetworks(false /*sync*/, true /*show_hidden*/);
    if (n <= 0) { WiFi.scanDelete(); return false; }
    int best = -1, bestRssi = -127;
    for (int i = 0; i < n; i++) { int r = WiFi.RSSI(i); if (r > bestRssi) { bestRssi = r; best = i; } }
    if (best < 0) { WiFi.scanDelete(); return false; }
    const uint8_t* bm = WiFi.BSSID(best);
    if (!bm) { WiFi.scanDelete(); return false; }
    memcpy(outMac, bm, 6);
    *outCh = (uint8_t)WiFi.channel(best);
    strncpy(outSsid, WiFi.SSID(best).c_str(), ssidSz - 1); outSsid[ssidSz - 1] = '\0';
    WiFi.scanDelete();
    return true;
}

// ── CSI radio setup / teardown ────────────────────────────────────────────────
static void csiStart() {
    csiResetStats();
    gMotion = 0.0f; gCount = 0;
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t pf = {};
    pf.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&pf);
    esp_wifi_set_promiscuous_rx_cb(csiPromiscCb);
    // Auto mode: park on the chosen AP's channel (connected mode stays on the link's).
    if (gAutoMode && gChan >= 1 && gChan <= 14)
        esp_wifi_set_channel(gChan, WIFI_SECOND_CHAN_NONE);
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

// ── SD logging (presence transitions) ─────────────────────────────────────────
// /apps/csidetect/NNN.csv (sequential, never overwritten — wguard/bmon scheme).
// CSI keeps promiscuous live, so every SD touch is wrapped in ScopedPromiscPause
// (pauses promiscuous DMA for the write, resumes after — GDMA rule).
static void csiTimestamp(char* out, size_t n) {
    ClockManager::instance().getTimestamp(out, n);
    if (!out[0]) snprintf(out, n, "@%lums", (unsigned long)millis());
}

static bool csiOpenLog() {
    if (!sdCardManager.isReady()) return false;
    ScopedPromiscPause _;                         // GDMA-safe: pause CSI RX DMA
    sdCardManager.ensureDir(SD_DIR_CSIDETECT);
    uint16_t seq = 1; char probe[40];
    while (seq <= 999) {
        snprintf(probe, sizeof(probe), SD_DIR_CSIDETECT "/%03u.csv", seq);
        if (!SD.exists(probe)) break;
        seq++;
    }
    strncpy(gLogPath, probe, sizeof(gLogPath) - 1); gLogPath[sizeof(gLogPath)-1] = '\0';
    File f = SD.open(gLogPath, FILE_WRITE);
    if (!f) return false;
    f.println("time,event,motion_pct,thresh_pct,zones,mode,channel,bssid,ssid");
    f.close();
    gLogCount = 0;
    return true;
}

// Log one presence transition (CONTACT on entry / CLEAR on exit).
static void csiLogEvent(bool present, int motionPct, int threshPct, int zones) {
    if (!gLogOn || !sdCardManager.isReady()) return;
    char ts[24]; csiTimestamp(ts, sizeof(ts));
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             gLockMac[0], gLockMac[1], gLockMac[2], gLockMac[3], gLockMac[4], gLockMac[5]);
    char ssid[33];
    strncpy(ssid, gLockSsid, sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = '\0';
    for (char* p = ssid; *p; p++) if (*p == ',' || *p == '\r' || *p == '\n') *p = ' ';  // CSV-safe
    if (!ssid[0]) strncpy(ssid, "(hidden)", sizeof(ssid));
    char line[140];
    snprintf(line, sizeof(line), "%s,%s,%d,%d,%d,%s,%u,%s,%s",
             ts, present ? "CONTACT" : "CLEAR", motionPct, threshPct, zones,
             gAutoMode ? "AUTO" : "LINK", gChan, mac, ssid);
    ScopedPromiscPause _;                         // GDMA-safe write
    File f = SD.open(gLogPath, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
    gLogCount++;
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

    // zones (active signal sectors — NOT a person count) + NBVI weighting state
    snprintf(b, sizeof(b), "zones:%d", zones);
    dm.setTextColor(zones > 0 ? 0x6FE8 : 0x7BEF);
    dm.setCursor(PANEL_X, 90); dm.printText(b);
    snprintf(b, sizeof(b), "NBVI:%s", gNbviOn ? "on" : "off");
    dm.setTextColor(gNbviOn ? 0x6FE8 : 0x7BEF);
    dm.setCursor(PANEL_X + 60, 90); dm.printText(b);

    int mp = (int)(disp * 100.0f), tp = (int)(thresh * 100.0f);
    dm.setTextColor(0x7BEF); dm.setCursor(PANEL_X, 110); dm.printText("MOTION");
    snprintf(b, sizeof(b), "%3d%%", mp);
    dm.setTextColor(mp >= tp ? 0x6FE8 : 0xC618);
    dm.setCursor(PANEL_X + 70, 110); dm.printText(b);
    dm.fillRect(PANEL_X, 122, 108, 8, 0x0c22);
    dm.fillRect(PANEL_X, 122, (108 * mp) / 100, 8, 0x6FE8);

    dm.setTextColor(gAdaptive ? TFT_CYAN : 0x7BEF);
    dm.setCursor(PANEL_X, 138); dm.printText(gAdaptive ? "THR auto" : "THRESH");
    snprintf(b, sizeof(b), "%3d%%", tp);
    dm.setTextColor(TFT_YELLOW);
    dm.setCursor(PANEL_X + 70, 138); dm.printText(b);
    dm.fillRect(PANEL_X, 150, 108, 8, 0x2104);
    dm.fillRect(PANEL_X, 150, (108 * tp) / 100, 8, 0x8400);
    int mx = PANEL_X + (108 * tp) / 100;
    tft.drawLine(mx, 148, mx, 159, TFT_YELLOW);

    // source / mode line: AUTO (passive beacon lock) vs LINK, with channel + SSID
    // name (truncated to the panel width; full BSSID/SSID live in the CSV log).
    char ss[13];
    strncpy(ss, gLockSsid[0] ? gLockSsid : "(hidden)", sizeof(ss) - 1);
    ss[sizeof(ss) - 1] = '\0';
    if (gAutoMode) snprintf(b, sizeof(b), "AUTO c%u %s", gChan, ss);
    else           snprintf(b, sizeof(b), "LINK c%u %s", gChan, ss);
    dm.setTextColor(gAutoMode ? 0x6E6C : 0x7BEF);
    dm.setCursor(PANEL_X, 164); dm.printText(b);

    // bring-up diagnostics (+ SD log counter when recording)
    uint32_t fr = gCount;
    if (gLogOn) snprintf(b, sizeof(b), "fr:%lu %ddBm L%lu",
                         (unsigned long)fr, (int)gRssi, (unsigned long)gLogCount);
    else        snprintf(b, sizeof(b), "fr:%lu %ddBm", (unsigned long)fr, (int)gRssi);
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
    dm.setCursor(6, 230); dm.printText("a/l sens c=cal t=auto n=nbvi s=log q");
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
            line(2,  TFT_CYAN,     "csi      = connected link");
            line(3,  TFT_CYAN,     "csi auto = any AP, no join");
            line(4,  0x6FE8,       "STILL  = CLEAR (green)");
            line(5,  TFT_RED,      "MOVING = CONTACT (red)");
            line(6,  TFT_WHITE,    "MOTION% = how much movement");
            line(7,  TFT_CYAN,     "KEYS a/l=sens c=cal q=quit");
            line(8,  TFT_CYAN,     "n=NBVI weight motion-reactive");
            line(9,  TFT_WHITE,    "  subcarriers (cleaner signal)");
            line(10, TFT_CYAN,     "t=auto-threshold   s=SD log");
            line(11, 0x7BEF,       "Motion only (still=CLEAR).");
            line(12, 0x7BEF,       "Blips/sweep decorative, NOT a");
            line(13, TFT_DARKGREY, "bearing.   any key = back");
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
void runCsiDetect(char* args) {
    auto& dm = displayManager;

    // `csi auto` → passive mode: sense off the strongest nearby AP's beacons with
    // NO association. Plain `csi` → use the current connected link (cleaner signal).
    bool autoMode = (args && *args && (strcmp(args, "auto") == 0 || strcmp(args, "a") == 0));

    // Any other argument is a typo → show the command's help rather than run.
    if (args && *args && !autoMode) { Utils::printUsage("csi"); return; }

    if (!autoMode && WiFi.status() != WL_CONNECTED) {
        dm.clearScreen();
        dm.setTextColor(TFT_RED);   dm.println("CSI needs WiFi.");
        dm.setTextColor(TFT_WHITE); dm.println("Connect (cw <ssid>) then csi,");
        dm.println("or run 'csi auto' (no router needed).");
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

    // ── source selection ──────────────────────────────────────────────────────
    gAutoMode = false; gLockActive = false; gChan = 0;
    memset(gLockMac, 0, sizeof(gLockMac)); gLockSsid[0] = '\0';
    if (autoMode) {
        dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
        dm.setCursor(6, outputY);
        dm.setTextColor(TFT_CYAN); dm.println("CSI auto: scanning for APs...");
        if (!csiAutoScout(gLockMac, &gChan, gLockSsid, sizeof(gLockSsid))) {
            dm.setTextColor(TFT_RED); dm.println("No APs found. Try 'csi' (connected).");
            gRadar.deleteSprite();
            dm.printCommandScreen();
            return;
        }
        gAutoMode = true; gLockActive = true;     // lock to that single AP
    } else {
        // connected link: record the joined AP for the log (no MAC filter needed)
        const uint8_t* bm = WiFi.BSSID();        // connected-AP BSSID (no-arg overload)
        if (bm) memcpy(gLockMac, bm, 6);
        strncpy(gLockSsid, WiFi.SSID().c_str(), sizeof(gLockSsid) - 1);
        gLockSsid[sizeof(gLockSsid) - 1] = '\0';
        gChan = (uint8_t)WiFi.channel();
    }

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
    gLogOn = false; gPrevPresent = false;        // SD logging starts off ([s])
    gAdaptive = false; gMargin = 0.12f;          // manual threshold by default ([t])

    while (true) {
        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') break;
        // sensitivity: 'l'/up = more sensitive, 'a'/down = less. In adaptive mode
        // a/l nudge the noise-floor MARGIN instead of an absolute threshold.
        if (k == 'l' || k == 'L' || tb == TBALL_UP) {
            if (gAdaptive) { gMargin -= 0.03f; if (gMargin < 0.02f) gMargin = 0.02f; }
            else           { thresh  -= 0.05f; if (thresh  < 0.05f) thresh  = 0.05f; }
        } else if (k == 'a' || k == 'A' || tb == TBALL_DOWN) {
            if (gAdaptive) { gMargin += 0.03f; if (gMargin > 0.60f) gMargin = 0.60f; }
            else           { thresh  += 0.05f; if (thresh  > 0.95f) thresh  = 0.95f; }
        }
        else if (k == 'c' || k == 'C')                      csiResetStats();
        else if (k == 'n' || k == 'N')                      gNbviOn   = !gNbviOn;    // A/B the weighting
        else if (k == 't' || k == 'T')                      gAdaptive = !gAdaptive;  // auto-threshold on/off
        else if (k == 's' || k == 'S') {                                             // SD logging on/off
            if (gLogOn) gLogOn = false;
            else if (csiOpenLog()) { gLogOn = true; gPrevPresent = false; }
        }
        else if (k == 'h' || k == 'H') {
            csiHelp();                                       // modal; redraw statics after
            dm.clearScreen(); dm.updateStatusBar(); dm.setDefaultTextSize();
            csiHeader(); csiDrawFooter();
        }

        uint32_t now = millis();
        if (now - lastTick < CSI_FPS_MS) { delay(3); continue; }
        lastTick = now;

        // snappy presence: trip instantly, coast a short while.
        // Hampel-filter the motion stream first → a lone glitch can't false-trip.
        float raw = csiHampel(gMotion);
        // Adaptive threshold: sit just above the learned quiet-room noise floor.
        if (gAdaptive) { thresh = clamp01(gNoiseEMA + gMargin); if (thresh < 0.06f) thresh = 0.06f; }
        if (raw > thresh) { holdUntil = now + CSI_HOLD_MS; held = raw; }
        bool present = (now < holdUntil);
        // Learn the noise floor only while quiet, so real motion can't inflate it.
        if (!present) gNoiseEMA += 0.02f * (raw - gNoiseEMA);
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

        // SD log: write a row only on a CLEAR<->CONTACT transition (compact log).
        if (present != gPrevPresent) {
            csiLogEvent(present, (int)(disp * 100.0f), (int)(thresh * 100.0f), zones);
            gPrevPresent = present;
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
    gLockActive = false; gAutoMode = false;   // don't carry the lock into a later run
    gLogOn = false; gAdaptive = false;        // session toggles reset for next run
    gRadar.deleteSprite();
    dm.clearScreen();                 // wipe the sweep display before returning to CLI
    dm.printCommandScreen();
}
