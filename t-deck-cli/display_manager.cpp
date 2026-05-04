#include "display_manager.h"
#include "utilities.h"
#include <Wire.h>
#include <string>
#include <Arduino.h>
#include <WiFi.h>
#include "input_handling.h"

extern InputHandling inputHandler;

DisplayManager::DisplayManager(LGFX& tft) : tft(tft) {}

void DisplayManager::init() {
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    tft.init();
    tft.setRotation(1);
    tft.setBrightness(128);
    tft.invertDisplay(1);
    matrix_effect.init(&tft);
    tft.fillScreen(TFT_BLACK);
    tdeck_begin();
}

void DisplayManager::updateStatusBar() {
    tft.fillRect(0, promptY, SCREEN_WIDTH, promptHeight, 0x000F);
    tft.drawFastHLine(0, promptY + promptHeight - 1, SCREEN_WIDTH, TFT_DARKGREY);

    setDefaultTextSize();

    tft.setTextColor(TFT_CYAN);
    tft.setCursor(5, promptY + 8);
    tft.print("T-DECK");

    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(90, promptY + 8);
        tft.print(WiFi.localIP().toString());
    } else {
        tft.setTextColor(0x7BEF);
        tft.setCursor(110, promptY + 8);
        tft.print("OFFLINE");
    }

    tft.setTextColor(0x7BEF);
    tft.setCursor(278, promptY + 8);
    tft.print("v0.1");

    tft.setTextColor(TFT_WHITE);
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
    if (y > SCREEN_HEIGHT - LINE_HEIGHT * 2 || y < outputY) {
        clearScreen();
        tft.setCursor(10, outputY);
    }
}

void DisplayManager::clearScreen() {
    tft.fillRect(0, promptY + promptHeight, SCREEN_WIDTH, SCREEN_HEIGHT - (promptY + promptHeight), TFT_BLACK);
}

void DisplayManager::printFirstCommandScreen(const char* command) {
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.print("CMD> ");
    tft.print(command);
}

void DisplayManager::setDefaultTextSize() {
    tft.setTextSize(1.2, 1.2);
}

void DisplayManager::printText(const char* text) {
    tft.print(text);
}

void DisplayManager::println(const char* text) {
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println(String text) {
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println(int text) {
    scrollIfNeeded();
    tft.println(text);
}

void DisplayManager::println() {
    scrollIfNeeded();
    tft.println();
}

void DisplayManager::printText(const char* text, uint16_t x, uint16_t y) {
    tft.setCursor(x, y);
    tft.print(text);
}

void DisplayManager::printText(const char* text, uint16_t x, uint16_t y, uint16_t color) {
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(text);
}

void DisplayManager::printText(char incoming) {
    tft.print(incoming);
}

void DisplayManager::printText(int incoming) {
    tft.print(incoming);
}

void DisplayManager::printText(const std::string& text) {
    tft.print(text.c_str());
}

void DisplayManager::printText(const String& text) {
    tft.print(text);
}

void DisplayManager::backspaceChar() {
    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    int16_t charW = (int16_t)(6 * 1.2);
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
    int32_t savedY = tft.getCursorY();
    updateStatusBar();
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, savedY);
    scrollIfNeeded();
    tft.println();
    tft.setCursor(10, tft.getCursorY());
    tft.print("CMD> ");
}

void DisplayManager::newLinePrintLn(const char* text) {
    scrollIfNeeded();
    newLine();
    tft.println(text);
}

void DisplayManager::newLinePrint(const char* text) {
    newLine();
    tft.print(text);
}

void DisplayManager::newLinePrint(char text) {
    newLine();
    tft.print(text);
}

void DisplayManager::setCursor(uint16_t x, uint16_t y) {
    tft.setCursor(x, y);
}

void DisplayManager::printDefaultTableHelpInstructions() {
    setDefaultTextSize();
    tft.setTextColor(TFT_WHITE);
    tft.print("--");
    tft.setTextColor(TFT_GREEN);
    tft.print("a=prev");
    tft.setTextColor(TFT_WHITE);
    tft.print("-");
    tft.setTextColor(TFT_GREEN);
    tft.print("l=next");
    tft.setTextColor(TFT_WHITE);
    tft.print("-");
    tft.setTextColor(TFT_GREEN);
    tft.print("q=quit");
    tft.setTextColor(TFT_WHITE);
    tft.print("-");
    tft.setTextColor(TFT_GREEN);
    tft.print("u=update");
    tft.setTextColor(TFT_WHITE);
    tft.println("--");
}

void DisplayManager::setTextColor(uint16_t color) {
    tft.setTextColor(color);
}

void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
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
        matrix_effect.loop();
    }
}

void DisplayManager::setTextSize(float size) {
    tft.setTextSize(size);
}
