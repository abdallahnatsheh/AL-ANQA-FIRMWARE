#include "Utils.h"
#include "command_manager.h"
#include "display_manager.h"
#include <cstring>

extern CommandManager commandManager;
extern DisplayManager displayManager;

bool Utils::startsWith(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

void Utils::printHelp(char* args) {
    Serial.print("Printing help args ");
    Serial.println(args);
    
    if (args != NULL) {
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
        
        if (!found) {
            displayManager.newLinePrintLn("Command not found. Type 'help' to see available commands.");
        }
    } else {
        displayManager.newLinePrintLn("Available commands:");
        for (int i = 0; i < commandManager.commandCount; i++) {
            displayManager.newLinePrint(commandManager.commands[i].name);
            displayManager.printText("/");
            displayManager.printText(commandManager.commands[i].shortName);
            displayManager.printText(" - ");
            displayManager.println(commandManager.commands[i].description);
        }
    }
    
    displayManager.printCommandScreen();
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