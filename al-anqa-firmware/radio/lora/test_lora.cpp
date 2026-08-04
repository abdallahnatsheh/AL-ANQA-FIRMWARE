// LoRa diagnostic — SX1262 on the shared T-Deck SPI bus
// CS=9 RST=17 DIO1=45 BUSY=13  SPI: SCK=40 MISO=38 MOSI=41

#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "utilities.h"
#include <RadioLib.h>
#include <SPI.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

static const float   FREQS[]  = { 868.0f, 915.0f, 433.0f };
static const uint8_t N_FREQS  = 3;
static const float   BW       = 125.0f;
static const uint8_t SF       = 9;
static const uint8_t CR       = 5;    // 4/5
static const int8_t  TX_PWR   = 17;   // dBm
static const uint8_t SYNC_W   = 0x12; // private LoRa network

// ── Display helpers ───────────────────────────────────────────────────────────

static void lRow(const char* lbl, const char* val, uint16_t col = TFT_WHITE) {
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.printText(lbl);
    displayManager.setTextColor(col);
    displayManager.println(val);
}

static void lSep() {
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("-------------------------------");
}

// ── Info screen ───────────────────────────────────────────────────────────────

static void drawInfo(bool ok, int16_t errCode, float freq,
                     int txPkts, int rxPkts,
                     float lastRssi, float lastSnr, bool hasRx) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(10, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("LoRa Test");
    lSep();

    char buf[48];

    if (ok) {
        lRow("Radio  ", "SX1262  OK", TFT_GREEN);
    } else {
        snprintf(buf, sizeof(buf), "SX1262  ERR(%d)", errCode);
        lRow("Radio  ", buf, TFT_RED);
    }

    snprintf(buf, sizeof(buf), "%.1f MHz", (double)freq);
    lRow("Freq   ", buf);

    snprintf(buf, sizeof(buf), "%u  /  %.0f kHz", SF, (double)BW);
    lRow("SF/BW  ", buf);

    snprintf(buf, sizeof(buf), "4/%u  /  %d dBm", CR, TX_PWR);
    lRow("CR/Pwr ", buf);

    snprintf(buf, sizeof(buf), "0x%02X  (private)", SYNC_W);
    lRow("Sync   ", buf);

    lSep();

    snprintf(buf, sizeof(buf), "%d tx   %d rx", txPkts, rxPkts);
    lRow("Pkts   ", buf);

    if (hasRx) {
        uint16_t rc = lastRssi >= -80 ? TFT_GREEN : (lastRssi >= -100 ? TFT_YELLOW : TFT_RED);
        snprintf(buf, sizeof(buf), "%.0f dBm   SNR:%.1f", (double)lastRssi, (double)lastSnr);
        lRow("Last RX", buf, rc);
    } else {
        lRow("Last RX", "---", 0x7BEF);
    }

    lSep();

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(TFT_WHITE);
    dm.println("[t]tx  [r]rx  [f]freq  [q]quit");
}

// ── RX mode ───────────────────────────────────────────────────────────────────

struct PktEntry { float rssi; float snr; char data[24]; };

static void drawRxScreen(float freq, PktEntry* pkts, int total) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    char hdr[40];
    snprintf(hdr, sizeof(hdr), "LoRa RX  %.1f MHz", (double)freq);
    dm.setCursor(10, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println(hdr);
    lSep();

    if (total == 0) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        dm.println("Listening...");
    }

    // Show up to last 5 packets (circular in pkts[5])
    int show     = total > 5 ? 5 : total;
    int startIdx = total > 5 ? total - 5 : 0;
    for (int i = startIdx; i < total; i++) {
        PktEntry& p = pkts[i % 5];
        char buf[40];
        uint16_t rc = p.rssi >= -80 ? TFT_GREEN : (p.rssi >= -100 ? TFT_YELLOW : TFT_RED);
        snprintf(buf, sizeof(buf), "#%d RSSI:%.0f SNR:%.1f", i + 1, (double)p.rssi, (double)p.snr);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(rc);
        dm.println(buf);
        snprintf(buf, sizeof(buf), " \"%.22s\"", p.data);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);
        dm.println(buf);
    }

    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("[q]stop rx");
}

