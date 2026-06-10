#include "display_manager.h"
#include "battery_manager.h"
#include "gps_manager.h"
#include "clock_manager.h"
#include "utilities.h"
#include <Wire.h>
#include <string>
#include <Arduino.h>
#include <WiFi.h>
#include "input_handling.h"

extern InputHandling  inputHandler;
extern BatteryManager batteryManager;

DisplayManager::DisplayManager(LGFX& tft) : tft(tft) {}

void DisplayManager::init() {
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    tft.init();
    tft.setRotation(1);
    tft.setBrightness(128);
#ifdef BOARD_TDECK_PLUS
    tft.invertDisplay(1);
#else
    tft.invertDisplay(0);
#endif
    matrix_effect.init(&tft);
    tft.fillScreen(TFT_BLACK);
    // tdeck_begin() called by main after splash so CMD screen never shows before it
}

// Force LovyanGFX to drain its DMA queue and release SPI2.
// Call this before any other task starts SD sector I/O — both use SPI2 on T-DECK
// and their locking mechanisms (spi_device_acquire_bus vs spi->lock semaphore)
// don't coordinate, so we must ensure LGFX has fully released the bus first.
void DisplayManager::flushSPI() {
    tft.startWrite();  // Acquire the bus (increments transaction counter)
    tft.endWrite();    // Release: waits for DMA completion, calls spi_device_release_bus()
}

// ── Status bar icon helpers ───────────────────────────────────────────────────

// WiFi: 3 states — off (grey+X), on-no-IP (grey bars), connected (green bars by RSSI)
static void drawWiFiBars(LGFX& tft, int x, int yBot, bool wifiOn, bool connected, int rssi) {
    const int bW = 3, bGap = 1;
    const int bH[4] = { 4, 7, 10, 13 };
    int litBars = 0;
    if (connected) {
        if      (rssi >= -60) litBars = 4;
        else if (rssi >= -70) litBars = 3;
        else if (rssi >= -80) litBars = 2;
        else                   litBars = 1;
    }
    for (int i = 0; i < 4; i++) {
        uint16_t c = (i < litBars) ? TFT_GREEN : 0x2104;
        tft.fillRect(x + i * (bW + bGap), yBot - bH[i], bW, bH[i], c);
    }
    // Red X only when WiFi stack is completely off
    if (!wifiOn) {
        tft.drawLine(x, yBot - 13, x + 14, yBot - 1, TFT_RED);
        tft.drawLine(x + 14, yBot - 13, x, yBot - 1, TFT_RED);
    }
}

// GPS icon: satellite body (4×4 square) with two solar-panel arms and a signal arc
static void drawGPSIcon(LGFX& tft, int cx, int cy, bool active, bool fixed) {
    uint16_t bg   = 0x000F;
    uint16_t body = active ? (fixed ? TFT_GREEN : TFT_YELLOW) : 0x2104;
    uint16_t arc  = active ? (fixed ? TFT_GREEN : TFT_ORANGE) : 0x2104;
    tft.fillRect(cx - 6, cy - 8, 13, 17, bg);  // clear area
    // Satellite body (4×4)
    tft.fillRect(cx - 2, cy - 2, 4, 4, body);
    // Solar panels (left and right, 3px wide 2px tall)
    tft.fillRect(cx - 6, cy - 1, 3, 2, body);
    tft.fillRect(cx + 3, cy - 1, 3, 2, body);
    // Signal arcs below (3 concentric arcs = strong signal when fixed)
    if (active) {
        tft.drawArc(cx, cy + 6, 3, 2, 200, 340, arc);
        if (fixed) {
            tft.drawArc(cx, cy + 6, 6, 5, 200, 340, arc);
        }
    }
}

// WGuard shield icon: 11×14px shield, color = off/green/yellow/red by severity
static void drawWGuardIcon(LGFX& tft, int cx, int cy, bool active, uint8_t maxSev) {
    uint16_t bg = 0x000F;
    uint16_t c  = !active  ? 0x2104      // off  = dark grey
                : maxSev == 0 ? TFT_GREEN   // clean= green
                : maxSev == 1 ? TFT_YELLOW  // warn = yellow
                :               TFT_RED;    // crit = red
    tft.fillRect(cx - 6, cy - 8, 13, 16, bg);          // clear area
    tft.fillRect(cx - 5, cy - 7, 11, 9, c);             // shield top rect
    tft.fillTriangle(cx - 5, cy + 2, cx + 5, cy + 2, cx, cy + 7, c);  // pointed bottom
    // White inner check mark when clean (maxSev==0 and active)
    if (active && maxSev == 0) {
        tft.drawLine(cx - 2, cy - 1, cx, cy + 2, TFT_WHITE);
        tft.drawLine(cx, cy + 2, cx + 3, cy - 3, TFT_WHITE);
    }
}

