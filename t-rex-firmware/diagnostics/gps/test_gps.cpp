#include "display_manager.h"
#include "input_handling.h"
#include "utilities.h"
#include <Arduino.h>
#include <TinyGPS++.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

#ifdef BOARD_TDECK_PLUS

// ── L76K / u-blox helpers (adapted from LilyGo official example) ──────────────

static HardwareSerial* _gser = nullptr;

static int gpsGetAck(uint8_t reqClass, uint8_t reqID) {
    uint8_t  buf[32];
    uint16_t fc = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        while (_gser->available()) {
            int c = _gser->read();
            switch (fc) {
                case 0: fc = (c == 0xB5) ? 1 : 0; break;
                case 1: fc = (c == 0x62) ? 2 : 0; break;
                case 2: fc = (c == reqClass) ? 3 : 0; break;
                case 3: fc = (c == reqID)    ? 4 : 0; break;
                case 4: { // length lo
                    uint16_t need = c;
                    // wait for length hi
                    uint32_t tw = millis() + 100;
                    while (!_gser->available() && millis() < tw) {}
                    need |= (_gser->read() << 8);
                    if (need == 0 || need > sizeof(buf)) { fc = 0; break; }
                    if ((int)_gser->readBytes(buf, need) == need) return (int)need;
                    fc = 0; break;
                }
                default: fc = 0; break;
            }
        }
    }
    return 0;
}

// Try to init L76K GNSS module. Returns true if found.
static bool initL76K() {
    for (int attempt = 0; attempt < 3; attempt++) {
        // Stop all NMEA sentences
        _gser->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
        delay(100);
        while (_gser->available()) _gser->read(); // flush
        delay(100);

        // Ask for version
        _gser->write("$PCAS06,0*1B\r\n");
        uint32_t t0 = millis();
        while (!_gser->available() && millis() - t0 < 600) {}
        _gser->setTimeout(200);
        String ver = _gser->readStringUntil('\n');
        if (ver.startsWith("$GPTXT,01,01,02")) {
            // GPS + GLONASS mode
            _gser->write("$PCAS04,5*1C\r\n"); delay(250);
            // Re-enable standard NMEA sentences
            _gser->write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n"); delay(250);
            // Vehicle mode
            _gser->write("$PCAS11,3*1E\r\n"); delay(100);
            return true;
        }
        delay(500);
    }
    return false;
}

// Try u-blox M10Q recovery at 38400, fall back to 9600. Returns true if acked.
static bool recoverUblox() {
    static const uint8_t CFG1[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0xFF,0xFF,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x1C,0xA2};
    static const uint8_t CFG2[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0xFF,0xFF,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x1B,0xA1};
    static const uint8_t CFG3[] = {0xB5,0x62,0x06,0x09,0x0D,0x00,0x00,0x00,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x03,0x1D,0xB3};
    static const uint8_t RATE[] = {0xB5,0x62,0x06,0x08,0x00,0x00,0x0E,0x30};
    _gser->write(CFG1, sizeof(CFG1)); gpsGetAck(0x05, 0x01);
    _gser->write(CFG2, sizeof(CFG2)); gpsGetAck(0x05, 0x01);
    _gser->write(CFG3, sizeof(CFG3)); gpsGetAck(0x05, 0x01);
    _gser->write(RATE, sizeof(RATE));
    return gpsGetAck(0x06, 0x08) > 0;
}

// ── entry point ───────────────────────────────────────────────────────────────
void runGpsTest() {
    TinyGPSPlus gps;
    HardwareSerial gpsSerial(1);
    _gser = &gpsSerial;

    // ── status display while init runs ───
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("GPS TEST — initialising...");
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("Trying L76K at 9600 baud");

    // ── init sequence (matches official LilyGo example) ─────────────────────
    gpsSerial.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);

    char moduleLabel[24] = "Unknown";
    bool initOk = initL76K();
    if (initOk) {
        strncpy(moduleLabel, "L76K (GPS+GLONASS)", 23);
    } else {
        // Try u-blox M10Q at 38400
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Trying u-blox at 38400 baud");
        gpsSerial.begin(38400, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
        if (recoverUblox()) {
            strncpy(moduleLabel, "u-blox M10Q", 23);
            initOk = true;
        } else {
            // Last chance: u-blox at 9600
            gpsSerial.updateBaudRate(9600);
            if (recoverUblox()) {
                strncpy(moduleLabel, "u-blox M10Q @9600", 23);
                initOk = true;
            }
        }
    }

    uint32_t lastDraw = 0;

    while (true) {
        // Feed TinyGPS++
        while (gpsSerial.available()) {
            gps.encode((char)gpsSerial.read());
        }

        if (millis() - lastDraw >= 1000) {
            lastDraw = millis();
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();

            // Header
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_CYAN);
            displayManager.println("GPS TEST");

            // Module
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(initOk ? TFT_GREEN : TFT_RED);
            char modLine[36];
            snprintf(modLine, sizeof(modLine), "Module: %s", moduleLabel);
            displayManager.println(modLine);

            // Chars processed — key diagnostic (0 = no data from module)
            displayManager.setCursor(4, displayManager.getCursorY());
            uint32_t chars = gps.charsProcessed();
            displayManager.setTextColor(chars > 0 ? TFT_GREEN : TFT_RED);
            char diagLine[36];
            snprintf(diagLine, sizeof(diagLine), "Chars: %lu   Sats: %lu",
                     (unsigned long)chars,
                     gps.satellites.isValid() ? (unsigned long)gps.satellites.value() : 0UL);
            displayManager.println(diagLine);

            // Fix / location
            displayManager.setCursor(4, displayManager.getCursorY());
            if (chars == 0) {
                displayManager.setTextColor(TFT_RED);
                displayManager.println("No data — module not responding");
            } else if (!gps.location.isValid()) {
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("Searching for fix... (go outside)");
            } else {
                displayManager.setTextColor(TFT_GREEN);
                displayManager.println("FIX OK");

                char buf[52];
                displayManager.setTextColor(TFT_WHITE);

                snprintf(buf, sizeof(buf), "Lat:  %+.6f", gps.location.lat());
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(buf);

                snprintf(buf, sizeof(buf), "Lon:  %+.6f", gps.location.lng());
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.println(buf);

                // Paste directly into Google Maps search bar
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(TFT_YELLOW);
                snprintf(buf, sizeof(buf), "Maps: %.6f,%.6f",
                         gps.location.lat(), gps.location.lng());
                displayManager.println(buf);

                displayManager.setTextColor(TFT_WHITE);
                if (gps.speed.isValid()) {
                    snprintf(buf, sizeof(buf), "Spd: %.1f km/h   Alt: %.0f m",
                             gps.speed.kmph(),
                             gps.altitude.isValid() ? gps.altitude.meters() : 0.0);
                    displayManager.setCursor(4, displayManager.getCursorY());
                    displayManager.println(buf);
                }

                if (gps.date.isValid() && gps.time.isValid()) {
                    snprintf(buf, sizeof(buf), "UTC: %02d:%02d:%02d  %02d/%02d/%04d",
                             gps.time.hour(), gps.time.minute(), gps.time.second(),
                             gps.date.day(), gps.date.month(), gps.date.year());
                    displayManager.setCursor(4, displayManager.getCursorY());
                    displayManager.println(buf);
                }
            }

            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("[q] quit");
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }

    _gser = nullptr;
    gpsSerial.end();
    displayManager.printCommandScreen();
}

#else

void runGpsTest() {
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_RED);
    displayManager.println("GPS only on T-Deck Plus.");
    displayManager.println("Build: env:T-Deck-Plus");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
}

#endif
