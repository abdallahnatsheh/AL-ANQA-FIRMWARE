#include "display_manager.h"
#include "utilities.h"
#include <Wire.h>
#include <string>
#include <Arduino.h>
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
    clearScreen();
    tdeck_begin();
}

void DisplayManager::clearScreen() {
    tft.fillRect(0, promptY + promptHeight, SCREEN_WIDTH, SCREEN_HEIGHT - (promptY + promptHeight), TFT_BLACK);
}

void DisplayManager::tdeck_begin() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    printFirstCommandScreen("");
}

void DisplayManager::printFirstCommandScreen(const char* command) {
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.print("CMD> ");
    tft.print(command);
}
void DisplayManager::setDefaultTextSize(){
    tft.setTextSize(1.2,1.2);
}
void DisplayManager::printText(const char* text){
    tft.print(text);
}
void DisplayManager::println(const char* text){
    tft.println(text);
}
void DisplayManager::println(String text){
    tft.println(text);
}
void DisplayManager::println(int text){
    tft.println(text);
}
void DisplayManager::println(){
    tft.println();
}
void DisplayManager::printText(const char* text, uint16_t x, uint16_t y){
    tft.setCursor(x, y);
    tft.print(text);
}
void DisplayManager::printText(const char* text, uint16_t x, uint16_t y, uint16_t color){
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(text);
}
void DisplayManager::printText(char incoming){
    tft.print(incoming);
}
void DisplayManager::printText(int incoming){
    tft.print(incoming);
}
void DisplayManager::printText(const std::string& text){
    tft.print(text.c_str());
}
void DisplayManager::printText(const String& text){
    tft.print(text);
}
void DisplayManager::backspaceChar(){
    tft.fillRect(tft.getCursorX() - 7, tft.getCursorY(), SCREEN_WIDTH, promptHeight, TFT_BLACK);
    tft.setCursor(tft.getCursorX() - 7, tft.getCursorY());
}
void DisplayManager::newLine(){
    tft.setCursor(10, tft.getCursorY());
}
int32_t DisplayManager::getCursorX() {
    return tft.getCursorX();
}
int32_t DisplayManager::getCursorY() {
    return tft.getCursorY();
}
void DisplayManager::clearInputText()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.setCursor(10, promptY + 8);
    tft.println("T-DECK CLI v0.1");
    tft.setCursor(10, outputY);
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.println("CMD> ");
}
void DisplayManager::printCommandScreen(){
    tft.setTextColor(TFT_WHITE);
    setDefaultTextSize();
    tft.println();
    tft.setCursor(10, tft.getCursorY());
    tft.print("CMD> ");
}
void DisplayManager::newLinePrintLn(const char* text){
    newLine();
    tft.println(text);
}
void DisplayManager::newLinePrint(const char* text){
    newLine();
    tft.print(text);
}
void DisplayManager::newLinePrint(char text){
    newLine();
    tft.print(text);
}
void DisplayManager::setCursor(uint16_t x, uint16_t y) {
    tft.setCursor(x, y);
}

void DisplayManager::printDefaultTableHelpInstructions(){
        setDefaultTextSize();
        tft.setTextColor(TFT_WHITE);
        tft.print("-------");
        tft.setTextColor(TFT_GREEN);
        tft.print("a=prev");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("l=next");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("q=quit");
        tft.setTextColor(TFT_WHITE);
        tft.print("--");
        tft.setTextColor(TFT_GREEN);
        tft.print("u=update");
        tft.setTextColor(TFT_WHITE);
        tft.println("------");
}
void DisplayManager::setTextColor(uint16_t color){
    tft.setTextColor(color);
}
void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}
void  DisplayManager::launchMatrixAnimation(){
    matrix_effect.setTextAnimMode(AnimMode::SHOWCASE, "\nWake Up, Neo...    \nThe Matrix has you.    \nFollow     \nthe white rabbit.     \nKnock, knock, Neo.                 ");
    while (true){
    char incomingKey = inputHandler.getKeyboardInput();
    if (incomingKey == 'q' || incomingKey == 'Q')
    {
        tdeck_begin();
        return;
    }
    else {
        matrix_effect.loop();
        }
    }
}
void DisplayManager::setTextSize(float size){
    tft.setTextSize(size);
}
