#include "splash_screen.h"
#include "splash_image.h"
#include "LGFX_T-Deck.h"
#include "input_handling.h"
#include <Arduino.h>

extern LGFX          tft;
extern InputHandling inputHandler;

void showSplashScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.drawPng(SPLASH_PNG, SPLASH_PNG_LEN, 0, 0, tft.width(), tft.height());

    uint32_t start = millis();
    while (millis() - start < 3000) {
        if (inputHandler.getKeyboardInput() != 0) break;
        delay(10);
    }

    tft.fillScreen(TFT_BLACK);
}
