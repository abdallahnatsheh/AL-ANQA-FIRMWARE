#include "Utils.h"
#include "command_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include <cstring>
#include <cctype>
#include <stdio.h>

extern CommandManager commandManager;
extern DisplayManager displayManager;
extern InputHandling  inputHandler;

bool Utils::startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool Utils::matchesCmd(const char* str, const char* prefix) {
    size_t n = strlen(prefix);
    return strncmp(str, prefix, n) == 0 && (str[n] == '\0' || str[n] == ' ');
}

void Utils::printHelp(char* args) {
    // ── specific command lookup ───────────────────────────────────────────────
    if (args && args[0] != '\0') {
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
        if (!found) displayManager.newLinePrintLn("Command not found.");
        displayManager.printCommandScreen();
        return;
    }

    // ── build ordered category list ───────────────────────────────────────────
    const char* cats[16];
    int catCount = 0;
    for (int i = 0; i < commandManager.commandCount; i++) {
        const char* cat = commandManager.commands[i].category;
        bool dup = false;
        for (int j = 0; j < catCount; j++) {
            if (strcmp(cats[j], cat) == 0) { dup = true; break; }
        }
        if (!dup && catCount < 16) cats[catCount++] = cat;
    }

    const int LINE_H  = 14;
    const int CMDS_PP = 5;   // commands per sub-page (5×2 lines×14px = 140px, fits with nav bar)
    int catIdx = 0, subPage = 0;

    while (true) {
        // collect command indices for current category
        int idx[64]; int cnt = 0;
        for (int i = 0; i < commandManager.commandCount; i++)
            if (strcmp(commandManager.commands[i].category, cats[catIdx]) == 0 && cnt < 64)
                idx[cnt++] = i;
        int subPages = (cnt + CMDS_PP - 1) / CMDS_PP;
        if (subPages < 1) subPages = 1;
        if (subPage >= subPages) subPage = subPages - 1;

        displayManager.clearScreen();

        // ── header: category name + page indicator ────────────────────────────
        displayManager.setCursor(10, outputY);
        char upcat[20]; int ci = 0;
        for (const char* p = cats[catIdx]; *p && ci < 19; p++, ci++)
            upcat[ci] = toupper((unsigned char)*p);
        upcat[ci] = '\0';
        char pg[16];
        if (subPages > 1)
            snprintf(pg, sizeof(pg), "%d/%d %d/%d", catIdx+1, catCount, subPage+1, subPages);
        else
            snprintf(pg, sizeof(pg), "%02d/%02d", catIdx+1, catCount);
        displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);   displayManager.printText("HELP");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText(upcat);
        displayManager.setTextColor(0x7BEF);     displayManager.printText("]  ");
        displayManager.setTextColor(0x7BEF);     displayManager.println(pg);
        displayManager.fillRect(5, outputY + LINE_H + 1, 310, 1, TFT_CYAN);

        // ── commands for this sub-page ────────────────────────────────────────
        int y = outputY + LINE_H + 4;
        int start = subPage * CMDS_PP;
        int end   = min(start + CMDS_PP, cnt);
        for (int i = start; i < end; i++) {
            displayManager.setCursor(10, y);
            displayManager.setTextColor(TFT_GREEN);
            displayManager.printText(commandManager.commands[idx[i]].name);
            displayManager.setTextColor(TFT_DARKGREY);
            displayManager.printText("/");
            displayManager.printText(commandManager.commands[idx[i]].shortName);
            y += LINE_H;
            displayManager.setCursor(16, y);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText(commandManager.commands[idx[i]].description);
            y += LINE_H;
        }

        // ── nav bar ───────────────────────────────────────────────────────────
        displayManager.fillRect(5, y + 1, 310, 1, TFT_CYAN);
        displayManager.setCursor(10, y + 4);
        auto kv = [&](const char* k, const char* label) {
            displayManager.setTextColor(0x7BEF); displayManager.printText("[");
            displayManager.setTextColor(TFT_GREEN); displayManager.printText(k);
            displayManager.setTextColor(0x7BEF); displayManager.printText("]");
            displayManager.setTextColor(TFT_WHITE); displayManager.printText(label);
        };
        kv("a", "prev ");
        kv("l", "next ");
        kv("q", "quit");

        displayManager.setDefaultTextSize();
        displayManager.setTextColor(TFT_WHITE);

        // ── key wait ──────────────────────────────────────────────────────────
        bool flip = false;
        while (!flip) {
            char key = inputHandler.getKeyboardInput();
            if (key == 'q' || key == 'Q') { displayManager.clearInputText(); return; }
            if (key == 'l' || key == 'L') {
                if      (subPage < subPages - 1)  { subPage++; flip = true; }
                else if (catIdx  < catCount  - 1) { catIdx++;  subPage = 0; flip = true; }
            }
            if (key == 'a' || key == 'A') {
                if      (subPage > 0) { subPage--; flip = true; }
                else if (catIdx  > 0) { catIdx--;  subPage = 0; flip = true; }
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
