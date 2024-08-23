#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <Arduino.h>
#include "display_manager.h"
#include "wifi_functions.h"

typedef std::function<void(char*)> CommandFunction;

struct Command {
    const char* name;
    const char* shortName;
    CommandFunction function;
    const char* description;
    bool haveArgs;
};
class CommandManager {
public:
    CommandManager();
    void registerCommand(const char* name, const char* shortName, CommandFunction function, const char* description, bool haveArgs);
    void processInput(char input);
    void executeCommand();
    void setupCommands();
    static const uint16_t bufferSize = 64;
    char command[bufferSize];
    Command commands[64];
    uint8_t commandCount;
private:
    uint8_t commandIndex;
    void resetCommand();
};

#endif // COMMAND_MANAGER_H