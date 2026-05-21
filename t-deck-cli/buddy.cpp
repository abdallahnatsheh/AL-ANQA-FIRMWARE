// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Claude Desktop BLE remote (claude-desktop-buddy port).
// Connects to Claude Desktop via BLE Nordic UART Service (NUS).
// Desktop pushes session status + permission prompts; T-DECK approves/denies.
// ASCII pet animations ported from https://github.com/anthropics/claude-desktop-buddy

#include "buddy.h"
#include "buddy_common.h"
#include "M5StickCPlus.h"      // typedef LGFX_Sprite TFT_eSprite for species files
#include "display_manager.h"
#include "input_handling.h"
#include "notification_manager.h"

#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include "esp_random.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern LGFX           tft;          // global display from main.ino

// ── NUS UUIDs ─────────────────────────────────────────────────────────────────
#define NUS_SVC_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ── Pet sprite (exposed as TFT_eSprite spr for species files via typedef) ─────
LGFX_Sprite spr;           // species files do: extern TFT_eSprite spr

#define PET_X   162        // screen X where sprite is pushed
#define PET_Y    38        // screen Y (= outputY, just below header)
#define PET_W   155        // sprite width
#define PET_H   200        // sprite height

// ── Rendering geometry (extern'd by species files via buddy_common.h) ─────────
const int BUDDY_X_CENTER  = 77;    // center of PET_W (155/2)
const int BUDDY_CANVAS_W  = 155;
const int BUDDY_Y_BASE    = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W    = 6;
const int BUDDY_CHAR_H    = 8;

// ── Colors ────────────────────────────────────────────────────────────────────
const uint16_t BUDDY_BG     = 0x0000;
const uint16_t BUDDY_HEART  = 0xF810;
const uint16_t BUDDY_DIM    = 0x8410;
const uint16_t BUDDY_YEL    = 0xFFE0;
const uint16_t BUDDY_WHITE  = 0xFFFF;
const uint16_t BUDDY_CYAN   = 0x07FF;
const uint16_t BUDDY_GREEN  = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED    = 0xF800;
const uint16_t BUDDY_BLUE   = 0x041F;
// UI colors matching the original claude-desktop-buddy
static const uint16_t HOT   = 0xFA20;   // red-orange: deny, warnings, impatience
static const uint16_t DIM   = 0x8410;   // grey: dimmed text (= BUDDY_DIM)
static const uint16_t GREEN = 0x07E0;   // green: approve, ok (= BUDDY_GREEN)

// ── Persistent stats (NVS "buddy" namespace, mirrors stats.h) ────────────────
struct BuddyStats {
    uint32_t napSeconds;     // total time spent inside bd sessions
    uint16_t approvals;
    uint16_t denials;
    uint16_t velocity[8];    // seconds-to-respond ring buffer
    uint8_t  velIdx;
    uint8_t  velCount;
    uint32_t tokens;         // cumulative output tokens, 50K = 1 level
};
static BuddyStats  _stats;
static Preferences _prefs;
static bool        _dirty = false;
static const uint32_t TOKENS_PER_LEVEL = 50000;

static void statsLoad() {
    _prefs.begin("buddy", true);
    _stats.napSeconds = _prefs.getUInt("nap",  0);
    _stats.approvals  = _prefs.getUShort("appr", 0);
    _stats.denials    = _prefs.getUShort("deny", 0);
    _stats.velIdx     = _prefs.getUChar("vidx", 0);
    _stats.velCount   = _prefs.getUChar("vcnt", 0);
    _stats.tokens     = _prefs.getUInt("tok",  0);
    size_t got = _prefs.getBytes("vel", _stats.velocity, sizeof(_stats.velocity));
    if (got != sizeof(_stats.velocity)) memset(_stats.velocity, 0, sizeof(_stats.velocity));
    _prefs.end();
}
static void statsSave() {
    if (!_dirty) return;
    _prefs.begin("buddy", false);
    _prefs.putUInt("nap",  _stats.napSeconds);
    _prefs.putUShort("appr", _stats.approvals);
    _prefs.putUShort("deny", _stats.denials);
    _prefs.putUChar("vidx", _stats.velIdx);
    _prefs.putUChar("vcnt", _stats.velCount);
    _prefs.putUInt("tok",  _stats.tokens);
    _prefs.putBytes("vel", _stats.velocity, sizeof(_stats.velocity));
    _prefs.end();
    _dirty = false;
}