// BT icon: classic ᛒ rune, 2-px thick spine, 7-px right reach, 16-px tall
static void drawBTIcon(LGFX& tft, int cx, int cy, bool active) {
    uint16_t c  = active ? 0x07FF : 0x2104;   // cyan : dark grey
    uint16_t bg = 0x000F;                       // status bar background
    tft.fillRect(cx - 2, cy - 8, 11, 17, bg);  // clear area first
    tft.fillRect(cx, cy - 8, 2, 17, c);         // thick vertical spine
    // Upper-right arms
    tft.drawLine(cx + 1, cy - 8, cx + 7, cy - 4, c);
    tft.drawLine(cx + 7, cy - 4, cx + 1, cy,     c);
    // Lower-right arms
    tft.drawLine(cx + 1, cy,     cx + 7, cy + 4, c);
    tft.drawLine(cx + 7, cy + 4, cx + 1, cy + 8, c);
}

static void drawBattery(LGFX& tft, int x, int y, int pct, bool charging) {
    if (charging) {
        tft.drawRect(x, y, 18, 10, TFT_CYAN);              // cyan outline = charging
        tft.fillRect(x + 18, y + 2, 3, 6, TFT_CYAN);      // nub
        tft.fillRect(x + 1, y + 1, 16, 8, 0x000F);         // clear interior
        for (int i = 0; i < 5; i++)
            tft.fillRect(x + 2 + i * 3, y + 2, 2, 6, TFT_CYAN);  // all cells lit
        tft.setTextColor(TFT_CYAN);
        tft.setCursor(x + 21, y + 1);
        tft.print("+");
    } else {
        int litCells = pct / 20;                            // 0-5
        uint16_t c   = litCells > 2 ? TFT_GREEN
                     : litCells > 1 ? TFT_YELLOW
                     :                TFT_RED;
        tft.drawRect(x, y, 18, 10, TFT_LIGHTGREY);
        tft.fillRect(x + 18, y + 2, 3, 6, TFT_LIGHTGREY);
        tft.fillRect(x + 1, y + 1, 16, 8, 0x000F);
        for (int i = 0; i < 5; i++)
            tft.fillRect(x + 2 + i * 3, y + 2, 2, 6, i < litCells ? c : 0x2104);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void DisplayManager::updateStatusBar() {
    tft.fillRect(0, promptY, SCREEN_WIDTH, promptHeight, 0x000F);
    tft.drawFastHLine(0, promptY + promptHeight - 1, SCREEN_WIDTH, TFT_CYAN);
    setDefaultTextSize();

    // ── [T-REX] title ────────────────────────────────────────────────────────
    tft.setCursor(5, promptY + 11);
    tft.setTextColor(0x7BEF); tft.print("[");
    tft.setTextColor(TFT_CYAN);
#ifdef BOARD_TDECK_PLUS
    tft.print("T-REX+");
#else
    tft.print("T-REX");
#endif
    tft.setTextColor(0x7BEF); tft.print("]");

    // ── WiFi bars + IP ────────────────────────────────────────────────────────
    bool wifiOn    = (WiFi.getMode() != WIFI_MODE_NULL);
    bool connected = (WiFi.status() == WL_CONNECTED);
    drawWiFiBars(tft, 60, promptY + 22, wifiOn, connected, connected ? WiFi.RSSI() : 0);

    if (connected) {
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(82, promptY + 11);
        tft.print(WiFi.localIP().toString());
    }

    // ── UTC clock — "HH:MM" fixed at x=173, same row as IP ───────────────────
    {
        char clk[6];
        ClockManager::instance().getShortTime(clk, sizeof(clk));
        tft.setCursor(175, promptY + 11);
        tft.setTextColor(ClockManager::instance().isValid() ? TFT_WHITE : 0x2104);
        tft.print(clk);
    }

    // ── WGuard shield icon ────────────────────────────────────────────────────
    drawWGuardIcon(tft, 215, promptY + 15, _wgActive, _wgMaxSev);

    // ── GPS icon (T-Deck Plus only) ───────────────────────────────────────────
#ifdef BOARD_TDECK_PLUS
    {
        GpsManager& gm = GpsManager::instance();
        drawGPSIcon(tft, 236, promptY + 15, gm.isRunning(), gm.isValid());
    }
#endif

    // ── EC (ESPChat bg) badge — bottom-left strip, clear of all other elements ──
    tft.fillRect(82, promptY + 20, 18, 8, 0x000F);  // clear area
    if (_ecActive) {
        tft.setTextColor(TFT_CYAN);
        tft.setCursor(82, promptY + 20);
        tft.print("EC");
    }

    // ── BT icon ───────────────────────────────────────────────────────────────
    drawBTIcon(tft, 255, promptY + 15, _btActive);

    // ── Battery ───────────────────────────────────────────────────────────────
    drawBattery(tft, 275, promptY + 10, batteryManager.getPct(), batteryManager.isCharging());

    tft.setTextColor(TFT_WHITE);
}

void DisplayManager::setBtActive(bool active) {
    _btActive = active;
}

void DisplayManager::setWGuardState(bool active, uint8_t maxSev) {
    _wgActive = active;
    _wgMaxSev = maxSev;
    // Redraw just the shield without a full status bar repaint
    drawWGuardIcon(tft, 215, promptY + 15, _wgActive, _wgMaxSev);
}

void DisplayManager::setEcActive(bool active) {
    _ecActive = active;
    // Partial redraw — just the EC badge area
    tft.fillRect(82, promptY + 20, 18, 8, 0x000F);
    if (_ecActive) {
        tft.setTextColor(TFT_CYAN);
        tft.setCursor(82, promptY + 20);
        tft.print("EC");
    }
}

void DisplayManager::tdeck_begin() {
    tft.fillScreen(TFT_BLACK);
    updateStatusBar();
    printFirstCommandScreen("");
}

void DisplayManager::clearInputText() {
    tdeck_begin();
}

void DisplayManager::scrollIfNeeded() {
    int32_t y = tft.getCursorY();
    if (y > SCREEN_HEIGHT - LINE_HEIGHT || y < outputY) {
        clearScreen();
        tft.setCursor(10, outputY);
    }
}

void DisplayManager::clearScreen() {
    if (_blocked) return;
    tft.fillRect(0, promptY + promptHeight, SCREEN_WIDTH, SCREEN_HEIGHT - (promptY + promptHeight), TFT_BLACK);
}

void DisplayManager::redrawCommandLine(const char* cmd, int cursorPos) {
    if (_blocked) return;
    tft.fillRect(0, _cmdLineY, SCREEN_WIDTH, LINE_HEIGHT + 2, TFT_BLACK);
    tft.setCursor(10, _cmdLineY);
    setDefaultTextSize();
    tft.setTextColor(0x7BEF); tft.print("[");
    tft.setTextColor(TFT_CYAN); tft.print(">");
    tft.setTextColor(0x7BEF); tft.print("] ");

    int len = (int)strlen(cmd);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < cursorPos && i < len; i++) tft.print(cmd[i]);

    if (cursorPos < len) {
        tft.setTextColor(TFT_BLACK, TFT_CYAN);
        tft.print(cmd[cursorPos]);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        for (int i = cursorPos + 1; i < len; i++) tft.print(cmd[i]);
    } else {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.print('_');
    }
    tft.setTextColor(TFT_WHITE);
}

void DisplayManager::printFirstCommandScreen(const char* command) {
    _cmdLineY = outputY;
    tft.setCursor(10, outputY);
    setDefaultTextSize();
    tft.setTextColor(0x7BEF); tft.print("[");
    tft.setTextColor(TFT_CYAN); tft.print(">");
    tft.setTextColor(0x7BEF); tft.print("] ");
    tft.setTextColor(TFT_WHITE);
    tft.print(command);
}

void DisplayManager::setDefaultTextSize() {
    tft.setTextSize(1.0, 1.0);
}

void DisplayManager::printText(const char* text) {
    if (_blocked) return;
    tft.print(text);
}

void DisplayManager::println(const char* text) {
    if (_blocked) return;
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println(String text) {
    if (_blocked) return;
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println(int text) {
    if (_blocked) return;
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println() {
    if (_blocked) return;
    scrollIfNeeded();
    tft.println();
}

void DisplayManager::printText(const char* text, uint16_t x, uint16_t y) {
    if (_blocked) return;
    tft.setCursor(x, y);
    tft.print(text);
}

void DisplayManager::printText(const char* text, uint16_t x, uint16_t y, uint16_t color) {
    if (_blocked) return;
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(text);
}

void DisplayManager::printText(char incoming) {
    if (_blocked) return;
    tft.print(incoming);
}

void DisplayManager::printText(int incoming) {
    if (_blocked) return;
    tft.print(incoming);
}

void DisplayManager::printText(const std::string& text) {
    if (_blocked) return;
    tft.print(text.c_str());
}

void DisplayManager::printText(const String& text) {
    if (_blocked) return;
    tft.print(text);
}

void DisplayManager::backspaceChar() {
    if (_blocked) return;
    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    int16_t charW = 6;
    if (x - charW < 10) return;
    tft.fillRect(x - charW, y, charW, LINE_HEIGHT + 2, TFT_BLACK);
    tft.setCursor(x - charW, y);
}

void DisplayManager::newLine() {
    tft.setCursor(10, tft.getCursorY());
}

int32_t DisplayManager::getCursorX() {
    return tft.getCursorX();
}

int32_t DisplayManager::getCursorY() {
    return tft.getCursorY();
}

void DisplayManager::printCommandScreen() {
    if (_blocked) return;
    int32_t savedY = tft.getCursorY();
    updateStatusBar();
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, savedY);
    scrollIfNeeded();
    tft.println();
    tft.setCursor(10, tft.getCursorY());
    _cmdLineY = tft.getCursorY();
    tft.setTextColor(0x7BEF); tft.print("[");
    tft.setTextColor(TFT_CYAN); tft.print(">");
    tft.setTextColor(0x7BEF); tft.print("] ");
    tft.setTextColor(TFT_WHITE);
}

void DisplayManager::newLinePrintLn(const char* text) {
    if (_blocked) return;
    scrollIfNeeded();
    newLine();
    tft.println(text);
}

void DisplayManager::newLinePrint(const char* text) {
    if (_blocked) return;
    newLine();
    tft.print(text);
}

void DisplayManager::newLinePrint(char text) {
    if (_blocked) return;
    newLine();
    tft.print(text);
}

void DisplayManager::setCursor(uint16_t x, uint16_t y) {
    tft.setCursor(x, y);
}

void DisplayManager::printDefaultTableHelpInstructions() {
    if (_blocked) return;
    setDefaultTextSize();
    auto kv = [&](const char* k, const char* label) {
        tft.setTextColor(0x7BEF); tft.print("[");
        tft.setTextColor(TFT_GREEN); tft.print(k);
        tft.setTextColor(0x7BEF); tft.print("]");
        tft.setTextColor(TFT_WHITE); tft.print(label);
    };
    kv("a", "prev ");
    kv("l", "next ");
    kv("q", "quit ");
    kv("u", "scan");
    tft.println();
    tft.setTextColor(TFT_WHITE);
}

void DisplayManager::printSeparator(uint16_t color) {
    if (_blocked) return;
    int32_t y = tft.getCursorY();
    tft.fillRect(4, y + LINE_HEIGHT / 2, SCREEN_WIDTH - 8, 1, color);
    tft.setCursor(10, y + LINE_HEIGHT);
}

void DisplayManager::setTextColor(uint16_t color) {
    tft.setTextColor(color);
}

void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (_blocked) return;
    tft.fillRect(x, y, w, h, color);
}

void DisplayManager::launchMatrixAnimation() {
    matrix_effect.setTextAnimMode(AnimMode::SHOWCASE,
        "\nWake Up, Neo...    \nThe Matrix has you.    \nFollow     \nthe white rabbit.     \nKnock, knock, Neo.                 ");
    while (true) {
        char incomingKey = inputHandler.getKeyboardInput();
        if (incomingKey == 'q' || incomingKey == 'Q') {
            tdeck_begin();
            return;
        }
        // matrix_effect draws straight to tft, bypassing the DisplayManager
        // block check — skip it while the lock screen owns the display.
        if (isBlocked()) continue;
        matrix_effect.loop();
    }
}

void DisplayManager::setTextSize(float size) {
    tft.setTextSize(size);
}
