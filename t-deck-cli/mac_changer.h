#pragma once
#include <Arduino.h>
#include "display_manager.h"

class MacChanger {
public:
    static MacChanger& getInstance();
    void   begin();
    void   handleCommand(char* args);
    void   applyIfEnabled();
    bool   isEnabled()  const { return _enabled; }
    bool   isCustom()   const { return _useCustom; }
    String spoofedMacStr() const;

private:
    MacChanger() = default;
    bool    _enabled   = false;
    bool    _useCustom = false;
    uint8_t _customMac[6]  = {};
    uint8_t _currentMac[6] = {};

    void randomMac(uint8_t* mac);
    void applyMac(const uint8_t* mac);
    void loadConfig();
    void saveConfig();
    void printStatus();
};
