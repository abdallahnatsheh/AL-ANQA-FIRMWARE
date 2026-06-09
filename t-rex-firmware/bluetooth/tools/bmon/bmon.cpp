#include "bmon.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include "utilities.h"
#include <NimBLEDevice.h>
#include <SD.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── advertisement types ───────────────────────────────────────────────────────
enum BmonType : uint8_t {
    BM_IBCN = 0,  // Apple iBeacon
    BM_EUID,       // Eddystone-UID
    BM_EURL,       // Eddystone-URL
    BM_ETLM,       // Eddystone-TLM
    BM_CLRT,       // Cleartext name
    BM_UNKN,       // Unknown / other MFR
};

static const char* const BM_LABELS[] = { "iBCN", "E-UID", "E-URL", "E-TLM", "CLRT", "UNKN" };
static const uint16_t    BM_COLORS[] = {
    TFT_CYAN, TFT_GREEN, TFT_GREEN, 0x7BEF, TFT_YELLOW, 0x4228
};

// ── device record ─────────────────────────────────────────────────────────────
struct BmonDev {
    uint8_t  mac[6];
    uint8_t  addrType;     // 0=public  1=random
    BmonType type;
    int8_t   rssi;
    uint32_t firstMs;
    uint32_t lastMs;
    uint32_t lastLogMs;    // millis() of last CSV write (0 = never)
    uint16_t sightings;
    char     info[44];     // truncated string for the screen
    char     extended[96]; // full decoded data — written to CSV only
    char     firstTs[20];  // "YYYY-MM-DD HH:MM:SS" at first sight, or "@NNNms"
};

// ── ring buffer (BT task → main task) ────────────────────────────────────────
#define BMON_RING 32
struct BmonRingEntry {
    uint8_t mac[6];
    uint8_t addrType;
    int8_t  rssi;
    uint8_t mfr[32];   // raw MFR data incl. 2-byte company ID LE
    uint8_t mfrLen;
    uint8_t svc[20];   // Eddystone service data (excl. UUID)
    uint8_t svcLen;
    char    name[20];  // complete local name
};

static volatile BmonRingEntry s_ring[BMON_RING];
static volatile uint8_t s_head = 0;
static volatile uint8_t s_tail = 0;

// ── device table ─────────────────────────────────────────────────────────────
#define BMON_MAX 64
static BmonDev s_devs[BMON_MAX];
static int     s_count = 0;

// ── log state ─────────────────────────────────────────────────────────────────
static bool   s_logOpen  = false;
static int    s_logCount = 0;
static char   s_logPath[36] = "";

// ── NimBLE scan pointer ───────────────────────────────────────────────────────
static NimBLEScan* s_scan = nullptr;

