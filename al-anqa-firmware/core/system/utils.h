#ifndef UTILS_H
#define UTILS_H
 #include <Arduino.h>
#include <string.h>

class Utils {
public:
    static bool startsWith(const char* str, const char* prefix);
    // True only when str matches prefix followed by '\0' or ' '
    static bool matchesCmd(const char* str, const char* prefix);
    static void printHelp(char* args);
    // Reject a bad argument by printing the command's own registered help line
    // (single source of truth in the command table) — DRY: no hand-copied usage
    // strings in each handler. `cmd` is the long OR short name.
    static void printUsage(const char* cmd);
    // Validate a WiFi-channel argument (the only arg of wm/es/est/ev): true if `a`
    // is empty (default) or an integer 0..13. Otherwise prints `cmd`'s usage and
    // returns false — stops "wm ff" (atoi→0) from silently running.
    static bool checkChannelArg(const char* a, const char* cmd);
    static String getValue(const String& data, char separator, int index);
};

#endif // UTILS_H