// Token delta — bridge sends cumulative since its own start
static uint32_t _lastBridgeTok = 0;
static bool     _tokSynced     = false;
static bool     _levelUpPend   = false;
static void statsOnBridgeTokens(uint32_t bridgeTotal) {
    if (!_tokSynced) { _lastBridgeTok = bridgeTotal; _tokSynced = true; return; }
    if (bridgeTotal < _lastBridgeTok) { _lastBridgeTok = bridgeTotal; return; }
    uint32_t delta = bridgeTotal - _lastBridgeTok;
    _lastBridgeTok = bridgeTotal;
    if (delta == 0) return;
    uint8_t lvlBefore = (uint8_t)(_stats.tokens / TOKENS_PER_LEVEL);
    _stats.tokens += delta;
    if ((uint8_t)(_stats.tokens / TOKENS_PER_LEVEL) > lvlBefore) { _levelUpPend = true; _dirty = true; statsSave(); }
}
static void statsOnApproval(uint32_t secs) {
    _stats.approvals++;
    _stats.velocity[_stats.velIdx] = (uint16_t)min(secs, 65535u);
    _stats.velIdx = (_stats.velIdx + 1) % 8;
    if (_stats.velCount < 8) _stats.velCount++;
    _dirty = true; statsSave();
}
static void statsOnDenial() { _stats.denials++; _dirty = true; statsSave(); }
static bool statsPollLevelUp() { bool r = _levelUpPend; _levelUpPend = false; return r; }

static uint16_t statsMedianVelocity() {
    if (_stats.velCount == 0) return 0;
    uint16_t tmp[8]; memcpy(tmp, _stats.velocity, sizeof(tmp));
    uint8_t n = _stats.velCount;
    for (uint8_t i = 1; i < n; i++) {
        uint16_t k = tmp[i]; int8_t j = i - 1;
        while (j >= 0 && tmp[j] > k) { tmp[j+1] = tmp[j]; j--; }
        tmp[j+1] = k;
    }
    return tmp[n / 2];
}
static uint8_t statsMoodTier() {
    uint16_t vel = statsMedianVelocity();
    int8_t t;
    if      (vel == 0)   t = 2;
    else if (vel < 15)   t = 4;
    else if (vel < 30)   t = 3;
    else if (vel < 60)   t = 2;
    else if (vel < 120)  t = 1;
    else                 t = 0;
    uint16_t a = _stats.approvals, d = _stats.denials;
    if (a + d >= 3) {
        if (d > a)           t -= 2;
        else if (d * 2 > a)  t -= 1;
    }
    if (t < 0) t = 0;
    return (uint8_t)t;
}

// Energy: tops to 5 on each bd session start, drains 1 tier per 2 hours
static uint32_t _lastNapEndMs = 0;
static uint8_t  _energyAtNap  = 3;
static void statsOnWake() { _lastNapEndMs = millis(); _energyAtNap = 5; }
static uint8_t statsEnergyTier() {
    uint32_t hoursSince = (millis() - _lastNapEndMs) / 3600000UL;
    int8_t e = (int8_t)_energyAtNap - (int8_t)(hoursSince / 2);
    if (e < 0) e = 0; if (e > 5) e = 5;
    return (uint8_t)e;
}
static uint8_t statsFedProgress() {
    return (uint8_t)((_stats.tokens % TOKENS_PER_LEVEL) / (TOKENS_PER_LEVEL / 10));
}


// ── Rendering backend (mirrors buddy.cpp from claude-desktop-buddy) ───────────
static LGFX_Sprite* _tgt   = &spr;
static uint8_t      _scale = 2;

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff) {
    int len = strlen(line);
    if (_scale > 1) {
        while (len > 0 && line[len-1] == ' ') len--;
        while (len > 0 && *line == ' ')       { line++; len--; }
    }
    int w = len * BUDDY_CHAR_W * _scale;
    int x = BUDDY_X_CENTER - w / 2 + xOff * _scale;
    _tgt->setTextColor(color, BUDDY_BG);
    _tgt->setCursor(x, yPx);
    for (int i = 0; i < len; i++) _tgt->print(line[i]);
}

void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset, uint16_t color, int xOff) {
    _tgt->setTextSize(_scale);
    int yBase = BUDDY_Y_BASE * _scale - (_scale - 1) * 14;
    for (uint8_t i = 0; i < nLines; i++) {
        buddyPrintLine(lines[i],
                       yBase + (yOffset + i * BUDDY_CHAR_H) * _scale,
                       color, xOff);
    }
}