// ── helpers ───────────────────────────────────────────────────────────────────
static bool macEq(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static uint16_t rssiColor(int8_t r) {
    if (r >= -60) return TFT_GREEN;
    if (r >= -75) return TFT_YELLOW;
    return TFT_RED;
}

static void macStr(const uint8_t* m, char* buf) {
    snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

// Append hex bytes with separator, returns chars written
static int hexDump(const uint8_t* data, int len, char* out, int outSz, char sep = ' ') {
    int pos = 0;
    for (int i = 0; i < len && pos < outSz - 3; i++) {
        if (i > 0 && sep) out[pos++] = sep;
        pos += snprintf(out + pos, outSz - pos, "%02X", data[i]);
    }
    return pos;
}

// ── advertisement parser ──────────────────────────────────────────────────────
static BmonType parseAdv(const BmonRingEntry& e,
                          char* info,     size_t infoSz,
                          char* extended, size_t extSz) {
    info[0]     = '\0';
    extended[0] = '\0';

    // ── Apple iBeacon ─────────────────────────────────────────────────────────
    if (e.mfrLen >= 25 &&
        e.mfr[0] == 0x4C && e.mfr[1] == 0x00 &&
        e.mfr[2] == 0x02 && e.mfr[3] == 0x15) {

        uint16_t major = ((uint16_t)e.mfr[20] << 8) | e.mfr[21];
        uint16_t minor = ((uint16_t)e.mfr[22] << 8) | e.mfr[23];
        int8_t   txpow = (int8_t)e.mfr[24];

        snprintf(info, infoSz, "%02X%02X%02X%02X M:%u m:%u P:%d",
                 e.mfr[4], e.mfr[5], e.mfr[6], e.mfr[7], major, minor, txpow);

        snprintf(extended, extSz,
                 "UUID:%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X"
                 " Major:%u Minor:%u TxPow:%d",
                 e.mfr[4],  e.mfr[5],  e.mfr[6],  e.mfr[7],
                 e.mfr[8],  e.mfr[9],
                 e.mfr[10], e.mfr[11],
                 e.mfr[12], e.mfr[13],
                 e.mfr[14], e.mfr[15], e.mfr[16], e.mfr[17], e.mfr[18], e.mfr[19],
                 major, minor, txpow);
        return BM_IBCN;
    }

    // ── Eddystone (service UUID 0xFEAA) ──────────────────────────────────────
    if (e.svcLen >= 2) {
        uint8_t frame = e.svc[0];

        // Eddystone-UID
        if (frame == 0x00 && e.svcLen >= 18) {
            int8_t txpow = (int8_t)e.svc[1];
            snprintf(info, infoSz, "NS:%02X%02X%02X%02X I:%02X%02X%02X%02X P:%d",
                     e.svc[2],  e.svc[3],  e.svc[4],  e.svc[5],
                     e.svc[12], e.svc[13], e.svc[14], e.svc[15], txpow);

            char ns[22] = "", inst[14] = "";
            hexDump(e.svc + 2,  10, ns,   sizeof(ns),   '\0');
            hexDump(e.svc + 12,  6, inst, sizeof(inst),  '\0');
            snprintf(extended, extSz, "NS:%s Instance:%s TxPow:%d", ns, inst, txpow);
            return BM_EUID;
        }

        // Eddystone-URL
        if (frame == 0x10 && e.svcLen >= 3) {
            static const char* const schemes[] = {
                "http://www.", "https://www.", "http://", "https://"
            };
            static const char* const expand[] = {
                ".com/", ".org/", ".edu/", ".net/",
                ".info/", ".biz/", ".gov/", ".com"
            };
            int8_t  txpow = (int8_t)e.svc[1];
            uint8_t sch   = e.svc[2];

            char url[60] = "";
            size_t pos = (size_t)snprintf(url, sizeof(url), "%s",
                                          sch < 4 ? schemes[sch] : "?://");
            for (uint8_t i = 3; i < e.svcLen && pos < sizeof(url) - 1; i++) {
                uint8_t b = e.svc[i];
                if (b <= 0x07) {
                    size_t el = strlen(expand[b]);
                    if (pos + el < sizeof(url) - 1) { memcpy(url + pos, expand[b], el); pos += el; }
                } else if (b >= 0x20 && b < 0x7F) {
                    url[pos++] = (char)b;
                }
            }
            url[pos] = '\0';

            snprintf(info,     infoSz, "%s", url);
            snprintf(extended, extSz,  "URL:%s TxPow:%d", url, txpow);
            return BM_EURL;
        }

        // Eddystone-TLM
        if (frame == 0x20 && e.svcLen >= 6) {
            uint16_t vbatt = ((uint16_t)e.svc[2] << 8) | e.svc[3];
            int16_t  tempQ = (int16_t)(((uint16_t)e.svc[4] << 8) | (uint16_t)e.svc[5]);
            float    tempC = (float)tempQ / 256.0f;

            snprintf(info, infoSz, "Batt:%umV Temp:%.1fC", vbatt, tempC);

            if (e.svcLen >= 14) {
                uint32_t advCnt  = ((uint32_t)e.svc[6]  << 24) | ((uint32_t)e.svc[7]  << 16)
                                 | ((uint32_t)e.svc[8]  <<  8) |  (uint32_t)e.svc[9];
                uint32_t secCnt  = ((uint32_t)e.svc[10] << 24) | ((uint32_t)e.svc[11] << 16)
                                 | ((uint32_t)e.svc[12] <<  8) |  (uint32_t)e.svc[13];
                uint32_t uptimeSec = secCnt / 10;
                snprintf(extended, extSz, "Batt:%umV Temp:%.2fC AdvCnt:%lu Uptime:%lus",
                         vbatt, tempC, (unsigned long)advCnt, (unsigned long)uptimeSec);
            } else {
                snprintf(extended, extSz, "Batt:%umV Temp:%.2fC", vbatt, tempC);
            }
            return BM_ETLM;
        }
    }

    // ── Cleartext device name ─────────────────────────────────────────────────
    if (e.name[0]) {
        snprintf(info,     infoSz, "%s", e.name);
        snprintf(extended, extSz,  "Name:%s", e.name);
        return BM_CLRT;
    }

    // ── Unknown MFR data ──────────────────────────────────────────────────────
    if (e.mfrLen >= 2) {
        uint16_t cid = (uint16_t)e.mfr[0] | ((uint16_t)e.mfr[1] << 8);

        char shortHex[16] = "";
        int n = min((int)e.mfrLen - 2, 4);
        for (int i = 0; i < n; i++)
            snprintf(shortHex + i * 3, 4, "%02X ", e.mfr[2 + i]);
        snprintf(info, infoSz, "CID:%04X %s", cid, shortHex);

        char fullHex[96] = "";
        hexDump(e.mfr + 2, min((int)e.mfrLen - 2, 30), fullHex, sizeof(fullHex), ' ');
        snprintf(extended, extSz, "CID:%04X MFR:%s", cid, fullHex);
        return BM_UNKN;
    }

    snprintf(info,     infoSz, "-");
    snprintf(extended, extSz,  "-");
    return BM_UNKN;
}

// ── update or insert device table ─────────────────────────────────────────────
static bool processEntry(const BmonRingEntry& e) {
    char info[44], extended[96];
    BmonType t = parseAdv(e, info, sizeof(info), extended, sizeof(extended));

    for (int i = 0; i < s_count; i++) {
        if (macEq(s_devs[i].mac, e.mac)) {
            s_devs[i].rssi     = e.rssi;
            s_devs[i].lastMs   = millis();
            s_devs[i].sightings++;
            if (t != BM_UNKN) s_devs[i].type = t;
            strncpy(s_devs[i].info,     info,     43); s_devs[i].info[43]     = '\0';
            strncpy(s_devs[i].extended, extended, 95); s_devs[i].extended[95] = '\0';
            return false;
        }
    }

    if (s_count >= BMON_MAX) return false;
    BmonDev& d = s_devs[s_count++];
    memset(&d, 0, sizeof(d));
    memcpy(d.mac, e.mac, 6);
    d.addrType  = e.addrType;
    d.type      = t;
    d.rssi      = e.rssi;
    d.firstMs   = millis();
    d.lastMs    = d.firstMs;
    d.sightings = 1;
    strncpy(d.info,     info,     43); d.info[43]     = '\0';
    strncpy(d.extended, extended, 95); d.extended[95] = '\0';

    char ts[20];
    ClockManager::instance().getTimestamp(ts, sizeof(ts));
    if (ts[0]) { strncpy(d.firstTs, ts, 19); d.firstTs[19] = '\0'; }
    else        { snprintf(d.firstTs, sizeof(d.firstTs), "@%lums", millis()); }

    return true;
}

// ── log file ──────────────────────────────────────────────────────────────────
static bool openLog() {
    if (!sdCardManager.isReady()) return false;
    sdCardManager.ensureDir("/logs/bmon");

    uint16_t n = 1;
    char probe[36];
    while (n <= 999) {
        snprintf(probe, sizeof(probe), "/logs/bmon/%03u.csv", n);
        if (!SD.exists(probe)) break;
        n++;
    }
    strncpy(s_logPath, probe, sizeof(s_logPath) - 1);

    File f = SD.open(s_logPath, FILE_WRITE);
    if (!f) return false;
    f.println("timestamp,first_seen,mac,addr_type,type,rssi,sightings,info,extended");
    f.close();

    s_logOpen  = true;
    s_logCount = 0;
    return true;
}

static void appendLog(BmonDev& d) {
    if (!s_logOpen || !sdCardManager.isReady()) return;

    char mac[18]; macStr(d.mac, mac);

    char ts[20];
    ClockManager::instance().getTimestamp(ts, sizeof(ts));
    if (!ts[0]) snprintf(ts, sizeof(ts), "@%lums", millis());

    auto sanitize = [](char* buf, size_t sz) {
        for (size_t i = 0; i < sz && buf[i]; i++)
            if (buf[i] == ',') buf[i] = ';';
    };
    char safeInfo[44], safeExt[96];
    strncpy(safeInfo, d.info,     43); safeInfo[43] = '\0'; sanitize(safeInfo, sizeof(safeInfo));
    strncpy(safeExt,  d.extended, 95); safeExt[95]  = '\0'; sanitize(safeExt,  sizeof(safeExt));

    char line[256];
    snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%d,%u,%s,%s",
             ts, d.firstTs, mac,
             d.addrType == 0 ? "pub" : "rnd",
             BM_LABELS[d.type],
             d.rssi, d.sightings,
             safeInfo, safeExt);

    File f = SD.open(s_logPath, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
    d.lastLogMs = millis();
    s_logCount++;
}

// ── NimBLE passive scan callback ──────────────────────────────────────────────
class BmonCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        uint8_t next = (s_head + 1) % BMON_RING;
        if (next == s_tail) return;

        volatile BmonRingEntry& e = s_ring[s_head];

        std::string addr = dev->getAddress().toString();
        sscanf(addr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               (unsigned char*)&e.mac[0], (unsigned char*)&e.mac[1],
               (unsigned char*)&e.mac[2], (unsigned char*)&e.mac[3],
               (unsigned char*)&e.mac[4], (unsigned char*)&e.mac[5]);
        e.addrType = (uint8_t)dev->getAddress().getType();
        e.rssi     = (int8_t)dev->getRSSI();

        e.mfrLen = 0;
        if (dev->haveManufacturerData()) {
            std::string mfr = dev->getManufacturerData();
            e.mfrLen = (uint8_t)min((int)mfr.size(), 32);
            memcpy((void*)e.mfr, mfr.data(), e.mfrLen);
        }

        e.svcLen = 0;
        if (dev->haveServiceData()) {
            std::string sd = dev->getServiceData(NimBLEUUID((uint16_t)0xFEAA));
            if (!sd.empty()) {
                e.svcLen = (uint8_t)min((int)sd.size(), 20);
                memcpy((void*)e.svc, sd.data(), e.svcLen);
            }
        }

        ((char*)e.name)[0] = '\0';
        std::string nm = dev->getName();
        if (!nm.empty()) {
            size_t n = min(nm.size(), (size_t)19);
            memcpy((void*)e.name, nm.data(), n);
            ((char*)e.name)[n] = '\0';
        }

        s_head = next;
    }
};

