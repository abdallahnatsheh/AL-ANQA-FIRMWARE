#include "trackme.h"
#include "input_handling.h"
#include "utilities.h"
#include <WiFi.h>
#include <SD.h>
#include <math.h>
#include <driver/i2s.h>

#define TM_I2S_PORT   I2S_NUM_0
#define TM_SAMPLE_RATE 22050

extern InputHandling inputHandler;

// ── static ring buffer (written from WiFi ISR, read from main task) ──────────
volatile TrackMeScanner::ProbeEntry TrackMeScanner::_ring[TM_PROBE_RING];
volatile uint8_t TrackMeScanner::_rHead = 0;
volatile uint8_t TrackMeScanner::_rTail = 0;

// ── hardcoded fallback signatures ────────────────────────────────────────────
static void fillBuiltinSigs(TrackerSig* sigs, int& count) {
    count = 0;
    // payloadByte: required mfr[2] in BLE advertisement; 0x00 = any
    // Apple 0x004C is shared by ALL Apple devices — require 0x12 (Offline Finding / Find My)
    // to distinguish AirTag / separated AirPods from iPhones and Macs.
    const struct { const char* name; uint16_t cid; uint8_t pb; } entries[] = {
        { "Apple AirTag",     0x004C, 0x12 },
        { "Tile Tracker",     0x00D7, 0x00 },
        { "Samsung SmartTag", 0x0075, 0x00 },
        { "Chipolo",          0x00F0, 0x00 },
        { "Google FindMy",    0x00E0, 0x00 },
    };
    for (auto& e : entries) {
        if (count >= TM_SIG_MAX) break;
        strncpy(sigs[count].name, e.name, 23);
        sigs[count].name[23]    = '\0';
        sigs[count].companyId   = e.cid;
        sigs[count].payloadByte = e.pb;
        sigs[count].level       = THREAT_WARNING;
        count++;
    }
}

// ── constructor ───────────────────────────────────────────────────────────────
TrackMeScanner::TrackMeScanner(DisplayManager& dm, SDCardManager& sd)
    : dm(dm), sd(sd), sigCount(0),
      tier1Count(0), tier2Count(0),
      page(0), startMs(0), _i2sReady(false)
{
#ifdef BOARD_TDECK_PLUS
    gpsLat = 0; gpsLon = 0; gpsValid = false;
    gpsSerial = nullptr; gpsBufLen = 0;
#endif
}

// ── helpers ───────────────────────────────────────────────────────────────────
String TrackMeScanner::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