void buddySetCursor(int x, int y) {
    _tgt->setCursor(BUDDY_X_CENTER + (x - BUDDY_X_CENTER) * _scale, y * _scale);
}
void buddySetColor(uint16_t fg) { _tgt->setTextColor(fg, BUDDY_BG); }
void buddyPrint(const char* s)  { _tgt->setTextSize(_scale); _tgt->print(s); }

// ── Species registry ──────────────────────────────────────────────────────────
extern const Species AXOLOTL_SPECIES;
extern const Species BLOB_SPECIES;
extern const Species CACTUS_SPECIES;
extern const Species CAPYBARA_SPECIES;
extern const Species CAT_SPECIES;
extern const Species CHONK_SPECIES;
extern const Species DRAGON_SPECIES;
extern const Species DUCK_SPECIES;
extern const Species GHOST_SPECIES;
extern const Species GOOSE_SPECIES;
extern const Species MUSHROOM_SPECIES;
extern const Species OCTOPUS_SPECIES;
extern const Species OWL_SPECIES;
extern const Species PENGUIN_SPECIES;
extern const Species RABBIT_SPECIES;
extern const Species ROBOT_SPECIES;
extern const Species SNAIL_SPECIES;
extern const Species TURTLE_SPECIES;
extern const Species TREX_SPECIES;

static const Species* SPECIES_TABLE[] = {
    &TREX_SPECIES,     &CAPYBARA_SPECIES, &DUCK_SPECIES,   &GOOSE_SPECIES,
    &BLOB_SPECIES,     &CAT_SPECIES,      &DRAGON_SPECIES, &OCTOPUS_SPECIES,
    &OWL_SPECIES,      &PENGUIN_SPECIES,  &TURTLE_SPECIES, &SNAIL_SPECIES,
    &GHOST_SPECIES,    &AXOLOTL_SPECIES,  &CACTUS_SPECIES, &ROBOT_SPECIES,
    &RABBIT_SPECIES,   &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
static const uint8_t N_SPECIES = sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]);
static uint8_t s_speciesIdx = 0;

// ── Animation tick ────────────────────────────────────────────────────────────
static uint32_t s_tickCount  = 0;
static uint32_t s_nextTickAt = 0;
static bool     s_petDirty   = true;   // force draw on first call
static const uint32_t TICK_MS = 200;   // 5 fps

static void petTick(uint8_t personaState) {
    uint32_t now = millis();
    if ((int32_t)(now - s_nextTickAt) >= 0) {
        s_nextTickAt = now + TICK_MS;
        s_tickCount++;
        s_petDirty = true;
    }
    if (!s_petDirty) return;
    s_petDirty = false;

    if (personaState >= 7) personaState = B_IDLE;
    spr.fillSprite(BUDDY_BG);
    spr.setTextSize(_scale);
    const Species* sp = SPECIES_TABLE[s_speciesIdx];
    if (sp->states[personaState]) sp->states[personaState](s_tickCount);
    spr.pushSprite(&tft, PET_X, PET_Y);
}

// ── RX ring buffer ────────────────────────────────────────────────────────────
static const size_t RX_CAP = 2048;
static uint8_t         s_rxBuf[RX_CAP];
static volatile size_t s_rxHead = 0;
static volatile size_t s_rxTail = 0;

static void rxPush(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t next = (s_rxHead + 1) % RX_CAP;
        if (next == s_rxTail) return;
        s_rxBuf[s_rxHead] = p[i];
        s_rxHead = next;
    }
}
static bool rxAvail() { return s_rxHead != s_rxTail; }
static int  rxRead()  {
    if (s_rxHead == s_rxTail) return -1;
    int b = s_rxBuf[s_rxTail];
    s_rxTail = (s_rxTail + 1) % RX_CAP;
    return b;
}

// ── BLE state ─────────────────────────────────────────────────────────────────
static NimBLEServer*         s_server    = nullptr;
static NimBLECharacteristic* s_txChar    = nullptr;
static volatile bool         s_connected = false;
static volatile bool         s_secure    = false;
static uint32_t              s_passkey   = 0;

// ── Session state ─────────────────────────────────────────────────────────────
struct TamaState {
    uint8_t  sessionsTotal;
    uint8_t  sessionsRunning;
    uint8_t  sessionsWaiting;
    bool     recentlyCompleted;
    uint32_t tokensToday;
    uint32_t lastUpdated;
    char     msg[24];
    bool     connected;
    char     lines[8][92];
    uint8_t  nLines;
    uint16_t lineGen;
    char     promptId[40];
    char     promptTool[20];
    char     promptHint[256];
};