static BmonCb s_cb;

// ── display layout ────────────────────────────────────────────────────────────
// Screen: 320×240 · status bar: 30 px · outputY: 38 · LINE_HEIGHT: 14
// Text size 1.0 → 6 px/char wide, 8 px tall
//
// Row  y    Content
//  0   38   Header: [BMON::PASSIVE]  N devs  [LOG:N file]
//  1   52   Column headers: TYPE / MAC / AT / RSSI / INFO
//  2   66   Separator
//  3   80   ┐
//  4   94   │
//  5  108   │  7 data rows (BMON_ROWS_PER_PAGE)
//  6  122   │
//  7  136   │
//  8  150   │
//  9  164   ┘
// 10  178   Separator
// 11  192   Extended detail — line 1
// 12  206   Extended detail — line 2
// 13  220   Footer / controls

#define BMON_ROWS_PER_PAGE  7
#define RY(n)               (outputY + (n) * LINE_HEIGHT)

// Column x-positions (6 px/char)
// sel(2)=12px  type(5)=30px  mac(17)=102px  at(3)=18px  rssi(4)=24px  info(17)=102px
static const int16_t CX_SEL  =   4;  // ">" indicator
static const int16_t CX_TYPE =  16;  // type label       [ends 46, +6 gap → mac 52]
static const int16_t CX_MAC  =  52;  // MAC address      [ends 154, +6 gap → at 160]
static const int16_t CX_AT   = 160;  // pub/rnd          [ends 178, +6 gap → rssi 184]
static const int16_t CX_RSSI = 184;  // RSSI             [ends 208, +6 gap → info 214]
static const int16_t CX_INFO = 214;  // info string      [17 chars → ends 316]