bool TrackMeScanner::macEq(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

// ── 1-D Kalman (process noise Q=1, measurement noise R=4) ────────────────────
float TrackMeScanner::kalmanUpdate(KState& k, float z) {
    const float Q = 1.0f, R = 4.0f;
    k.P += Q;
    float gain = k.P / (k.P + R);
    k.x = k.x + gain * (z - k.x);
    k.P = (1.0f - gain) * k.P;
    return k.x;
}

float TrackMeScanner::calcVariance(const TrackedDev& d) {
    if (d.rssiCount < 2) return 999.0f;
    float sum = 0;
    for (int i = 0; i < d.rssiCount; i++) sum += d.rssiHistory[i];
    float mean = sum / d.rssiCount;
    float var  = 0;
    for (int i = 0; i < d.rssiCount; i++) {
        float diff = d.rssiHistory[i] - mean;
        var += diff * diff;
    }
    return var / d.rssiCount;
}

// ── signature matching ────────────────────────────────────────────────────────
// mfrType = mfr[2] from BLE advertisement; 0x00 means "not available / any"
int TrackMeScanner::matchSig(uint16_t companyId, uint8_t mfrType) {
    if (companyId == 0) return -1;
    for (int i = 0; i < sigCount; i++) {
        if (sigs[i].companyId != companyId) continue;
        // If the signature requires a specific payload byte, enforce it.
        // If mfrType == 0x00 (not available) we skip the check — better a
        // false positive than a miss when payload couldn't be read.
        if (sigs[i].payloadByte != 0x00 && mfrType != 0x00 &&
            mfrType != sigs[i].payloadByte) continue;
        return i;
    }
    return -1;
}

// ── signature loading ─────────────────────────────────────────────────────────
void TrackMeScanner::loadSignatures() {
    sigCount = 0;

    if (sd.isReady() && SD.exists("/signatures.csv")) {
        File f = SD.open("/signatures.csv", FILE_READ);
        if (f) {
            while (f.available() && sigCount < TM_SIG_MAX) {
                String line = f.readStringUntil('\n');
                line.trim();
                if (line.length() == 0 || line[0] == '#') continue;
                // format: BLE,0x004C,Apple AirTag,HIGH
                int f1 = line.indexOf(',');
                int f2 = line.indexOf(',', f1 + 1);
                int f3 = line.indexOf(',', f2 + 1);
                if (f1 < 0 || f2 < 0 || f3 < 0) continue;

                String cidStr = line.substring(f1 + 1, f2);
                String name   = line.substring(f2 + 1, f3);
                cidStr.trim(); name.trim();

                uint16_t cid = (uint16_t)strtoul(cidStr.c_str(), nullptr, 16);
                strncpy(sigs[sigCount].name, name.c_str(), 23);
                sigs[sigCount].name[23]    = '\0';
                sigs[sigCount].companyId   = cid;
                sigs[sigCount].payloadByte = 0x00; // CSV doesn't specify payload byte
                sigs[sigCount].level       = THREAT_WARNING;
                sigCount++;
            }
            f.close();
            if (sigCount > 0) return;
        }
    }

    fillBuiltinSigs(sigs, sigCount);
}

// ── device init helper ────────────────────────────────────────────────────────
void TrackMeScanner::initDev(TrackedDev& d, const uint8_t* mac, const char* name,
                              uint16_t companyId, int8_t rssi, bool isWiFi, uint8_t t,
                              uint8_t mfrType, uint8_t crowd) {
    memset(&d, 0, sizeof(TrackedDev));
    memcpy(d.mac, mac, 6);
    strncpy(d.name, name, 27); d.name[27] = '\0';
    d.companyId      = companyId;
    d.firstSeen      = millis();
    d.lastSeen       = d.firstSeen;
    d.sightings      = 1;
    d.rssiSmoothed   = (float)rssi;
    d.rssiHistory[0] = rssi;
    d.rssiIdx        = 1 % TM_RSSI_HISTORY;
    d.rssiCount      = 1;
    d.sigIdx         = matchSig(companyId, mfrType);
    d.isKnown        = (d.sigIdx >= 0);
    d.tier           = t;
    d.isWiFi         = isWiFi;
    d.crowdAtArrival = crowd;
}

// ── process one detected device ───────────────────────────────────────────────
void TrackMeScanner::processDevice(const uint8_t* mac, const char* name,
                                    uint16_t companyId, uint8_t mfrType,
                                    int8_t rssi, bool isWiFi) {
    // Locate in tier1 or tier2
    TrackedDev* existing = nullptr;
    KState*     ks       = nullptr;
    int         t2idx    = -1;

    for (int i = 0; i < tier1Count; i++) {
        if (macEq(tier1[i].mac, mac)) { existing = &tier1[i]; ks = &k1[i]; break; }
    }
    if (!existing) {
        for (int i = 0; i < tier2Count; i++) {
            if (macEq(tier2[i].mac, mac)) {
                existing = &tier2[i]; ks = &k2[i]; t2idx = i; break;
            }
        }
    }

    // Kalman update, or raw for first sighting
    float smooth = (existing && ks) ? kalmanUpdate(*ks, (float)rssi) : (float)rssi;

    // RSSI gate — drop anything too far
    if (smooth < -85.0f) return;

    if (existing) {
        existing->lastSeen     = millis();
        existing->sightings    = min((int)existing->sightings + 1, 65535);
        existing->rssiSmoothed = smooth;
        existing->rssiHistory[existing->rssiIdx] = rssi;
        existing->rssiIdx = (existing->rssiIdx + 1) % TM_RSSI_HISTORY;
        if (existing->rssiCount < TM_RSSI_HISTORY) existing->rssiCount++;

        if (name[0] != '\0' && existing->name[0] == '\0') {
            strncpy(existing->name, name, 27); existing->name[27] = '\0';
        }
        // If we now learn the company ID for an existing device, re-run sig match
        if (companyId != 0 && existing->companyId == 0) {
            existing->companyId = companyId;
            int idx = matchSig(companyId, mfrType);
            if (idx >= 0) { existing->sigIdx = idx; existing->isKnown = true; }
        }

        // Gap return
        if (existing->gapActive) {
            existing->gapActive   = false;
            existing->gapReturned = true;
            existing->distinctWindows++;
        }

        // Tier 2 → Tier 1 promotion
        if (t2idx >= 0) {
            bool promote = (smooth > -70.0f) || (existing->sightings >= 3);
            if (promote && tier1Count < TM_TIER1_MAX) {
                tier1[tier1Count]      = *existing;
                tier1[tier1Count].tier = 1;
                k1[tier1Count]         = *ks;
                tier1Count++;
                int last = tier2Count - 1;
                if (t2idx != last) { tier2[t2idx] = tier2[last]; k2[t2idx] = k2[last]; }
                tier2Count--;
            }
        }
    } else {
        KState newK = { smooth, 10.0f };
        if (smooth > -70.0f) {
            if (tier1Count < TM_TIER1_MAX) {
                // tier1Count before insertion = number of other devices present = crowd size
                initDev(tier1[tier1Count], mac, name, companyId, rssi, isWiFi, 1,
                        mfrType, (uint8_t)tier1Count);
                k1[tier1Count] = newK;
                tier1Count++;
            }
        } else {
            if (tier2Count < TM_TIER2_MAX) {
                initDev(tier2[tier2Count], mac, name, companyId, rssi, isWiFi, 2,
                        mfrType, (uint8_t)tier1Count);
                k2[tier2Count] = newK;
                tier2Count++;
            }
        }
    }
}

// ── gap marking (called after each scan cycle) ────────────────────────────────
void TrackMeScanner::markGaps(uint32_t cycleStart) {
    for (int i = 0; i < tier1Count; i++) {
        TrackedDev& d = tier1[i];
        if (d.lastSeen < cycleStart && !d.gapActive) {
            d.gapActive = true; d.gapStart = d.lastSeen;
        }
    }
    for (int i = 0; i < tier2Count; i++) {
        TrackedDev& d = tier2[i];
        if (d.lastSeen < cycleStart && !d.gapActive) {
            d.gapActive = true; d.gapStart = d.lastSeen;
        }
    }
}

// ── Gate 2: behaviour scoring ─────────────────────────────────────────────────
void TrackMeScanner::runGate2(TrackedDev& d) {
    uint32_t seenMs = millis() - d.firstSeen;
    int      score  = 0;

    if      (seenMs > 30ul * 60000) score += 25;
    else if (seenMs > 15ul * 60000) score += 15;
    else if (seenMs >  5ul * 60000) score += 10;

    if      (d.sightings >= 5) score += 20;
    else if (d.sightings >= 3) score += 15;

    float var = calcVariance(d);
    if      (var < 20.0f) score += 35; // very consistent (+20 low + +15 very)
    else if (var < 50.0f) score += 20;

    if (d.gapReturned) score += 25;

    d.score = score;
}

// ── Gate 3: confirmation ──────────────────────────────────────────────────────
bool TrackMeScanner::runGate3(const TrackedDev& d) {
    uint32_t seenMs = millis() - d.firstSeen;
    if (seenMs < 5ul * 60000)  return false;    // < 5 min
    if (d.distinctWindows < 3 && d.sightings < 3) return false;
    if (d.rssiSmoothed < -80.0f) return false;  // device fading away

    // Traffic-jam guard: device arrived in a crowd (4+ others present) and has
    // never disappeared then returned — it's just stuck in the same jam as you.
    // Require at least one gap-and-return to prove it followed you, not just
    // shared the same road for a while.
    if (d.crowdAtArrival >= 4 && d.distinctWindows == 0) return false;

    return true;
}

// ── full scoring pass on tier1 ────────────────────────────────────────────────
void TrackMeScanner::runScoring() {
    uint32_t now = millis();
    for (int i = 0; i < tier1Count; i++) {
        TrackedDev& d = tier1[i];
        uint32_t seenMs = now - d.firstSeen;

        if (d.isKnown) {
            if (runGate3(d)) {
                // Gate 3 confirmed: definite tracker following you
                d.alertLevel = THREAT_ALERT;
            } else if (seenMs >= 60000 && d.sightings >= 2 &&
                       !(d.crowdAtArrival >= 4 && d.distinctWindows == 0)) {
                // Seen for 60 s across 2+ scan cycles, and not just stuck in
                // the same traffic jam (crowd arrival with zero gap-returns)
                d.alertLevel = THREAT_NOTICE;
            } else {
                d.alertLevel = THREAT_NONE;
            }
        } else {
            runGate2(d);
            if (d.score >= 40) {
                if (runGate3(d))
                    d.alertLevel = (d.score >= 80) ? THREAT_ALERT : THREAT_WARNING;
                else
                    d.alertLevel = (d.score >= 60) ? THREAT_WARNING : THREAT_NOTICE;
            } else {
                d.alertLevel = THREAT_NONE;
            }
        }
    }
}

// ── WiFi promiscuous callback (runs in WiFi driver task) ──────────────────────
void IRAM_ATTR TrackMeScanner::wifiCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;
    const uint8_t* d = pkt->payload;
    if (((d[0] >> 4) & 0x0F) != 4) return; // probe requests only
    uint8_t next = (_rHead + 1) % TM_PROBE_RING;
    if (next == _rTail) return;
    ProbeEntry& slot = (ProbeEntry&)_ring[_rHead];
    memcpy(slot.mac, d + 10, 6);
    slot.rssi = pkt->rx_ctrl.rssi;
    _rHead = next;
}

