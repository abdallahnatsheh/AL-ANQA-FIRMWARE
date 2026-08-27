#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include "board.h"   // BOARD_HAS_GPS must be defined before the guard below
                     // (it's a header macro, not a -D build flag)
#if defined(BOARD_HAS_GPS)
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
    float       altitude()       const { return _alt; }   // metres MSL; 0 until a 3D fix
    float       hdop()           const { return _hdop; }  // horizontal dilution; 0 = unknown
    uint32_t    satellites()     const { return _sats; }
    bool        timeValid()      const { return _timeValid; }
    uint8_t     hour()           const { return _hour; }
    uint8_t     minute()         const { return _minute; }
    uint8_t     second()         const { return _second; }
    bool        dateValid()      const { return _dateValid; }
    uint8_t     day()            const { return _day; }
    uint8_t     month()          const { return _month; }
    uint16_t    year()           const { return _year; }
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
    volatile float    _alt       = 0.0f;   // altitude metres MSL (TinyGPS++ altitude.meters())
    volatile float    _hdop      = 0.0f;   // horizontal dilution of precision (hdop.hdop())
    volatile uint32_t _sats      = 0;
    volatile bool     _timeValid = false;
    volatile uint8_t  _hour      = 0;
    volatile uint8_t  _minute    = 0;
    volatile uint8_t  _second    = 0;
    volatile bool     _dateValid = false;
    volatile uint8_t  _day       = 0;
    volatile uint8_t  _month     = 0;
    volatile uint16_t _year      = 0;
    volatile uint32_t _chars     = 0;
    char              _module[24] = "Unknown";
    bool              _nvsCachedFix = false; // true if a GPS fix was ever saved to NVS
    bool              _fixSaved     = false; // true after first fix saved this session
    int32_t           _nvsLat       = 0;     // last fix lat × 1e7 (loaded from NVS)
    int32_t           _nvsLon       = 0;     // last fix lon × 1e7 (loaded from NVS)
};

#endif // BOARD_HAS_GPS
#endif // GPS_MANAGER_H