static int s_selected = 0;  // selected row index within current page

static void drawBmon(int page) {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    // ── compute sort order ────────────────────────────────────────────────────
    static int8_t idx[BMON_MAX];
    for (int i = 0; i < s_count; i++) idx[i] = (int8_t)i;
    for (int i = 0; i < s_count - 1; i++)
        for (int j = i + 1; j < s_count; j++)
            if (s_devs[(uint8_t)idx[j]].lastMs > s_devs[(uint8_t)idx[i]].lastMs) {
                int8_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }

    int totalPages = max(1, (s_count + BMON_ROWS_PER_PAGE - 1) / BMON_ROWS_PER_PAGE);
    int start      = page * BMON_ROWS_PER_PAGE;
    int end        = min(start + BMON_ROWS_PER_PAGE, s_count);
    int pageCount  = end - start;

    // Clamp selection
    if (s_selected >= pageCount && pageCount > 0) s_selected = pageCount - 1;
    if (s_selected < 0) s_selected = 0;
    int selGlobal = (pageCount > 0) ? start + s_selected : -1;

    // ── row 0 — header ────────────────────────────────────────────────────────
    dm.setCursor(CX_SEL, RY(0));
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("BMON");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("PASSIVE");
    dm.setTextColor(0x7BEF);     dm.printText("]");

    char hbuf[28];
    snprintf(hbuf, sizeof(hbuf), "  %d dev%s", s_count, s_count == 1 ? "" : "s");
    dm.setTextColor(TFT_WHITE);  dm.printText(hbuf);

    if (s_logOpen) {
        const char* fn = strrchr(s_logPath, '/');
        fn = fn ? fn + 1 : s_logPath;
        char lbuf[24];
        snprintf(lbuf, sizeof(lbuf), "  [LOG:%d %s]", s_logCount, fn);
        dm.setTextColor(TFT_CYAN); dm.printText(lbuf);
    }

    // ── row 1 — column headers ────────────────────────────────────────────────
    dm.setTextColor(0x7BEF);
    dm.setCursor(CX_TYPE, RY(1)); dm.printText("TYPE ");
    dm.setCursor(CX_MAC,  RY(1)); dm.printText("MAC              ");
    dm.setCursor(CX_AT,   RY(1)); dm.printText("AT ");
    dm.setCursor(CX_RSSI, RY(1)); dm.printText("RSSI");
    dm.setCursor(CX_INFO, RY(1)); dm.printText("INFO");

    // ── row 2 — separator ─────────────────────────────────────────────────────
    dm.setCursor(CX_SEL, RY(2));
    dm.printSeparator();

    // ── rows 3-9 — device list ────────────────────────────────────────────────
    for (int si = start; si < end; si++) {
        int16_t ry  = RY(3 + (si - start));
        const BmonDev& d = s_devs[(uint8_t)idx[si]];
        bool sel = (si == selGlobal);

        // Selected row background highlight
        if (sel) {
            dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
        }

        // Selector marker
        dm.setCursor(CX_SEL, ry);
        dm.setTextColor(TFT_CYAN);
        dm.printText(sel ? ">" : " ");

        // Type label (color-coded by type)
        char lbl[6]; snprintf(lbl, sizeof(lbl), "%-5s", BM_LABELS[d.type]);
        dm.setCursor(CX_TYPE, ry);
        dm.setTextColor(BM_COLORS[d.type]);
        dm.printText(lbl);

        // MAC address
        char mac[18]; macStr(d.mac, mac);
        dm.setCursor(CX_MAC, ry);
        dm.setTextColor(sel ? TFT_WHITE : 0xC618);
        dm.printText(mac);

        // Addr type: pub=white, rnd=grey
        dm.setCursor(CX_AT, ry);
        dm.setTextColor(d.addrType == 0 ? 0xC618 : 0x7BEF);
        dm.printText(d.addrType == 0 ? "pub" : "rnd");

        // RSSI (color by strength)
        char rssiStr[5]; snprintf(rssiStr, sizeof(rssiStr), "%4d", (int)d.rssi);
        dm.setCursor(CX_RSSI, ry);
        dm.setTextColor(rssiColor(d.rssi));
        dm.printText(rssiStr);

        // Info — truncated to 17 chars on screen, full data in detail pane
        char info[19]; snprintf(info, sizeof(info), "%-17.17s", d.info);
        dm.setCursor(CX_INFO, ry);
        dm.setTextColor(sel ? TFT_YELLOW : TFT_WHITE);
        dm.printText(info);
    }

    if (s_count == 0) {
        dm.setCursor(CX_SEL, RY(3));
        dm.setTextColor(0x7BEF);
        dm.printText("Listening for BLE advertisements...");
    }

    // ── row 10 — separator ────────────────────────────────────────────────────
    dm.setCursor(CX_SEL, RY(10));
    dm.printSeparator();

    // ── rows 11-12 — extended detail for selected device ──────────────────────
    const char* ext = (selGlobal >= 0 && selGlobal < s_count)
                      ? s_devs[(uint8_t)idx[selGlobal]].extended
                      : nullptr;

    if (ext && ext[0] && ext[0] != '-') {
        // Split into up to 2 lines of 52 chars each
        size_t extLen = strlen(ext);
        char det1[54] = "", det2[54] = "";

        if (extLen <= 52) {
            strncpy(det1, ext, 52);
        } else {
            strncpy(det1, ext, 52); det1[52] = '\0';
            // Break at last space for readability
            char* sp = strrchr(det1, ' ');
            int   br = (sp && sp > det1 + 8) ? (int)(sp - det1) : 52;
            det1[br] = '\0';
            const char* rest = ext + br + (ext[br] == ' ' ? 1 : 0);
            snprintf(det2, sizeof(det2), "%.52s", rest);
        }

        dm.setCursor(CX_SEL, RY(11));
        dm.setTextColor(TFT_WHITE); dm.printText(det1);

        if (det2[0]) {
            dm.setCursor(CX_SEL, RY(12));
            dm.setTextColor(0xA514); dm.printText(det2);
        }
    } else {
        dm.setCursor(CX_SEL, RY(11));
        dm.setTextColor(0x4208);
        dm.printText(s_count == 0 ? "no devices yet" : "no extended data");
    }

    // ── row 13 — footer / controls ────────────────────────────────────────────
    dm.setCursor(CX_SEL, RY(13));
    if (s_logOpen) {
        dm.setTextColor(TFT_CYAN);   dm.printText("[s]");
        dm.setTextColor(TFT_RED);    dm.printText("stop");
    } else {
        dm.setTextColor(0x7BEF);     dm.printText("[s]log");
    }
    dm.setTextColor(0x7BEF);
    char ftr[40];
    snprintf(ftr, sizeof(ftr), "  [a/l]%d/%d  [pad]sel  [q]quit",
             page + 1, totalPages);
    dm.printText(ftr);
}