// ── BLE scan (~2 s blocking) ──────────────────────────────────────────────────
void TrackMeScanner::doBLEScan(int seconds) {
    esp_wifi_set_promiscuous(false);
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    BLEScanResults results = scan->start(seconds, false);

    int n = results.getCount();
    for (int i = 0; i < n; i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        uint8_t  mac[6]    = {0};
        uint16_t companyId = 0;
        String   addr      = dev.getAddress().toString().c_str();
        sscanf(addr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        uint8_t mfrType = 0x00;
        if (dev.haveManufacturerData()) {
            std::string mfr = dev.getManufacturerData();
            if (mfr.size() >= 2)
                companyId = (uint8_t)mfr[0] | ((uint16_t)(uint8_t)mfr[1] << 8);
            if (mfr.size() >= 3)
                mfrType = (uint8_t)mfr[2]; // advertisement type byte
        }
        String name = dev.getName().c_str();
        processDevice(mac, name.c_str(), companyId, mfrType, dev.getRSSI(), false);
    }
    scan->clearResults();
}

// ── WiFi probe sniff (500 ms, non-blocking loop) ──────────────────────────────
void TrackMeScanner::doWiFiSniff(uint32_t durationMs) {
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(50);
    }
    wifi_promiscuous_filter_t flt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(wifiCb);
    esp_wifi_set_promiscuous(true);

    uint32_t t0 = millis();
    while (millis() - t0 < durationMs) {
        while (_rTail != _rHead) {
            ProbeEntry pe;
            memcpy(&pe, (const void*)&_ring[_rTail], sizeof(ProbeEntry));
            _rTail = (_rTail + 1) % TM_PROBE_RING;
            processDevice(pe.mac, "", 0, 0x00, pe.rssi, true);
        }
        delay(10);
    }
    esp_wifi_set_promiscuous(false);
}