// ── BLE write ─────────────────────────────────────────────────────────────────
static size_t buddyWrite(const uint8_t* data, size_t len) {
    if (!s_connected || !s_txChar) return 0;
    const size_t chunk = 180;
    size_t sent = 0;
    while (sent < len) {
        size_t n = len - sent;
        if (n > chunk) n = chunk;
        s_txChar->setValue(data + sent, n);
        s_txChar->notify();
        sent += n;
        delay(4);
    }
    return sent;
}

static void sendPermission(const char* id, const char* decision) {
    char buf[108];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n",
             id, decision);
    buddyWrite((const uint8_t*)buf, strlen(buf));
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────
class BuddyRxCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string v = c->getValue();
        if (!v.empty()) rxPush((const uint8_t*)v.data(), v.size());
    }
};

class BuddySrvCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override    { s_connected = true; }
    void onDisconnect(NimBLEServer*) override {
        s_connected = false;
        s_secure    = false;
        NimBLEDevice::startAdvertising();
    }
};

// ── JSON parsing ──────────────────────────────────────────────────────────────
static uint32_t s_lastLiveMs   = 0;
static char     s_lineBuf[1024];
static uint16_t s_lineLen = 0;

static void applyJson(const char* line, TamaState* out) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;

    if (!doc["time"].isNull()) { s_lastLiveMs = millis(); return; }  // skip RTC sync

    out->sessionsTotal     = doc["total"]        | out->sessionsTotal;
    out->sessionsRunning   = doc["running"]      | out->sessionsRunning;
    out->sessionsWaiting   = doc["waiting"]      | out->sessionsWaiting;
    out->recentlyCompleted = doc["completed"]    | false;
    out->tokensToday       = doc["tokens_today"] | out->tokensToday;
    if (!doc["tokens"].isNull()) statsOnBridgeTokens(doc["tokens"].as<uint32_t>());

    const char* m = doc["msg"];
    if (m) { strncpy(out->msg, m, sizeof(out->msg)-1); out->msg[sizeof(out->msg)-1] = 0; }

    JsonArray la = doc["entries"];
    if (!la.isNull()) {
        uint8_t n = 0;
        for (JsonVariant v : la) {
            if (n >= 8) break;
            const char* s = v.as<const char*>();
            strncpy(out->lines[n], s ? s : "", 91);
            out->lines[n][91] = 0;
            n++;
        }
        if (n != out->nLines) out->lineGen++;
        out->nLines = n;
    }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
        const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
        strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId)-1);   out->promptId[sizeof(out->promptId)-1]   = 0;
        strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool)-1); out->promptTool[sizeof(out->promptTool)-1] = 0;
        strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint)-1); out->promptHint[sizeof(out->promptHint)-1] = 0;
    } else {
        out->promptId[0] = 0; out->promptTool[0] = 0; out->promptHint[0] = 0;
    }
    out->lastUpdated = millis();
    s_lastLiveMs = millis();
}

static void dataPoll(TamaState* out) {
    while (rxAvail()) {
        int c = rxRead();
        if (c < 0) break;
        if (c == '\n' || c == '\r') {
            if (s_lineLen > 0) {
                s_lineBuf[s_lineLen] = 0;
                if (s_lineBuf[0] == '{') applyJson(s_lineBuf, out);
                s_lineLen = 0;
            }
        } else if (s_lineLen < sizeof(s_lineBuf) - 1) {
            s_lineBuf[s_lineLen++] = (char)c;
        }
    }
    bool fresh = s_lastLiveMs && (millis() - s_lastLiveMs) <= 30000;
    out->connected = fresh && s_connected;
    if (!out->connected) {
        const char* idle = s_connected ? "Connected..." : "Waiting for Claude...";
        strncpy(out->msg, idle, sizeof(out->msg)-1);
        out->msg[sizeof(out->msg)-1] = 0;
    }
}

// ── Persona state ─────────────────────────────────────────────────────────────
static uint8_t personaState(const TamaState& t, bool bleConn, uint32_t approvedMs) {
    if (!bleConn)                                    return B_SLEEP;
    if (approvedMs && millis() - approvedMs < 5000)  return B_HEART;
    if (t.recentlyCompleted)                         return B_CELEBRATE;
    if (t.sessionsWaiting > 0)                       return B_ATTENTION;
    if (t.sessionsRunning >= 3)                      return B_BUSY;
    return B_IDLE;
}

