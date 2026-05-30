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
    static String getValue(const String& data, char separator, int index);
};

#endif // UTILS_H