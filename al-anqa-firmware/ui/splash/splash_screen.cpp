#include "splash_screen.h"
#include "splash_image.h"
#include "input_handling.h"
#include "display_manager.h"   // brings in LGFX_T-Deck.h (no include guard — don't include it directly too)
#include <Arduino.h>

extern LGFX           tft;
extern InputHandling  inputHandler;
extern DisplayManager displayManager;

void showSplashScreen(bool holdUntilKey) {
    // Block display output for the duration so ClockManager's 3s status-bar
    // refresh (clock_manager.cpp — gated on isBlocked()) can't paint over the
    // splash while it is held on screen. Restore whatever state we found.
    bool wasBlocked = displayManager.isBlocked();
    displayManager.setBlocked(true);

    tft.fillScreen(TFT_BLACK);
    tft.drawPng(SPLASH_PNG, SPLASH_PNG_LEN, 0, 0, tft.width(), tft.height());

    uint32_t start = millis();
    while (holdUntilKey || millis() - start < 3000) {
        if (inputHandler.getKeyboardInput() != 0) break;
        delay(10);
    }

    tft.fillScreen(TFT_BLACK);
    displayManager.setBlocked(wasBlocked);
}
