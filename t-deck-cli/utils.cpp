#include "Utils.h"
#include "command_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include <cstring>
#include <stdio.h>

extern CommandManager commandManager;
extern DisplayManager displayManager;
extern InputHandling  inputHandler;

bool Utils::startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void Utils::printHelp(char* args) {
    if (args != NULL && args[0] != '\0') {
        bool found = false;
        for (int i = 0; i < commandManager.commandCount; i++) {
            if (strcmp(args, commandManager.commands[i].name) == 0 ||
                strcmp(args, commandManager.commands[i].shortName) == 0) {
                displayManager.newLinePrint(commandManager.commands[i].name);
                displayManager.printText("/");
                displayManager.printText(commandManager.commands[i].shortName);
                displayManager.printText(" - ");
                displayManager.println(commandManager.commands[i].description);
                found = true;
                break;
            }
        }
        if (!found)
            displayManager.newLinePrintLn("Command not found.");
        displayManager.printCommandScreen();
        return;
    }

    const int PAGE_SIZE = 6;
    const int LINE_H    = 10;
    int total      = commandManager.commandCount;
    int totalPages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    int page       = 0;

    while (true) {
        displayManager.clearScreen();
        displayManager.setTextSize(1.0f);

        // ── header ───────────────────────────────────────────────
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.printText("COMMANDS ");
        displayManager.setTextColor(0x7BEF);
        char pg[10];
        sprintf(pg, "(%d/%d)", page + 1, totalPages);
        displayManager.printText(pg);
        displayManager.fillRect(5, outputY + LINE_H + 1, 310, 1, TFT_DARKGREY);

        // ── command entries ───────────────────────────────────────
        int start = page * PAGE_SIZE;
        int end   = min(start + PAGE_SIZE, total);
        int y     = outputY + LINE_H + 4;

        for (int i = start; i < end; i++) {
            displayManager.setCursor(10, y);
            displayManager.setTextColor(TFT_GREEN);
            displayManager.printText(commandManager.commands[i].name);
            displayManager.setTextColor(TFT_DARKGREY);
            displayManager.printText("/");
            displayManager.printText(commandManager.commands[i].shortName);

            y += LINE_H;
            displayManager.setCursor(16, y);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText(commandManager.commands[i].description);
            y += LINE_H;
        }

        // ── bottom nav ────────────────────────────────────────────
        displayManager.fillRect(5, y + 1, 310, 1, TFT_DARKGREY);
        displayManager.setCursor(10, y + 4);
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText("--");
        displayManager.setTextColor(TFT_GREEN);
        displayManager.printText("a=prev");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText("-");
        displayManager.setTextColor(TFT_GREEN);
        displayManager.printText("l=next");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText("-");
        displayManager.setTextColor(TFT_GREEN);
        displayManager.printText("q=quit");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText("--");

        displayManager.setDefaultTextSize();
        displayManager.setTextColor(TFT_WHITE);

        // ── wait for key ──────────────────────────────────────────
        bool flip = false;
        while (!flip) {
            char key = inputHandler.getKeyboardInput();
            if (key == 'q' || key == 'Q') {
                displayManager.clearInputText();
                return;
            } else if ((key == 'l' || key == 'L') && page < totalPages - 1) {
                page++;
                flip = true;
            } else if ((key == 'a' || key == 'A') && page > 0) {
                page--;
                flip = true;
            }
        }
    }
}

String Utils::getValue(const String& data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    }

    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
