#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <Arduino.h>
#include "display_manager.h"
#include "wifi_functions.h"
#include "input_handling.h"

typedef std::function<void(char*)> CommandFunction;

enum CompType {
    COMP_NONE,   // no file/dir suggestions (network cmds, etc.)
    COMP_ANY,    // files and directories
    COMP_DIR,    // directories only (cd)
    COMP_FILE,   // files only (rm, ux)
};

struct Command {
    const char* name;
    const char* shortName;
    CommandFunction function;
    const char* description;
    bool haveArgs;
    const char* category;
    CompType compType;
};

class CommandManager {
public:
    CommandManager();
    void registerCommand(const char* name, const char* shortName, CommandFunction function,
                         const char* description, bool haveArgs, const char* category,
                         CompType compType = COMP_NONE);
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
    void doAutocomplete();

    static constexpr int kHistSize = 16;
    char  _hist[kHistSize][bufferSize];
    int   _histCount;
    int   _histHead;
    int   _histNav;
    char  _histSaved[bufferSize];
};

#endif // COMMAND_MANAGER_H