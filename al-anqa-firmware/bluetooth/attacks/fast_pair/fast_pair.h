#ifndef FAST_PAIR_H
#define FAST_PAIR_H

#include <stdint.h>

// fp — Google Fast Pair attack suite (CVE-2025-36911 / WhisperPair)
//   fp [scan]    — BLE scan for 0xFE2C service data
//   fp spam      — advertisement flood cycling known model IDs
//   fp h <idx>   — GATT hijack: WhisperPair probe → Key-based Pairing char
//   fp h all     — test all scanned devices in sequence

class FastPair {
public:
    void command(const char* args);

private:
    void scan();
    void spam();
    void hijack(int idx);
    void hijackAll();
    void listenTo(const uint8_t* classicBda, const char* name);

    bool lookupKey(uint32_t modelId, const char* name, uint8_t* out64);
    bool loadFromSD(uint32_t modelId, uint8_t* out64);
    void saveToSD(uint32_t modelId, const char* name, const uint8_t* key64);
    void savePaired(const char* classicAddr, const char* name);
    void saveLog(const char* mac, uint32_t modelId, const char* name,
                 int8_t rssi, const char* status);

    bool doGattAttack(const char* bdaStr, uint8_t addrType,
                      uint32_t modelId, const char* name);
};

extern FastPair fastPair;
extern bool     g_fpBtInited;

#endif // FAST_PAIR_H
