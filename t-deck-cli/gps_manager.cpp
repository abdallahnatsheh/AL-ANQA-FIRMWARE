#include "gps_manager.h"
#include "display_manager.h"
#include "input_handling.h"
#include <Preferences.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

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
            if (!self->_fixSaved) {
                self->saveGpsFixFlag();
                self->_fixSaved = true;
            }
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
            _serial->write("$PCAS04,7*1E\r\n");  delay(250);  // GPS+GLONASS+BDS
            _serial->write("$PMTK313,1*2E\r\n"); delay(100);  // enable SBAS search
            _serial->write("$PMTK301,2*2E\r\n"); delay(100);  // use SBAS corrections
            _serial->write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n"); delay(250);
            _serial->write("$PCAS11,3*1E\r\n");  delay(100);
            _serial->write("$PCAS00*01\r\n");    delay(400);  // persist config to flash
            if (_nvsCachedFix) {
                _serial->write("$PCAS10,0*1C\r\n"); delay(800); // hot start from BBR
            }
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
    _serial->begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("Trying L76K at 9600 baud");
    if (initL76K()) {
        strncpy(_module, _nvsCachedFix ? "L76K +BDS+SBAS warm" : "L76K +BDS+SBAS", sizeof(_module) - 1);
        return;
    }

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.println("Trying u-blox at 38400 baud");
    _serial->begin(38400, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    if (recoverUblox()) {
        applyUbloxHotStart();
        strncpy(_module, _nvsCachedFix ? "M10Q @38400 warm" : "M10Q @38400", sizeof(_module) - 1);
        return;
    }

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.println("Trying u-blox at 9600 baud");
    _serial->updateBaudRate(9600);
    if (recoverUblox()) {
        applyUbloxHotStart();
        strncpy(_module, _nvsCachedFix ? "M10Q @9600 warm" : "M10Q @9600", sizeof(_module) - 1);
        return;
    }

    strncpy(_module, "Unknown (no ack)", sizeof(_module) - 1);
}