// Prompt arrival time — file-scope so drawStatus can show the "approve? Ns" timer
static uint32_t s_promptArrivedMs = 0;
static bool     s_popupOpen   = false;
static uint32_t s_popupDrawMs = 0;

// Tiny heart matching original main.cpp tinyHeart(), drawn to tft (left panel direct)
static void tinyHeart(int x, int y, bool filled, uint16_t col) {
    if (filled) {
        tft.fillCircle(x - 2, y, 2, col);
        tft.fillCircle(x + 2, y, 2, col);
        tft.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, col);
    } else {
        tft.drawCircle(x - 2, y, 2, col);
        tft.drawCircle(x + 2, y, 2, col);
        tft.drawLine(x - 4, y + 1, x, y + 5, col);
        tft.drawLine(x + 4, y + 1, x, y + 5, col);
    }
}

// ── Status panel (left 160px) ─────────────────────────────────────────────────
static void drawStatus(const TamaState& t, bool bleConn, const char* name) {
    uint16_t bodyCol = SPECIES_TABLE[s_speciesIdx]->bodyColor;
    const int W = PET_X - 2;   // left panel width = 160px

    // Clear left panel (pet sprite owns x=162+)
    tft.fillRect(0, outputY, W, SCREEN_HEIGHT - outputY, 0x0000);
    tft.setTextSize(1);

    // ── Header — species name left, BLE status right ───────────────────────────
    int y = outputY + 2;
    tft.setTextColor(DIM, 0x0000);
    tft.setCursor(6, y);
    tft.print(SPECIES_TABLE[s_speciesIdx]->name);

    const char* bleLabel;
    uint16_t    bleCol;
    if (!bleConn)                  { bleLabel = "discover"; bleCol = HOT; }
    else if (t.sessionsWaiting>0)  { bleLabel = "APPROVAL"; bleCol = HOT; }
    else                           { bleLabel = "ok";       bleCol = GREEN; }
    tft.setTextColor(bleCol, 0x0000);
    tft.setCursor(W - (int)(strlen(bleLabel) * 6) - 4, y);
    tft.print(bleLabel);
    y += 12;

    tft.drawFastHLine(0, y, W, DIM);
    y += 4;

    // ── Not connected: pairing guide ───────────────────────────────────────────
    if (!bleConn) {
        tft.setTextColor(bleCol, 0x0000);
        tft.setTextSize(2);
        tft.setCursor(6, y); tft.print("discover");
        tft.setTextSize(1);
        y += 20;
        tft.setTextColor(bodyCol, 0x0000);
        tft.setCursor(6, y); tft.print(name);
        y += 14;
        tft.setTextColor(DIM, 0x0000);
        tft.setCursor(6, y); tft.print("Open Claude Desktop"); y += 10;
        tft.setCursor(6, y); tft.print("> Developer");          y += 10;
        tft.setCursor(6, y); tft.print("> Hardware Buddy");     y += 10;
        tft.setCursor(6, y); tft.print("auto-connects via BLE");
        tft.setCursor(6, SCREEN_HEIGHT - 10);
        tft.print("[q] quit  [spc] pet");
        return;
    }

    // ── Pet stats — mirrors original drawPetStats() ────────────────────────────
    // y starts at outputY+16=54; original starts at TOP+16=86; same relative offset

    // mood (4 tiny hearts)
    tft.setTextColor(DIM, 0x0000);
    tft.setCursor(6, y);
    tft.print("mood");
    uint8_t mood = statsMoodTier();
    uint16_t moodCol = (mood >= 3) ? GREEN : (mood >= 2) ? BUDDY_YEL : HOT;
    for (int i = 0; i < 4; i++) tinyHeart(54 + i * 16, y + 4, i < mood, moodCol);
    y += 20;

    // fed (10 circles, body color = token-driven)
    tft.setCursor(6, y);
    tft.print("fed");
    uint8_t fed = statsFedProgress();
    for (int i = 0; i < 10; i++) {
        int px = 38 + i * 9;
        if (i < fed) tft.fillCircle(px, y + 3, 2, bodyCol);
        else         tft.drawCircle(px, y + 3, 2, DIM);
    }
    y += 20;

    // energy (5 rects, color by level)
    tft.setCursor(6, y);
    tft.print("energy");
    uint8_t en = statsEnergyTier();
    uint16_t enCol = (en >= 4) ? BUDDY_CYAN : (en >= 2) ? BUDDY_YEL : HOT;
    for (int i = 0; i < 5; i++) {
        int px = 54 + i * 13;
        if (i < en) tft.fillRect(px, y, 9, 6, enCol);
        else        tft.drawRect(px, y, 9, 6, DIM);
    }
    y += 24;

    // Level badge (filled rounded rect in body color) + run/wait counts
    tft.fillRoundRect(6, y, 42, 14, 3, bodyCol);
    tft.setTextColor(0x0000, bodyCol);
    tft.setCursor(11, y + 3);
    tft.printf("Lv %u", (unsigned)(_stats.tokens / TOKENS_PER_LEVEL));
    tft.setTextColor(DIM, 0x0000);
    tft.setCursor(54, y + 3);
    tft.printf("run:%u  wait:%u", t.sessionsRunning, t.sessionsWaiting);
    y += 20;

    // approved, denied, napped — exact labels from original
    tft.setTextColor(DIM, 0x0000);
    tft.setCursor(6, y); tft.printf("approved %u", _stats.approvals); y += 10;
    tft.setCursor(6, y); tft.printf("denied   %u", _stats.denials);   y += 10;
    uint32_t nap = _stats.napSeconds;
    tft.setCursor(6, y); tft.printf("napped   %luh%02lum", nap/3600, (nap/60)%60); y += 10;

    // tokens + today — same format function as original
    auto tokFmt = [&](const char* label, uint32_t v) {
        tft.setCursor(6, y);
        if      (v >= 1000000) tft.printf("%s%lu.%luM", label, v/1000000, (v/100000)%10);
        else if (v >= 1000)    tft.printf("%s%lu.%luK", label, v/1000,    (v/100)%10);
        else                   tft.printf("%s%lu",      label, v);
        y += 10;
    };
    tokFmt("tokens   ", _stats.tokens);
    tokFmt("today    ", t.tokensToday);

    // ── HUD — approval panel or idle strip ────────────────────────────────────
    // y ≈ 176 at this point, leaving ~64px for the HUD (240-176=64)
    y += 4;
    tft.drawFastHLine(0, y, W, DIM);
    int hy = y + 4;

    if (t.promptId[0]) {
        // "approve? Ns" — turns HOT after 10s (mirrors original drawApproval)
        uint32_t waited = s_promptArrivedMs ? (millis() - s_promptArrivedMs) / 1000 : 0;
        tft.setTextColor(waited >= 10 ? HOT : DIM, 0x0000);
        tft.setCursor(4, hy);
        tft.printf("approve? %lus", (unsigned long)waited);
        hy += 12;

        // Tool name: size 2 if ≤12 chars and space allows (mirrors original)
        int toolLen = strlen(t.promptTool);
        tft.setTextColor(BUDDY_WHITE, 0x0000);
        if (toolLen <= 12 && hy + 16 < SCREEN_HEIGHT - 12) {
            tft.setTextSize(2);
            tft.setCursor(4, hy); tft.print(t.promptTool);
            tft.setTextSize(1);
            hy += 18;
        } else {
            tft.setCursor(4, hy); tft.print(t.promptTool);
            hy += 10;
        }

        // Hint — wraps at 20 chars (mirrors original's 21-char wrap)
        tft.setTextColor(DIM, 0x0000);
        int hlen = strlen(t.promptHint);
        if (hlen > 0) {
            tft.setCursor(4, hy);
            tft.printf("%.20s", t.promptHint);
            if (hlen > 20 && hy + 8 < SCREEN_HEIGHT - 12) {
                hy += 8;
                tft.setCursor(4, hy);
                tft.printf("%.20s", t.promptHint + 20);
            }
        }

        // Approve/deny buttons at very bottom (mirrors original)
        tft.setTextColor(GREEN, 0x0000);
        tft.setCursor(4, SCREEN_HEIGHT - 10);
        tft.print("[y] approve");
        tft.setTextColor(HOT, 0x0000);
        tft.setCursor(W - 8 * 6 - 4, SCREEN_HEIGHT - 10);
        tft.print("[n] deny");
    } else {
        // Idle: status message + key hint
        tft.setTextColor(BUDDY_WHITE, 0x0000);
        tft.setCursor(4, hy);
        char msg[20] = {0}; strncpy(msg, t.msg[0] ? t.msg : "Idle", 18);
        tft.print(msg);
        tft.setTextColor(DIM, 0x0000);
        tft.setCursor(4, SCREEN_HEIGHT - 10);
        tft.print("[q] quit  [spc] pet");
    }
}

