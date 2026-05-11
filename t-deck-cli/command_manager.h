#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <Arduino.h>
#include "display_manager.h"
#include "wifi_functions.h"
#include "input_handling.h"

typedef std::function<void(char*)> CommandFunction;

struct Command {
    const char* name;
    const char* shortName;
    CommandFunction function;
    const char* description;
    bool haveArgs;
    const char* category;
};
class CommandManager {
public:
    CommandManager();
    void registerCommand(const char* name, const char* shortName, CommandFunction function, const char* description, bool haveArgs, const char* category);
    void processInput(char input);
    void processTrackball(TrackballEvent evt);
    void executeCommand();
    void setupCommands();
    static const uint16_t bufferSize = 128;
    char command[bufferSize];
    Command commands[64];
    uint8_t commandCount;
private:
    uint8_t commandIndex;
    int     _cursorPos;
    void resetCommand();
};

#endif // COMMAND_MANAGER_H