void GpsManager::start() {
    if (_task) return; // already running

    _valid = false; _lat = 0; _lon = 0; _sats = 0;
    _timeValid = false; _hour = 0; _minute = 0; _second = 0; _chars = 0;
    _stop = false;
    _fixSaved     = false;
    _nvsCachedFix = loadGpsNvsFlag();

    _serial = new HardwareSerial(1);
    _serial->setRxBufferSize(1024);
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

// ── command entry points (called from command_manager) ────────────────────────
void runGpsOn() {
    GpsManager& gm = GpsManager::instance();

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    if (!gm.isRunning()) {
        displayManager.println("GPS BACKGROUND — starting...");
        gm.start();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Task running on core 0");
    } else {
        displayManager.println("GPS BACKGROUND — already running");
    }

    uint32_t lastDraw = 0;
    while (true) {
        if (millis() - lastDraw >= 1000) {
            lastDraw = millis();
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();

            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.println("GPS BACKGROUND");

            char buf[52];
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_GREEN);
            snprintf(buf, sizeof(buf), "Module: %s", gm.moduleName());
            displayManager.println(buf);

            uint32_t chars = gm.charsProcessed();
            uint32_t sats  = gm.satellites();
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(chars > 0 ? TFT_GREEN : TFT_RED);
            snprintf(buf, sizeof(buf), "Chars: %lu   Sats: %lu", (unsigned long)chars, (unsigned long)sats);
            displayManager.println(buf);

            displayManager.setCursor(4, displayManager.getCursorY());
            if (chars == 0) {
                displayManager.setTextColor(TFT_RED);
                displayManager.println("No data — module not responding");
            } else if (!gm.isValid()) {
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("Searching for fix... (go outside)");
            } else {
                displayManager.setTextColor(TFT_GREEN);
                displayManager.println("FIX OK");

                displayManager.setTextColor(TFT_WHITE);
                snprintf(buf, sizeof(buf), "Lat:  %+.6f", (double)gm.lat());
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(buf);

                snprintf(buf, sizeof(buf), "Lon:  %+.6f", (double)gm.lon());
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(buf);

                displayManager.setTextColor(TFT_YELLOW);
                snprintf(buf, sizeof(buf), "Maps: %.6f,%.6f", (double)gm.lat(), (double)gm.lon());
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(buf);

                if (gm.timeValid()) {
                    displayManager.setTextColor(TFT_WHITE);
                    snprintf(buf, sizeof(buf), "UTC:  %02d:%02d:%02d",
                             (int)gm.hour(), (int)gm.minute(), (int)gm.second());
                    displayManager.setCursor(4, displayManager.getCursorY());
                    displayManager.println(buf);
                }
            }

            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("[q] back to CLI  (GPS stays on)");
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    displayManager.printCommandScreen();
}

void runGpsOff() {
    GpsManager& gm = GpsManager::instance();
    if (gm.isRunning()) {
        displayManager.println("Stopping GPS background task...");
        gm.stop();
        displayManager.println("GPS stopped.");
    } else {
        displayManager.println("GPS background task not running.");
    }
}

// ── NVS warm-start helpers ────────────────────────────────────────────────────
bool GpsManager::loadGpsNvsFlag() {
    Preferences prefs;
    prefs.begin("gps", true);
    bool v = prefs.getBool("fix", false);
    if (v) {
        _nvsLat = prefs.getInt("lat", 0);
        _nvsLon = prefs.getInt("lon", 0);
    }
    prefs.end();
    return v;
}

void GpsManager::saveGpsFixFlag() {
    Preferences prefs;
    prefs.begin("gps", false);
    prefs.putBool("fix", true);
    prefs.putInt("lat", (int32_t)(_lat * 1.0e7f));
    prefs.putInt("lon", (int32_t)(_lon * 1.0e7f));
    prefs.end();
}

// Computes UBX Fletcher-8 checksum and sends a complete UBX frame.
static void sendUbxMsg(HardwareSerial* s, uint8_t cls, uint8_t id,
                       const uint8_t* payload, uint16_t plen) {
    uint8_t cka = 0, ckb = 0;
    s->write(0xB5); s->write(0x62);
    uint8_t hdr[4] = { cls, id, (uint8_t)(plen & 0xFF), (uint8_t)(plen >> 8) };
    for (int i = 0; i < 4; i++) { s->write(hdr[i]); cka += hdr[i]; ckb += cka; }
    for (uint16_t i = 0; i < plen; i++) { s->write(payload[i]); cka += payload[i]; ckb += cka; }
    s->write(cka); s->write(ckb);
}

// Inject last known position then do a GNSS-only hot start from BBR.
// Position uses UBX-MGA-INI-POS_LLH (0x13/0x40) — supported on M8/M9/M10.
// UBX-CFG-RST resetMode=0x09 restarts only the GNSS engine (no CPU reset).
void GpsManager::applyUbloxHotStart() {
    if (!_nvsCachedFix) return;

    if (_nvsLat != 0 || _nvsLon != 0) {
        uint8_t mga[20] = {};
        mga[0] = 0x01;                           // sub-type: LLH position
        int32_t posAcc = 5000000;                // 50 km in cm — loose hint
        memcpy(mga + 4,  &_nvsLat, 4);
        memcpy(mga + 8,  &_nvsLon, 4);
        // mga[12..15] alt = 0 cm (unknown, but posAcc is large so the module ignores it)
        memcpy(mga + 16, &posAcc, 4);
        sendUbxMsg(_serial, 0x13, 0x40, mga, sizeof(mga));
        delay(200);
    }

    // UBX-CFG-RST: navBbrMask=0x0000 (hot start from BBR), resetMode=0x09
    // Checksum pre-computed for {cls=0x06,id=0x04,len=4,payload={0,0,9,0}}: 0x17 0x76
    static const uint8_t HOTRST[] = {
        0xB5, 0x62, 0x06, 0x04, 0x04, 0x00,
        0x00, 0x00, 0x09, 0x00,
        0x17, 0x76
    };
    _serial->write(HOTRST, sizeof(HOTRST));
    delay(600);
}

#else  // non-Plus stub

void runGpsOn()  { displayManager.println("GPS only on T-Deck Plus."); displayManager.printCommandScreen(); }
void runGpsOff() { displayManager.println("GPS only on T-Deck Plus."); displayManager.printCommandScreen(); }

#endif // BOARD_TDECK_PLUS