// ── Terminal popup (full-screen overlay for long permission prompts) ──────────
static void drawPopup(const TamaState& t) {
    const int PX  = 4,  PY  = outputY + 2;
    const int PW  = 312, PH = SCREEN_HEIGHT - outputY - 4;
    const int IX  = PX + 8;          // text left margin
    const int CPL = (PW - 16) / 6;   // chars per line at textSize=1

    tft.fillRect(PX, PY, PW, PH, 0x0000);
    tft.drawRect(PX,   PY,   PW,   PH,   BUDDY_CYAN);
    tft.drawRect(PX+1, PY+1, PW-2, PH-2, BUDDY_CYAN);

    int y = PY + 6;
    tft.setTextSize(1);
    tft.setTextColor(BUDDY_CYAN, 0x0000);
    tft.setCursor(IX, y); tft.print("PERMISSION REQUEST");
    y += 12;
    tft.drawFastHLine(PX + 2, y, PW - 4, BUDDY_CYAN);
    y += 6;

    // Tool name — size 2 if short enough
    tft.setTextColor(BUDDY_WHITE, 0x0000);
    if ((int)strlen(t.promptTool) <= 13 && y + 16 < SCREEN_HEIGHT - 24) {
        tft.setTextSize(2);
        tft.setCursor(IX, y); tft.print(t.promptTool);
        tft.setTextSize(1);
        y += 18;
    } else {
        tft.setCursor(IX, y); tft.print(t.promptTool);
        y += 10;
    }
    y += 2;

    // Hint — char-wrapped
    tft.setTextColor(BUDDY_YEL, 0x0000);
    const char* h   = t.promptHint;
    int         hlen = (int)strlen(h);
    int         pos  = 0;
    while (pos < hlen && y <= SCREEN_HEIGHT - 24) {
        int end = pos + CPL;
        if (end > hlen) end = hlen;
        tft.setCursor(IX, y);
        for (int i = pos; i < end; i++) tft.print(h[i]);
        y  += 10;
        pos = end;
    }
    if (pos < hlen) {
        tft.setTextColor(HOT, 0x0000);
        tft.setCursor(IX + (CPL - 3) * 6, y - 10);
        tft.print("...");
    }

    // Timer line
    uint32_t waited = s_promptArrivedMs ? (millis() - s_promptArrivedMs) / 1000 : 0;
    tft.setTextColor(waited >= 10 ? HOT : DIM, 0x0000);
    tft.setCursor(IX, SCREEN_HEIGHT - 22);
    tft.printf("waiting %lus", (unsigned long)waited);

    // Approve / deny buttons
    tft.setTextColor(GREEN, 0x0000);
    tft.setCursor(IX, SCREEN_HEIGHT - 10);
    tft.print("[y] approve");
    tft.setTextColor(HOT, 0x0000);
    tft.setCursor(PX + PW - 9 * 6 - 4, SCREEN_HEIGHT - 10);
    tft.print("[n] deny");
}