// ── command entry point ───────────────────────────────────────────────────────
void runBmon(char* args) {
    (void)args;

    s_count      = 0;
    s_head       = s_tail = 0;
    s_logOpen    = false;
    s_logCount   = 0;
    s_logPath[0] = '\0';
    s_selected   = 0;

    NimBLEDevice::init("");
    displayManager.setBtActive(true);
    displayManager.updateStatusBar();

    s_scan = NimBLEDevice::getScan();
    s_scan->setScanCallbacks(nullptr);
    s_scan->setScanCallbacks(&s_cb, true);   // true = report duplicates
    s_scan->setActiveScan(false);            // passive — no SCAN_REQ
    s_scan->setInterval(160);
    s_scan->setWindow(144);
    s_scan->start(0, false);                 // indefinite, non-blocking

    int      page     = 0;
    uint32_t lastDraw = 0;
    bool     needDraw = true;

    while (true) {
        // Drain ring → update device table
        bool changed = false;
        while (s_tail != s_head) {
            BmonRingEntry e;
            memcpy(&e, (const void*)&s_ring[s_tail], sizeof(e));
            s_tail = (s_tail + 1) % BMON_RING;

            bool isNew = processEntry(e);
            changed = changed || isNew;

            // Log on first sight or after 60 s dedup window
            if (s_logOpen) {
                for (int i = 0; i < s_count; i++) {
                    if (macEq(s_devs[i].mac, e.mac)) {
                        uint32_t now = millis();
                        if (s_devs[i].lastLogMs == 0 || now - s_devs[i].lastLogMs >= 60000) {
                            appendLog(s_devs[i]);
                            changed = true;
                        }
                        break;
                    }
                }
            }
        }

        // Redraw on new device, log toggle, or every 1 s for RSSI updates
        uint32_t now = millis();
        if (needDraw || changed || now - lastDraw >= 1000) {
            int totalPages = max(1, (s_count + BMON_ROWS_PER_PAGE - 1) / BMON_ROWS_PER_PAGE);
            if (page >= totalPages) page = totalPages - 1;
            drawBmon(page);
            lastDraw = now;
            needDraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') break;

        if (k == 's' || k == 'S') {
            if (s_logOpen) {
                s_logOpen = false;
            } else {
                if (!openLog()) {
                    displayManager.setCursor(CX_SEL, RY(13));
                    displayManager.fillRect(0, RY(13), SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
                    displayManager.setCursor(CX_SEL, RY(13));
                    displayManager.setTextColor(TFT_RED);
                    displayManager.printText("SD not available");
                    vTaskDelay(pdMS_TO_TICKS(1500));
                }
            }
            needDraw = true;
        }

        // Page navigation — reset selection when switching pages
        if (k == 'l' || k == 'L') {
            int totalPages = max(1, (s_count + BMON_ROWS_PER_PAGE - 1) / BMON_ROWS_PER_PAGE);
            if (page < totalPages - 1) { page++; s_selected = 0; needDraw = true; }
        }
        if (k == 'a' || k == 'A') {
            if (page > 0) { page--; s_selected = 0; needDraw = true; }
        }

        // Row selection via trackpad
        if (tb == TBALL_DOWN) {
            int pageCount = min(BMON_ROWS_PER_PAGE,
                                max(0, s_count - page * BMON_ROWS_PER_PAGE));
            if (s_selected < pageCount - 1) { s_selected++; needDraw = true; }
        }
        if (tb == TBALL_UP) {
            if (s_selected > 0) { s_selected--; needDraw = true; }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) needDraw = true;

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    s_scan->stop();
    s_scan->setScanCallbacks(nullptr);
    s_logOpen = false;

    displayManager.setBtActive(false);
    displayManager.updateStatusBar();
    displayManager.printCommandScreen();
}