// ── I2S audio (same driver approach as test_speaker.cpp) ─────────────────────
void TrackMeScanner::startI2S() {
    if (_i2sReady) return;

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = TM_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 128;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;

    if (i2s_driver_install(TM_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = BOARD_I2S_BCK;
    pins.ws_io_num    = BOARD_I2S_WS;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(TM_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(TM_I2S_PORT);
        return;
    }
    i2s_zero_dma_buffer(TM_I2S_PORT);
    _i2sReady = true;
}

void TrackMeScanner::stopI2S() {
    if (!_i2sReady) return;
    i2s_zero_dma_buffer(TM_I2S_PORT);
    i2s_driver_uninstall(TM_I2S_PORT);
    _i2sReady = false;
}

void TrackMeScanner::playTone(int freq, int durationMs) {
    if (!_i2sReady) return;
    int cycLen = TM_SAMPLE_RATE / freq;
    if (cycLen > 256) cycLen = 256;
    int16_t buf[512]; // 256 stereo samples max
    for (int i = 0; i < cycLen; i++) {
        int16_t v = (int16_t)(22000 * sinf(2.0f * M_PI * i / cycLen));
        buf[i * 2]     = v;
        buf[i * 2 + 1] = v;
    }
    size_t written;
    size_t blockBytes = (size_t)cycLen * 4;
    uint32_t t0 = millis();
    while ((int32_t)(millis() - t0) < durationMs) {
        i2s_write(TM_I2S_PORT, buf, blockBytes, &written, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(TM_I2S_PORT);
}

void TrackMeScanner::beep(ThreatLevel lvl) {
    if (!_i2sReady) return;
    if (lvl == THREAT_WARNING) {
        playTone(1000, 200);
    } else if (lvl == THREAT_ALERT) {
        for (int i = 0; i < 3; i++) {
            playTone(2000, 200);
            delay(200);
        }
    }
}

// ── SD logging ────────────────────────────────────────────────────────────────
void TrackMeScanner::appendLog(const TrackedDev& d) {
    if (!sd.isReady()) return;
    sd.ensureDir("/logs");
    const char* levelStr;
    switch (d.alertLevel) {
        case THREAT_ALERT:   levelStr = "ALERT";   break;
        case THREAT_WARNING: levelStr = "WARNING"; break;
        case THREAT_NOTICE:  levelStr = "NOTICE";  break;
        default:             levelStr = "NONE";    break;
    }
    char line[128];
    String mac = macStr(d.mac);
    snprintf(line, sizeof(line),
        "[%lums] %s | %s | 0x%04X | score:%d | seen:%lus | rssi:%.0f",
        (unsigned long)millis(), levelStr,
        d.name[0] ? d.name : mac.c_str(),
        d.companyId, d.score,
        (unsigned long)((millis() - d.firstSeen) / 1000),
        d.rssiSmoothed);
    sd.appendLine(SD_LOG_TRACKME, String(line));
}

void TrackMeScanner::saveLog() {
    for (int i = 0; i < tier1Count; i++) appendLog(tier1[i]);
    for (int i = 0; i < tier2Count; i++) appendLog(tier2[i]);
}

// ── custom red header ─────────────────────────────────────────────────────────
void TrackMeScanner::drawHeader() {
    dm.fillRect(0, promptY, SCREEN_WIDTH, promptHeight, TFT_RED);
    dm.printText("T-REX // TRACK ME", 5, promptY + 9, TFT_WHITE);
    dm.printText("[q]quit",          250, promptY + 9, TFT_WHITE);
}

// ── full screen redraw ────────────────────────────────────────────────────────
void TrackMeScanner::drawScreen(ThreatLevel highestLevel,
                                 const char* alertName, uint32_t alertSec) {
    const int RPP = 6; // rows per page
    int totalPages = (tier1Count + RPP - 1) / RPP;
    if (totalPages < 1) totalPages = 1;
    if (page >= totalPages) page = totalPages - 1;
    if (page < 0) page = 0;

    dm.clearScreen();
    drawHeader();
    dm.setDefaultTextSize();

    // Status line
    uint32_t elapsed = (millis() - startMs) / 1000;
    char status[48];
    snprintf(status, sizeof(status), "BLE+WiFi t:%02lu:%02lu  T1:%d T2:%d  [%d/%d]",
             (unsigned long)(elapsed / 60), (unsigned long)(elapsed % 60),
             tier1Count, tier2Count, page + 1, totalPages);
    dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN);
    dm.printText(status);

    // Table header
    dm.setCursor(4, outputY + LINE_HEIGHT);
    dm.setTextColor(0x7BEF);
    dm.printText("#  TYPE        SCORE  SEEN    RSSI");

    // Device rows
    int start = page * RPP;
    int end   = min(start + RPP, tier1Count);
    for (int i = start; i < end; i++) {
        const TrackedDev& d = tier1[i];
        uint16_t color;
        switch (d.alertLevel) {
            case THREAT_ALERT:   color = TFT_RED;    break;
            case THREAT_WARNING: color = TFT_ORANGE; break;
            case THREAT_NOTICE:  color = TFT_YELLOW; break;
            default:             color = TFT_WHITE;  break;
        }
        uint32_t seenMs = millis() - d.firstSeen;
        char typeBuf[11];
        if (d.isKnown && d.sigIdx >= 0)
            snprintf(typeBuf, sizeof(typeBuf), "%.10s", sigs[d.sigIdx].name);
        else if (d.isWiFi)
            strcpy(typeBuf, "WiFi?");
        else
            strcpy(typeBuf, "Unknown");

        char row[44];
        snprintf(row, sizeof(row), "%-2d %-10s %4d %3lu:%02lu %5.0f",
                 i + 1, typeBuf, d.score,
                 (unsigned long)(seenMs / 60000),
                 (unsigned long)((seenMs % 60000) / 1000),
                 d.rssiSmoothed);

        dm.setCursor(4, outputY + LINE_HEIGHT * (2 + (i - start)));
        dm.setTextColor(color);
        dm.printText(row);
    }

    // Controls
    dm.setCursor(4, outputY + LINE_HEIGHT * 9);
    dm.setTextColor(TFT_GREEN);  dm.printText("a");
    dm.setTextColor(TFT_WHITE);  dm.printText("=prev ");
    dm.setTextColor(TFT_GREEN);  dm.printText("l");
    dm.setTextColor(TFT_WHITE);  dm.printText("=next ");
    dm.setTextColor(TFT_GREEN);  dm.printText("c");
    dm.setTextColor(TFT_WHITE);  dm.printText("=clr ");
    dm.setTextColor(TFT_GREEN);  dm.printText("s");
    dm.setTextColor(TFT_WHITE);  dm.printText("=save ");
    dm.setTextColor(TFT_GREEN);  dm.printText("q");
    dm.setTextColor(TFT_WHITE);  dm.printText("=quit");

    // Alert bar
    int      alertY  = outputY + LINE_HEIGHT * 10;
    uint16_t alertBg, alertFg;
    char     alertText[48];

    if (highestLevel == THREAT_ALERT) {
        alertBg = TFT_RED; alertFg = TFT_WHITE;
        snprintf(alertText, sizeof(alertText), "[!] ALERT: %s  %lu:%02lu",
                 alertName, (unsigned long)(alertSec / 60),
                 (unsigned long)(alertSec % 60));
    } else if (highestLevel == THREAT_WARNING) {
        alertBg = TFT_ORANGE; alertFg = TFT_BLACK;
        snprintf(alertText, sizeof(alertText), "[!] WARNING: %.28s", alertName);
    } else if (highestLevel == THREAT_NOTICE) {
        alertBg = TFT_BLACK; alertFg = TFT_YELLOW;
        snprintf(alertText, sizeof(alertText), "[ ] NOTICE: %.29s", alertName);
    } else {
        alertBg = TFT_BLACK; alertFg = 0x7BEF;
        strcpy(alertText, "    No threats detected");
    }

    dm.fillRect(0, alertY, SCREEN_WIDTH, LINE_HEIGHT + 2, alertBg);
    dm.printText(alertText, 4, alertY + 1, alertFg);
}

// ── GPS (T-Deck Plus only) ────────────────────────────────────────────────────
#ifdef BOARD_TDECK_PLUS
void TrackMeScanner::readGPS() {
    if (!gpsSerial) return;
    while (gpsSerial->available()) {
        char c = gpsSerial->read();
        if (c == '\n' || c == '\r') {
            if (gpsBufLen > 6 && strncmp(gpsBuf, "$GPRMC", 6) == 0) {
                char   status = 'V';
                float  lat = 0, lon = 0;
                // $GPRMC,HHMMSS.ss,A,DDMM.mmmm,N,DDDMM.mmmm,E,...
                if (sscanf(gpsBuf, "$GPRMC,%*f,%c,%f,%*c,%f,%*c",
                           &status, &lat, &lon) == 3 && status == 'A') {
                    float latDeg = (int)(lat / 100);
                    float lonDeg = (int)(lon / 100);
                    gpsLat  = latDeg + (lat - latDeg * 100.0f) / 60.0f;
                    gpsLon  = lonDeg + (lon - lonDeg * 100.0f) / 60.0f;
                    gpsValid = true;
                }
            }
            gpsBufLen = 0;
        } else if (gpsBufLen < (int)sizeof(gpsBuf) - 1) {
            gpsBuf[gpsBufLen++] = c;
            gpsBuf[gpsBufLen]   = '\0';
        }
    }
}
#endif

// ── entry point ───────────────────────────────────────────────────────────────
void TrackMeScanner::start() {
    loadSignatures();

    memset(tier1, 0, sizeof(tier1)); tier1Count = 0;
    memset(tier2, 0, sizeof(tier2)); tier2Count = 0;
    memset(k1, 0, sizeof(k1));
    memset(k2, 0, sizeof(k2));
    _rHead = 0; _rTail = 0;
    page    = 0;
    startMs = millis();

    BLEDevice::init("");
    dm.setBtActive(true);
    startI2S();

#ifdef BOARD_TDECK_PLUS
    gpsSerial = new HardwareSerial(1);
    gpsSerial->begin(BOARD_GPS_BAUD, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    gpsValid = false; gpsBufLen = 0;
#endif

    drawHeader();
    drawScreen(THREAT_NONE, "", 0);

    ThreatLevel highestLevel = THREAT_NONE;
    char        alertName[28] = {0};
    uint32_t    alertSec     = 0;
    uint32_t    lastBeepMs   = 0;
    bool        alertActive  = false;

    while (true) {
        uint32_t cycleStart = millis();

        doBLEScan(2);        // ~2 s
        doWiFiSniff(500);    // ~0.5 s

#ifdef BOARD_TDECK_PLUS
        readGPS();
#endif

        markGaps(cycleStart);
        runScoring();

        // Collect highest threat
        highestLevel = THREAT_NONE;
        alertName[0] = '\0';
        alertSec     = 0;
        for (int i = 0; i < tier1Count; i++) {
            if (tier1[i].alertLevel > highestLevel) {
                highestLevel = tier1[i].alertLevel;
                const char* src = (tier1[i].isKnown && tier1[i].sigIdx >= 0)
                                  ? sigs[tier1[i].sigIdx].name : tier1[i].name;
                String mac = macStr(tier1[i].mac);
                strncpy(alertName, src[0] ? src : mac.c_str(), 27);
                alertName[27] = '\0';
                alertSec = (millis() - tier1[i].firstSeen) / 1000;
            }
        }

        // Beep
        uint32_t now = millis();
        if (highestLevel == THREAT_ALERT) {
            if (!alertActive || now - lastBeepMs >= 30000) {
                beep(THREAT_ALERT);
                lastBeepMs = now; alertActive = true;
            }
        } else if (highestLevel == THREAT_WARNING && !alertActive) {
            beep(THREAT_WARNING);
            lastBeepMs = now; alertActive = true;
        } else if (highestLevel < THREAT_WARNING) {
            alertActive = false;
        }

        // Auto-log new alerts
        for (int i = 0; i < tier1Count; i++) {
            if (tier1[i].alertLevel >= THREAT_WARNING && !tier1[i].alertFired) {
                appendLog(tier1[i]);
                tier1[i].alertFired = true;
            }
        }

        drawScreen(highestLevel, alertName, alertSec);

        // Keyboard (checked between scan cycles ~every 2.5 s)
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if (k == 'l' || k == 'L') page++;
        if (k == 'a' || k == 'A') page = max(0, page - 1);
        if (k == 'c' || k == 'C') {
            memset(tier1, 0, sizeof(tier1)); tier1Count = 0;
            memset(tier2, 0, sizeof(tier2)); tier2Count = 0;
            memset(k1, 0, sizeof(k1)); memset(k2, 0, sizeof(k2));
            page = 0; alertActive = false;
        }
        if (k == 's' || k == 'S') saveLog();
    }

    // Cleanup
    esp_wifi_set_promiscuous(false);
    BLEDevice::deinit(false);
    stopI2S();
    dm.setBtActive(false);

#ifdef BOARD_TDECK_PLUS
    if (gpsSerial) { gpsSerial->end(); delete gpsSerial; gpsSerial = nullptr; }
#endif

    dm.printCommandScreen();
}