// ── Command entry point ───────────────────────────────────────────────────────
void buddyCommand(char* args) {
    // Build BLE device name
    char btName[20];
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    if (args && *args)
        snprintf(btName, sizeof(btName), "%.16s", args);
    else
        snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);

    // Load persistent stats + reset energy on each session start
    statsLoad();
    statsOnWake();
    _tokSynced = false;   // re-latch token delta against this bridge session

    // Pick a random starting species for fun
    s_speciesIdx = esp_random() % N_SPECIES;
    s_tickCount  = 0;
    s_nextTickAt = 0;
    s_petDirty   = true;

    // Reset BLE + JSON state
    s_rxHead = 0; s_rxTail = 0;
    s_lineLen = 0;
    s_connected = false; s_secure = false;
    s_lastLiveMs = 0;
    s_popupOpen   = false;
    s_popupDrawMs = 0;
    TamaState tama = {};
    uint32_t sessionStartMs = millis();

    // Init pet sprite
    spr.setColorDepth(16);
    spr.createSprite(PET_W, PET_H);

    // NimBLE init
    NimBLEDevice::init("");
    vTaskDelay(pdMS_TO_TICKS(100));   // let controller fully start before cold-boot deinit
    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(100));

    NimBLEDevice::init(btName);
    NimBLEDevice::setMTU(517);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new BuddySrvCb());

    NimBLEService* pSvc = s_server->createService(NUS_SVC_UUID);
    s_txChar = pSvc->createCharacteristic(NUS_TX_UUID,
                   NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    NimBLECharacteristic* rxChar = pSvc->createCharacteristic(NUS_RX_UUID,
                   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rxChar->setCallbacks(new BuddyRxCb());
    pSvc->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(NUS_SVC_UUID);
    pAdv->setScanResponse(true);
    NimBLEDevice::startAdvertising();

    displayManager.setBtActive(true);
    displayManager.updateStatusBar();

    // Draw initial state
    drawStatus(tama, false, btName);
    petTick(B_SLEEP);

    // Change-tracking
    bool     lastConn = false;
    char     lastPid[40] = "";
    uint8_t  lastWait    = 255;
    uint16_t lastGen     = 0xFFFF;
    uint32_t approvedMs  = 0;
    s_promptArrivedMs = 0;

    while (true) {
        dataPoll(&tama);

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if ((k == 'y' || k == 'Y') && tama.promptId[0] && s_connected) {
            uint32_t secs = s_promptArrivedMs ? (millis() - s_promptArrivedMs) / 1000 : 0;
            statsOnApproval(secs);
            sendPermission(tama.promptId, "once");
            tama.promptId[0] = 0;
            approvedMs = millis();
            s_promptArrivedMs = 0;
            s_popupOpen = false;
            lastPid[0] = '\1';   // force status redraw
        }
        if ((k == 'n' || k == 'N') && tama.promptId[0] && s_connected) {
            statsOnDenial();
            sendPermission(tama.promptId, "deny");
            tama.promptId[0] = 0;
            s_promptArrivedMs = 0;
            s_popupOpen = false;
            lastPid[0] = '\1';
        }
        // Cycle species with spacebar (not while popup is open)
        if (k == ' ' && !s_popupOpen) {
            s_speciesIdx = (s_speciesIdx + 1) % N_SPECIES;
            s_petDirty = true;
        }

        // Track prompt arrival; open popup on new prompt
        if (tama.promptId[0] && strcmp(tama.promptId, lastPid) != 0) {
            if (s_promptArrivedMs == 0) s_promptArrivedMs = millis();
            if (!s_popupOpen) {
                s_popupOpen = true;
                s_popupDrawMs = 0;
                NotificationManager::getInstance().notify(NOTIF_PING);
            }
        }
        // Auto-close popup if desktop cleared the prompt
        if (s_popupOpen && !tama.promptId[0]) { s_popupOpen = false; lastPid[0] = '\1'; }

        // Level-up → celebrate animation
        if (statsPollLevelUp()) approvedMs = millis();

        if (s_popupOpen) {
            // Redraw popup every second to update the "waiting Ns" timer
            if (millis() - s_popupDrawMs >= 1000) {
                drawPopup(tama);
                s_popupDrawMs = millis();
            }
            // Keep lastPid in sync so we don't re-trigger on the same prompt
            strncpy(lastPid, tama.promptId, sizeof(lastPid)-1);
            lastPid[sizeof(lastPid)-1] = 0;
        } else {
            bool changed = (s_connected          != lastConn)
                        || (strcmp(tama.promptId, lastPid) != 0)
                        || (tama.sessionsWaiting  != lastWait)
                        || (tama.lineGen          != lastGen);
            if (changed) {
                drawStatus(tama, s_connected, btName);
                s_petDirty = true;
                lastConn = s_connected;
                strncpy(lastPid, tama.promptId, sizeof(lastPid)-1);
                lastPid[sizeof(lastPid)-1] = 0;
                lastWait = tama.sessionsWaiting;
                lastGen  = tama.lineGen;
            }
            uint8_t ps = personaState(tama, s_connected, approvedMs);
            petTick(ps);
        }

        delay(50);
    }

    // Accumulate session time as "nap seconds" + persist
    _stats.napSeconds += (millis() - sessionStartMs) / 1000;
    _dirty = true;
    statsSave();

    // Cleanup
    spr.deleteSprite();
    s_server    = nullptr;
    s_txChar    = nullptr;
    s_connected = false;
    s_secure    = false;

    NimBLEDevice::deinit(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    NimBLEDevice::init("T-REX");
    SD.begin(39);

    displayManager.setBtActive(false);
    displayManager.updateStatusBar();
    displayManager.printCommandScreen();
}