static void runRxMode(SX1262& radio, float freq, int& rxPkts,
                      float& lastRssi, float& lastSnr, bool& hasRx) {
    PktEntry pkts[5] = {};
    int total = 0;
    bool redraw = true;

    if (radio.startReceive() != RADIOLIB_ERR_NONE) {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("RX start failed");
        delay(1500);
        return;
    }

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            redraw = true;
        }

        if (redraw) { drawRxScreen(freq, pkts, total); redraw = false; }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (radio.available()) {
            String recv;
            if (radio.readData(recv) == RADIOLIB_ERR_NONE) {
                float rssi = radio.getRSSI();
                float snr  = radio.getSNR();

                PktEntry& slot = pkts[total % 5];
                slot.rssi = rssi;
                slot.snr  = snr;
                strncpy(slot.data, recv.c_str(), 23);
                slot.data[23] = '\0';

                rxPkts++;
                total++;
                lastRssi = rssi;
                lastSnr  = snr;
                hasRx    = true;
                redraw   = true;

                radio.startReceive();
            }
        }
    }

    radio.standby();
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runLoraTest() {
    DisplayManager& dm = displayManager;

    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(10, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.println("LoRa Test");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.println("Init SX1262...");

    // Ensure all SPI CS lines are HIGH before touching the bus — prevents
    // contention between the radio, SD, and TFT during SPI re-initialisation.
    // TFT and SD CS are already managed by their drivers; radio CS may not be.
    pinMode(RADIO_CS_PIN,   OUTPUT); digitalWrite(RADIO_CS_PIN,   HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT); digitalWrite(BOARD_SDCARD_CS, HIGH);
    pinMode(BOARD_TFT_CS,   OUTPUT); digitalWrite(BOARD_TFT_CS,   HIGH);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    SX1262 radio(new Module(RADIO_CS_PIN, RADIO_DIO1_PIN,
                            RADIO_RST_PIN, RADIO_BUSY_PIN));

    uint8_t freqIdx = 0;
    float   freq    = FREQS[freqIdx];
    int16_t state   = radio.begin(freq, BW, SF, CR, SYNC_W, TX_PWR);

    bool ok       = (state == RADIOLIB_ERR_NONE);
    int  txPkts   = 0;
    int  rxPkts   = 0;
    float lastRssi = 0, lastSnr = 0;
    bool  hasRx   = false;
    bool  redraw  = true;
    int   txSeq   = 0;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            redraw = true;
        }

        if (redraw) {
            drawInfo(ok, state, freq, txPkts, rxPkts, lastRssi, lastSnr, hasRx);
            redraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (!k) continue;

        if (k == 'q' || k == 'Q') break;

        if ((k == 'f' || k == 'F') && ok) {
            freqIdx = (freqIdx + 1) % N_FREQS;
            freq    = FREQS[freqIdx];
            radio.setFrequency(freq);
            redraw = true;
        }

        if ((k == 't' || k == 'T') && ok) {
            dm.clearScreen();
            dm.setDefaultTextSize();
            dm.setCursor(10, outputY);
            dm.setTextColor(TFT_CYAN);
            dm.println("Transmitting...");

            char pkt[24];
            snprintf(pkt, sizeof(pkt), "T-DECK #%d", ++txSeq);
            dm.setCursor(10, dm.getCursorY());
            dm.setTextColor(TFT_WHITE);
            char msg[32];
            snprintf(msg, sizeof(msg), "\"%s\"", pkt);
            dm.println(msg);

            state = radio.transmit(pkt);
            dm.setCursor(10, dm.getCursorY());
            if (state == RADIOLIB_ERR_NONE) {
                txPkts++;
                dm.setTextColor(TFT_GREEN);
                dm.println("TX OK");
            } else {
                dm.setTextColor(TFT_RED);
                char errbuf[24];
                snprintf(errbuf, sizeof(errbuf), "TX failed: %d", state);
                dm.println(errbuf);
            }
            delay(1200);
            redraw = true;
        }

        if ((k == 'r' || k == 'R') && ok) {
            runRxMode(radio, freq, rxPkts, lastRssi, lastSnr, hasRx);
            redraw = true;
        }
    }

    radio.sleep();
    dm.printCommandScreen();
}
