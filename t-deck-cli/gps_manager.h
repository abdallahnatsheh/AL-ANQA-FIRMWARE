#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#ifdef BOARD_TDECK_PLUS
#include <Arduino.h>
#include <TinyGPS++.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class GpsManager {
public:
    static GpsManager& instance();

    void start();
    void stop();

    bool        isRunning()      const { return _task != nullptr; }
    bool        isValid()        const { return _valid; }
    float       lat()            const { return _lat; }
    float       lon()            const { return _lon; }
    uint32_t    satellites()     const { return _sats; }
    bool        timeValid()      const { return _timeValid; }
    uint8_t     hour()           const { return _hour; }
    uint8_t     minute()         const { return _minute; }
    uint8_t     second()         const { return _second; }
    uint32_t    charsProcessed() const { return _chars; }
    const char* moduleName()     const { return _module; }

private:
    GpsManager() = default;
    static void gpsTask(void* pv);
    void        initModule();
    bool        initL76K();
    bool        recoverUblox();
    int         getUbloxAck(uint8_t cls, uint8_t id);
    bool        loadGpsNvsFlag();
    void        saveGpsFixFlag();
    void        applyUbloxHotStart();

    HardwareSerial* _serial    = nullptr;
    TinyGPSPlus     _gps;
    TaskHandle_t    _task      = nullptr;
    volatile bool   _stop      = false;

    // Written only from gpsTask (core 0); volatile is sufficient for
    // 4-byte aligned primitive reads on ARM32 (single-instruction load).
    volatile bool     _valid     = false;
    volatile float    _lat       = 0.0f;
    volatile float    _lon       = 0.0f;
    volatile uint32_t _sats      = 0;
    volatile bool     _timeValid = false;
    volatile uint8_t  _hour      = 0;
    volatile uint8_t  _minute    = 0;
    volatile uint8_t  _second    = 0;
    volatile uint32_t _chars     = 0;
    char              _module[24] = "Unknown";
    bool              _nvsCachedFix = false; // true if a GPS fix was ever saved to NVS
    bool              _fixSaved     = false; // true after first fix saved this session
    int32_t           _nvsLat       = 0;     // last fix lat × 1e7 (loaded from NVS)
    int32_t           _nvsLon       = 0;     // last fix lon × 1e7 (loaded from NVS)
};

#endif // BOARD_TDECK_PLUS
#endif // GPS_MANAGER_H
