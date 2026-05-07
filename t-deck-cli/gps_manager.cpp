#include "gps_manager.h"

#ifdef BOARD_TDECK_PLUS
#include "utilities.h"

GpsManager& GpsManager::instance() {
    static GpsManager inst;
    return inst;
}

// ── GPS task (core 0 — parallel with BLE/WiFi on core 1) ─────────────────────
void GpsManager::gpsTask(void* pv) {
    GpsManager* self = (GpsManager*)pv;
    while (!self->_stop) {
        while (self->_serial->available())
            self->_gps.encode((char)self->_serial->read());

        if (self->_gps.location.isValid()) {
            self->_lat   = (float)self->_gps.location.lat();
            self->_lon   = (float)self->_gps.location.lng();
            self->_valid = true;
        }
        if (self->_gps.satellites.isValid())
            self->_sats = self->_gps.satellites.value();
        if (self->_gps.time.isValid()) {
            self->_hour      = self->_gps.time.hour();
            self->_minute    = self->_gps.time.minute();
            self->_second    = self->_gps.time.second();
            self->_timeValid = true;
        }
        self->_chars = self->_gps.charsProcessed();

        vTaskDelay(pdMS_TO_TICKS(30));
    }
    self->_serial->end();
    delete self->_serial;
    self->_serial = nullptr;
    self->_task   = nullptr;
    vTaskDelete(nullptr);
}

// ── u-blox ACK wait ───────────────────────────────────────────────────────────
int GpsManager::getUbloxAck(uint8_t cls, uint8_t id) {
    uint8_t  buf[32];
    uint16_t fc = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        while (_serial->available()) {
            int c = _serial->read();
            switch (fc) {
                case 0: fc = (c == 0xB5) ? 1 : 0; break;
                case 1: fc = (c == 0x62) ? 2 : 0; break;
                case 2: fc = (c == cls)  ? 3 : 0; break;
                case 3: fc = (c == id)   ? 4 : 0; break;
                case 4: {
                    uint16_t need = c;
                    uint32_t tw = millis() + 100;
                    while (!_serial->available() && millis() < tw) {}
                    need |= (_serial->read() << 8);
                    if (need == 0 || need > sizeof(buf)) { fc = 0; break; }
                    if ((int)_serial->readBytes(buf, need) == need) return (int)need;
                    fc = 0; break;
                }
                default: fc = 0; break;
            }
        }
    }
    return 0;
}

// ── Quectel L76K init ─────────────────────────────────────────────────────────
bool GpsManager::initL76K() {
    for (int attempt = 0; attempt < 3; attempt++) {
        _serial->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
        delay(100);
        while (_serial->available()) _serial->read();
        delay(100);
        _serial->write("$PCAS06,0*1B\r\n");
        uint32_t t0 = millis();
        while (!_serial->available() && millis() - t0 < 600) {}
        _serial->setTimeout(200);
        String ver = _serial->readStringUntil('\n');
        if (ver.startsWith("$GPTXT,01,01,02")) {
            _serial->write("$PCAS04,5*1C\r\n");  delay(250);
            // GGA + RMC only — keeps throughput low enough for 1-s BLE scan
            _serial->write("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0*02\r\n"); delay(250);
            _serial->write("$PCAS11,3*1E\r\n");  delay(100);
            return true;
        }
        delay(500);
    }
    return false;
}

// ── u-blox M10Q recovery ──────────────────────────────────────────────────────
bool GpsManager::recoverUblox() {
    static const uint8_t CFG1[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0xFF,0xFF,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x1C,0xA2};
    static const uint8_t CFG2[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0xFF,0xFF,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x1B,0xA1};
    static const uint8_t CFG3[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0x00,0x00,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x03,0x1D,0xB3};
    static const uint8_t RATE[] = {0xB5,0x62,0x06,0x08,0x00,0x00,0x0E,0x30};
    _serial->write(CFG1, sizeof(CFG1)); getUbloxAck(0x05, 0x01);
    _serial->write(CFG2, sizeof(CFG2)); getUbloxAck(0x05, 0x01);
    _serial->write(CFG3, sizeof(CFG3)); getUbloxAck(0x05, 0x01);
    _serial->write(RATE, sizeof(RATE));
    return getUbloxAck(0x06, 0x08) > 0;
}

// ── public interface ──────────────────────────────────────────────────────────
void GpsManager::initModule() {
    if (!initL76K()) {
        _serial->begin(38400, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
        if (!recoverUblox()) {
            _serial->updateBaudRate(9600);
            recoverUblox();
        }
    }
}

void GpsManager::start() {
    if (_task) return; // already running

    _valid = false; _lat = 0; _lon = 0; _sats = 0;
    _timeValid = false; _hour = 0; _minute = 0; _second = 0; _chars = 0;
    _stop = false;

    _serial = new HardwareSerial(1);
    _serial->setRxBufferSize(1024);
    _serial->begin(BOARD_GPS_BAUD, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    initModule();

    // Pin to core 0 — truly parallel with BLE/WiFi scan on core 1
    xTaskCreatePinnedToCore(gpsTask, "gps_bg", 3072, this, 2, &_task, 0);
}

void GpsManager::stop() {
    if (!_task) return;
    _stop = true;
    // Task cleans up serial and calls vTaskDelete itself
    uint32_t t0 = millis();
    while (_task && millis() - t0 < 2000) vTaskDelay(pdMS_TO_TICKS(50));
    // If task didn't self-delete in time, force it
    if (_task) { vTaskDelete(_task); _task = nullptr; }
    if (_serial) { _serial->end(); delete _serial; _serial = nullptr; }
    _valid = false;
}

#endif // BOARD_TDECK_PLUS
