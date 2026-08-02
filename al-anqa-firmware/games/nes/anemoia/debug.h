#ifndef DEBUG_H
#define DEBUG_H

// AL-ANQA vendored debug.h — TFT_eSPI and OPTIMIZATION_FLAGS check removed.
// The flag is satisfied by library.json build flags.

#include "config.h"
#include <Arduino.h>

#ifdef DEBUG
    #define LOG(msg)       Serial.println(msg)
    #define LOGF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
    inline void log_pin_config() {}
#else
    #define LOG(msg)
    #define LOGF(fmt, ...)
    inline void log_pin_config() {}
#endif

#endif
