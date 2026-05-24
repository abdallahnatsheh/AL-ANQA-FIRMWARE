#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "LGFX_T-Deck.h"
#include <DigitalRainAnimation.hpp>

const uint16_t promptHeight = 30;
const uint16_t promptY      = 0;
const uint16_t outputY      = promptY + promptHeight + 8;

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define LINE_HEIGHT   14

class DisplayManager {
public:
    DisplayManager(LGFX& tft);
    void init();
    void tdeck_begin();
    void updateStatusBar();
    void printFirstCommandScreen(const char* command);
    void setDefaultTextSize();
    void printText(const char *text);
    void println();
    void println(const char *text);
    void println(String text);
    void println(int text);
    void printText(const char *text, uint16_t x, uint16_t y);
    void printText(const char *text, uint16_t x, uint16_t y, uint16_t color);
    void printText(char incoming);
    void printText(const std::string& text);
    void printText(int incoming);
    void printText(const String& text);
    void newLinePrint(char text);
    void clearScreen();
    void backspaceChar();
    void newLine();
    void clearInputText();
    void printCommandScreen();
    void newLinePrintLn(const char *text);
    void newLinePrint(const char *text);
    int32_t getCursorY();
    int32_t getCursorX();
    void setCursor(uint16_t x, uint16_t y);
    void printDefaultTableHelpInstructions();
    void printSeparator(uint16_t color = 0x7BEF);
    void setTextColor(uint16_t colour);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void launchMatrixAnimation();
    void setTextSize(float size);
    void setBtActive(bool active);
    void setWGuardState(bool active, uint8_t maxSev);
    void redrawCommandLine(const char* cmd, int cursorPos);
    void flushSPI();   // Drain any pending LovyanGFX DMA and release SPI2 bus
    void setBlocked(bool b) { _blocked = b; }
    bool isBlocked() const  { return _blocked; }
private:
    LGFX& tft;
    bool    _btActive      = false;
    bool    _wgActive      = false;
    uint8_t _wgMaxSev      = 0;
    int32_t _cmdLineY  = outputY;
    DigitalRainAnimation<LGFX> matrix_effect = DigitalRainAnimation<LGFX>();
    void scrollIfNeeded();
    bool _blocked = false;
};

#endif // DISPLAY_MANAGER_H
