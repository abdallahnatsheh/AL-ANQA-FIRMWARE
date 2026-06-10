#include "ble_info.h"
#include "bluetooth_functions.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include <NimBLEDevice.h>
#include <SD.h>
#include <cstring>
#include <cctype>

extern DisplayManager  displayManager;
extern InputHandling   inputHandler;
extern SDCardManager   sdCardManager;

// ── Storage ───────────────────────────────────────────────────────────────────

#define BI_MAX_SVCS   10
#define BI_MAX_CHARS   8
#define BI_MAX_LINES  80
#define BI_PER_PAGE   12
#define BI_NOTIF_MAX  16
#define BI_SNIFF_LINES 8
#define BI_SNIFF_MS   30000

struct BiChar {
    char    uuid[12];       // truncated for display
    char    fullUuid[40];   // full UUID for SD save
    char    props[6];       // R/W/N/I/- flags
    char    value[22];      // truncated display string
    uint8_t rawVal[20];     // raw bytes for full hex dump
    uint8_t rawLen;
    char    udesc[14];      // 0x2901 User Description
    uint8_t fmtType;        // 0x2904 format type (0=unknown)
    int8_t  fmtExp;         // 0x2904 exponent
    uint8_t risk;           // 0=none 1=low 2=med 3=high (auth leak score)
    char    riskReason[16]; // short reason label
};

struct BiSvc {
    char    uuid[12];       // truncated for display
    char    fullUuid[40];   // full UUID for SD save
    char    name[16];
    BiChar  chars[BI_MAX_CHARS];
    uint8_t nChars;
};

static BiSvc*  s_svcs = nullptr;  // ps_malloc'd — ~11KB in PSRAM, see ensureBiBuffers()
static uint8_t s_svcCount;
static bool    s_hasNotify;
static bool    s_hasWrite;
static bool    s_hasRisk    = false;    // any char scored risk >= 2
static bool    s_hasReplay  = false;    // sniff buffer has captured packets
static bool    s_pairEnabled = false;   // [p] toggles; persists for session

// Writable char index table: (svc, chr) pairs, max 16
struct BiWriteRef { uint8_t svc; uint8_t chr; };
static BiWriteRef s_writable[16];
static uint8_t    s_writableCount;

struct BiLine { char text[52]; uint16_t color; };
static BiLine* s_lines = nullptr;  // ps_malloc'd — ~4.2KB in PSRAM, see ensureBiBuffers()
static int    s_lineCount;

// Notification ring buffer (written from NimBLE task, read from main task)
struct BiNotif {
    char    uuid[12];       // truncated for display
    char    fullUuid[40];   // full UUID for SD save
    char    value[22];      // formatted display string
    uint32_t elapsed;
    uint8_t raw[20];        // raw bytes for replay
    uint8_t rawLen;
};
static BiNotif*          s_notifs = nullptr;  // ps_malloc'd — ~1.6KB in PSRAM, see ensureBiBuffers()
static volatile uint16_t s_nfTotal = 0;
static volatile uint8_t  s_nfWrite = 0;
static volatile bool     s_nfFlag  = false;
static uint32_t          s_sniffT0 = 0;

// Packet buffer for loaded .ble replay files
static BiNotif* s_loadedPkts = nullptr;  // ps_malloc'd — ~1.6KB in PSRAM, see ensureBiBuffers()
static uint8_t  s_loadedCount = 0;
static char     s_loadedMac[18] = {};
static uint8_t  s_loadedType   = BLE_ADDR_RANDOM;

// Lazy PSRAM alloc — ps_malloc requires the heap allocator set up by initArduino(),
// which hasn't run yet during global ctors, so allocate on first use instead.
static void ensureBiBuffers() {
    if (!s_svcs) {
        s_svcs = (BiSvc*)ps_malloc(BI_MAX_SVCS * sizeof(BiSvc));
        if (!s_svcs) s_svcs = (BiSvc*)malloc(BI_MAX_SVCS * sizeof(BiSvc));
        if (s_svcs) memset(s_svcs, 0, BI_MAX_SVCS * sizeof(BiSvc));
    }
    if (!s_lines) {
        s_lines = (BiLine*)ps_malloc(BI_MAX_LINES * sizeof(BiLine));
        if (!s_lines) s_lines = (BiLine*)malloc(BI_MAX_LINES * sizeof(BiLine));
        if (s_lines) memset(s_lines, 0, BI_MAX_LINES * sizeof(BiLine));
    }
    if (!s_notifs) {
        s_notifs = (BiNotif*)ps_malloc(BI_NOTIF_MAX * sizeof(BiNotif));
        if (!s_notifs) s_notifs = (BiNotif*)malloc(BI_NOTIF_MAX * sizeof(BiNotif));
        if (s_notifs) memset(s_notifs, 0, BI_NOTIF_MAX * sizeof(BiNotif));
    }
    if (!s_loadedPkts) {
        s_loadedPkts = (BiNotif*)ps_malloc(BI_NOTIF_MAX * sizeof(BiNotif));
        if (!s_loadedPkts) s_loadedPkts = (BiNotif*)malloc(BI_NOTIF_MAX * sizeof(BiNotif));
        if (s_loadedPkts) memset(s_loadedPkts, 0, BI_NOTIF_MAX * sizeof(BiNotif));
    }
}

// ── Known UUID names ──────────────────────────────────────────────────────────

static const struct { uint16_t u; const char* n; } kSvc[] = {
    { 0x1800, "GenericAccess"  }, { 0x1801, "GenericAttr"  },
    { 0x180A, "DeviceInfo"     }, { 0x180F, "Battery"      },
    { 0x1812, "HID"            }, { 0x1803, "LinkLoss"     },
    { 0x1804, "TxPower"        }, { 0x1810, "BloodPressure"},
};
static const struct { uint16_t u; const char* n; } kChr[] = {
    { 0x2A00, "DeviceName"   }, { 0x2A01, "Appearance"   },
    { 0x2A19, "BattLevel"    }, { 0x2A24, "ModelNum"     },
    { 0x2A25, "SerialNum"    }, { 0x2A26, "FirmwareRev"  },
    { 0x2A27, "HardwareRev"  }, { 0x2A29, "Manufacturer" },
    { 0x2A50, "PnPID"        }, { 0x2A04, "ConnParams"   },
};

static const char* svcName(uint16_t u) {
    for (auto& e : kSvc) if (e.u == u) return e.n;
    return nullptr;
}
static const char* chrName(uint16_t u) {
    for (auto& e : kChr) if (e.u == u) return e.n;
    return nullptr;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static void shortUuid(const std::string& full, char* out, size_t outLen) {
    if (full.size() <= 10) {
        strncpy(out, full.c_str(), outLen - 1);
        out[outLen - 1] = '\0';
    } else {
        snprintf(out, outLen, "%.8s~", full.c_str());
    }
}

static void fmtVal(const std::string& val, char* out, size_t outLen) {
    if (val.empty()) { strncpy(out, "(empty)", outLen); return; }
    bool isPrint = true;
    for (unsigned char c : val) if (c < 0x20 || c > 0x7E) { isPrint = false; break; }
    if (isPrint) {
        snprintf(out, outLen, "\"%.*s\"", (int)(outLen - 3), val.c_str());
    } else if (val.size() == 1) {
        snprintf(out, outLen, "0x%02X", (uint8_t)val[0]);
    } else {
        size_t n = val.size() < 5 ? val.size() : 5;
        char   hex[32] = {};
        for (size_t i = 0; i < n; i++) snprintf(hex + i * 3, 4, "%02X ", (uint8_t)val[i]);
        if (val.size() > 5) { hex[n*3-1] = '.'; hex[n*3] = '.'; hex[n*3+1] = '\0'; }
        strncpy(out, hex, outLen - 1);
        out[outLen - 1] = '\0';
    }
}

// Decode using 0x2904 Presentation Format type; falls back to fmtVal
static void decodeVal(const std::string& raw, uint8_t fmtType, int8_t fmtExp,
                      char* out, size_t outLen) {
    if (raw.empty()) { strncpy(out, "(empty)", outLen); return; }
    char num[24] = {};
    switch (fmtType) {
        case 0x01: snprintf(num, sizeof(num), "%s", (uint8_t)raw[0] ? "true" : "false"); break;
        case 0x04: snprintf(num, sizeof(num), "%u",  (uint8_t)raw[0]); break;
        case 0x06: if (raw.size() >= 2)
                       snprintf(num, sizeof(num), "%u",
                           (uint16_t)((uint8_t)raw[1] << 8 | (uint8_t)raw[0]));
                   break;
        case 0x08: if (raw.size() >= 4)
                       snprintf(num, sizeof(num), "%lu",
                           (unsigned long)((uint32_t)(uint8_t)raw[3] << 24 |
                                           (uint32_t)(uint8_t)raw[2] << 16 |
                                           (uint32_t)(uint8_t)raw[1] << 8  |
                                           (uint8_t)raw[0]));
                   break;
        case 0x0C: snprintf(num, sizeof(num), "%d",  (int8_t)raw[0]); break;
        case 0x0D: if (raw.size() >= 2)
                       snprintf(num, sizeof(num), "%d",
                           (int16_t)((uint8_t)raw[1] << 8 | (uint8_t)raw[0]));
                   break;
        case 0x16: // utf8s
            snprintf(out, outLen, "\"%.*s\"", (int)(outLen - 3), raw.c_str());
            return;
        default: fmtVal(raw, out, outLen); return;
    }
    if (num[0] == '\0') { fmtVal(raw, out, outLen); return; }
    if (fmtExp != 0) snprintf(out, outLen, "%se%d", num, fmtExp);
    else             strncpy(out, num, outLen - 1);
}

// ── Notify callback (NimBLE task context) ─────────────────────────────────────

static void onNotify(NimBLERemoteCharacteristic* chr,
                     uint8_t* data, size_t len, bool /*isNotify*/) {
    uint8_t idx = s_nfWrite;
    shortUuid(chr->getUUID().toString(), s_notifs[idx].uuid, sizeof(s_notifs[idx].uuid));
    strncpy(s_notifs[idx].fullUuid, chr->getUUID().toString().c_str(), sizeof(s_notifs[idx].fullUuid) - 1);
    s_notifs[idx].fullUuid[sizeof(s_notifs[idx].fullUuid) - 1] = '\0';
    uint8_t rlen = len < 20 ? (uint8_t)len : 20;
    memcpy(s_notifs[idx].raw, data, rlen);
    s_notifs[idx].rawLen = rlen;
    std::string val((char*)data, rlen);
    fmtVal(val, s_notifs[idx].value, sizeof(s_notifs[idx].value));
    s_notifs[idx].elapsed = millis() - s_sniffT0;
    s_nfWrite = (s_nfWrite + 1) % BI_NOTIF_MAX;
    s_nfTotal++;
    s_nfFlag = true;
}

// ── Risk scoring ──────────────────────────────────────────────────────────────

// Returns 0–3 and writes a short reason label.
// Operates on the raw read value (before display formatting).
static uint8_t scoreRisk(const std::string& val, const char* props,
                         char* reason, size_t reasonLen) {
    reason[0] = '\0';
    if (val.empty()) return 0;

    size_t len = val.size();
    bool allPrint = true;
    for (unsigned char c : val) if (c < 0x20 || c > 0x7E) { allPrint = false; break; }

    uint8_t score = 0;

    if (!allPrint) {
        // Binary data — check for cryptographic key sizes
        if (len == 16)      { score += 3; strncpy(reason, "16B key?",  reasonLen); }
        else if (len == 32) { score += 3; strncpy(reason, "32B key?",  reasonLen); }
        else if (len == 20) { score += 2; strncpy(reason, "SHA1/HMAC", reasonLen); }
        else if (len >= 8)  { score += 1; strncpy(reason, "bin>=8B",   reasonLen); }
    } else {
        // Printable — check for encoded secrets
        bool isHex = (len >= 8 && len % 2 == 0);
        for (unsigned char c : val) if (!isxdigit(c)) { isHex = false; break; }
        if (isHex) { score += 2; strncpy(reason, "hex str",   reasonLen); }

        bool isDigits = !val.empty();
        for (unsigned char c : val) if (!isdigit(c)) { isDigits = false; break; }
        if (isDigits && len >= 4 && len <= 8)
            { score += 2; strncpy(reason, "PIN/code",  reasonLen); }

        if (len >= 20 && !isHex && !isDigits)
            { score += 1; strncpy(reason, "long str",  reasonLen); }
    }

    // Writable secrets are more severe
    if (score > 0 && strchr(props, 'W')) score++;

    if (score >= 4) return 3;
    if (score >= 2) return 2;
    if (score >= 1) return 1;
    return 0;
}

// ── Enumeration ───────────────────────────────────────────────────────────────

static void enumerate(NimBLEClient* client) {
    s_svcCount      = 0;
    s_hasNotify     = false;
    s_hasWrite      = false;
    s_hasRisk       = false;
    s_writableCount = 0;
    const auto& services = client->getServices(true);
    if (services.empty()) return;

    for (auto* svc : services) {
        if (s_svcCount >= BI_MAX_SVCS) break;
        BiSvc& bs = s_svcs[s_svcCount++];
        memset(&bs, 0, sizeof(bs));

        shortUuid(svc->getUUID().toString(), bs.uuid, sizeof(bs.uuid));
        strncpy(bs.fullUuid, svc->getUUID().toString().c_str(), sizeof(bs.fullUuid) - 1);
        bs.fullUuid[sizeof(bs.fullUuid) - 1] = '\0';
        uint16_t su = (svc->getUUID().bitSize() == 16)
                      ? ((const ble_uuid16_t*)svc->getUUID().getBase())->value : 0;
        const char* sn = su ? svcName(su) : nullptr;
        if (sn) strncpy(bs.name, sn, sizeof(bs.name) - 1);

        const auto& chars = svc->getCharacteristics(true);
        if (chars.empty()) continue;

        for (auto* chr : chars) {
            if (bs.nChars >= BI_MAX_CHARS) break;
            BiChar& bc = bs.chars[bs.nChars++];
            memset(&bc, 0, sizeof(bc));

            shortUuid(chr->getUUID().toString(), bc.uuid, sizeof(bc.uuid));
            strncpy(bc.fullUuid, chr->getUUID().toString().c_str(), sizeof(bc.fullUuid) - 1);
            bc.fullUuid[sizeof(bc.fullUuid) - 1] = '\0';

            uint8_t pi = 0;
            if (chr->canRead())     bc.props[pi++] = 'R';
            if (chr->canWrite())  {
                bc.props[pi++] = 'W'; s_hasWrite = true;
                if (s_writableCount < 16) {
                    s_writable[s_writableCount++] = { (uint8_t)(s_svcCount - 1),
                                                       (uint8_t)(bs.nChars - 1) };
                }
            }
            if (chr->canNotify())   { bc.props[pi++] = 'N'; s_hasNotify = true; }
            if (chr->canIndicate()) { bc.props[pi++] = 'I'; s_hasNotify = true; }
            if (pi == 0)            bc.props[pi++] = '-';
            bc.props[pi] = '\0';

            uint16_t cu = (chr->getUUID().bitSize() == 16)
                          ? ((const ble_uuid16_t*)chr->getUUID().getBase())->value : 0;
            const char* cn = cu ? chrName(cu) : nullptr;

            // Read 0x2904 Presentation Format descriptor
            auto* pfmt = chr->getDescriptor(NimBLEUUID((uint16_t)0x2904));
            if (pfmt) {
                std::string pv = pfmt->readValue();
                if (pv.size() >= 2) {
                    bc.fmtType = (uint8_t)pv[0];
                    bc.fmtExp  = (int8_t)pv[1];
                }
            }

            // Read value for readable chars
            if (chr->canRead() && client->isConnected()) {
                std::string val = chr->readValue();
                // Store raw bytes for full hex dump in saved files
                bc.rawLen = (uint8_t)(val.size() < sizeof(bc.rawVal) ? val.size() : sizeof(bc.rawVal));
                memcpy(bc.rawVal, val.data(), bc.rawLen);
                // Score risk on raw bytes before formatting
                bc.risk = scoreRisk(val, bc.props, bc.riskReason, sizeof(bc.riskReason));
                if (bc.risk >= 2) s_hasRisk = true;
                if (cn) {
                    char vbuf[14] = {};
                    if (bc.fmtType) decodeVal(val, bc.fmtType, bc.fmtExp, vbuf, sizeof(vbuf));
                    else            fmtVal(val, vbuf, sizeof(vbuf));
                    snprintf(bc.value, sizeof(bc.value), "%s:%s", cn, vbuf);
                } else {
                    if (bc.fmtType) decodeVal(val, bc.fmtType, bc.fmtExp, bc.value, sizeof(bc.value));
                    else            fmtVal(val, bc.value, sizeof(bc.value));
                }
            } else if (cn) {
                strncpy(bc.value, cn, sizeof(bc.value) - 1);
            }

            // User Description descriptor (0x2901) for unnamed, non-readable chars
            if (!cn && !chr->canRead()) {
                auto* udesc = chr->getDescriptor(NimBLEUUID((uint16_t)0x2901));
                if (udesc) {
                    std::string dval = udesc->readValue();
                    bool isPrint = !dval.empty();
                    for (unsigned char c : dval) if (c < 0x20 || c > 0x7E) { isPrint = false; break; }
                    if (isPrint) strncpy(bc.udesc, dval.c_str(), sizeof(bc.udesc) - 1);
                }
            }
        }
    }
}

// ── Pairing / security ────────────────────────────────────────────────────────

class BiSecCallbacks : public NimBLEClientCallbacks {
    void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("Passkey (6 digits):");
        char buf[8] = {}; int pos = 0;
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printText("> ");
        while (pos < 6) {
            char k = inputHandler.getKeyboardInput();
            if (k >= '0' && k <= '9') {
                buf[pos++] = k;
                char ch[2] = { k, '\0' };
                displayManager.printText(ch);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        NimBLEDevice::injectPassKey(connInfo, (uint32_t)atol(buf));
    }
    void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        // Numeric comparison — show pin, ask y/n
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);
        char msg[32]; snprintf(msg, sizeof(msg), "Confirm PIN %06lu? [y/n]", (unsigned long)pin);
        displayManager.println(msg);
        bool accept = false;
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'y' || k == 'Y') { accept = true;  break; }
            if (k == 'n' || k == 'N') { accept = false; break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        NimBLEDevice::injectConfirmPasskey(connInfo, accept);
    }
};
static BiSecCallbacks s_secCallbacks;

static void applyPairing(NimBLEClient* client) {
    if (!s_pairEnabled) return;
    NimBLEDevice::setSecurityAuth(true, true, true);   // bonding, MITM, SC
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);
    client->setClientCallbacks(&s_secCallbacks, false);
    client->secureConnection();
}

// ── Replay file load helpers (needed by runReplay) ───────────────────────────

static bool loadReplayFromSd(const char* path) {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    s_loadedCount = 0;
    s_loadedMac[0] = '\0';

    String hdr = f.readStringUntil('\n'); hdr.trim();
    if (!hdr.equals("TREX_BLE_REPLAY")) { f.close(); return false; }

    while (f.available() && s_loadedCount < BI_NOTIF_MAX) {
        String line = f.readStringUntil('\n'); line.trim();
        if (line.startsWith("MAC ")) {
            strncpy(s_loadedMac, line.c_str() + 4, 17);
            s_loadedMac[17] = '\0';
        } else if (line.startsWith("TYPE ")) {
            s_loadedType = (uint8_t)line.substring(5).toInt();
        } else if (line.startsWith("PKT ")) {
            String body = line.substring(4);
            int sp1 = body.indexOf(' ');
            int sp2 = sp1 >= 0 ? body.indexOf(' ', sp1 + 1) : -1;
            if (sp1 < 0 || sp2 < 0) continue;
            BiNotif& pkt = s_loadedPkts[s_loadedCount];
            memset(&pkt, 0, sizeof(pkt));
            strncpy(pkt.uuid, body.substring(0, sp1).c_str(), 11);
            pkt.elapsed = (uint32_t)body.substring(sp1 + 1, sp2).toInt();
            String hexStr = body.substring(sp2 + 1); hexStr.trim();
            uint8_t bi = 0;
            for (int h = 0; h + 1 < (int)hexStr.length() && bi < 20; h += 2) {
                char hx[3] = { hexStr[h], hexStr[h+1], '\0' };
                pkt.raw[bi++] = (uint8_t)strtoul(hx, nullptr, 16);
            }
            pkt.rawLen = bi;
            std::string rawStr((char*)pkt.raw, pkt.rawLen);
            fmtVal(rawStr, pkt.value, sizeof(pkt.value));
            s_loadedCount++;
        }
    }
    f.close();
    return s_loadedCount > 0;
}

static int listReplayFiles(char names[][48], int maxFiles) {
    if (!sdCardManager.canAccessSD()) return 0;
    File dir = SD.open(SD_DIR_BLEINFO);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
    int count = 0;
    while (count < maxFiles) {
        File entry = dir.openNextFile();
        if (!entry) break;
        const char* nm = entry.name();
        size_t len = strlen(nm);
        if (len > 11 && strcmp(nm + len - 11, "_replay.ble") == 0) {
            snprintf(names[count], 48, SD_DIR_BLEINFO "/%s", nm);
            count++;
        }
        entry.close();
    }
    dir.close();
    return count;
}

// ── Auth leak audit view ──────────────────────────────────────────────────────

static void runAudit() {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[AUDIT] BLE Auth Leak Report");
    displayManager.printSeparator();

    int found = 0;
    for (int i = 0; i < s_svcCount; i++) {
        for (int j = 0; j < s_svcs[i].nChars; j++) {
            BiChar& c = s_svcs[i].chars[j];
            if (c.risk == 0) continue;
            found++;

            const char* lvl = (c.risk >= 3) ? "HIGH" :
                              (c.risk == 2) ? "MED " : "LOW ";
            uint16_t col    = (c.risk >= 3) ? TFT_RED :
                              (c.risk == 2) ? TFT_YELLOW : 0xFD20;

            char svcBuf[10]; strncpy(svcBuf, s_svcs[i].uuid, 9); svcBuf[9] = '\0';
            char line[52];
            snprintf(line, sizeof(line), "[%s] %-9s %-9s", lvl, svcBuf, c.uuid);
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(col);
            displayManager.println(line);

            // Reason + value on next line
            char detail[52];
            snprintf(detail, sizeof(detail), "     %-10s %s",
                     c.riskReason[0] ? c.riskReason : "?",
                     c.value[0]      ? c.value      : "(no value)");
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            displayManager.println(detail);
        }
    }

    if (found == 0) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("No suspicious chars found.");
    }

    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    char sum[40]; snprintf(sum, sizeof(sum), "%d flagged  [q] back", found);
    displayManager.println(sum);

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── Replay ────────────────────────────────────────────────────────────────────

static void runReplay(const char* macStr, uint8_t addrType) {
    // Determine packet source: live ring buffer or loaded file
    const BiNotif* pkts    = nullptr;
    uint8_t        pktCount = 0;
    const char*    replayMac  = macStr;
    uint8_t        replayType = addrType;

    // Always offer load-from-SD option if SD available
    bool wantLoad = (s_nfTotal == 0);   // force load if no live packets
    if (!wantLoad && sdCardManager.canAccessSD()) {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(0x7BEF);
        displayManager.println("[WRITE-CAP] source:");
        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        char liveLine[40];
        snprintf(liveLine, sizeof(liveLine), "[1] live capture (%lu pkts)",
                 (unsigned long)s_nfTotal);
        displayManager.println(liveLine);
        displayManager.println("[2] load from SD (.ble file)");
        displayManager.println("[q] cancel");
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == '1') { wantLoad = false; break; }
            if (k == '2') { wantLoad = true;  break; }
            if (k == 'q' || k == 'Q') return;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (wantLoad) {
        // Show file picker
        char fileNames[8][48];
        int  fileCount = listReplayFiles(fileNames, 8);

        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(0x7BEF);
        displayManager.println("[WRITE-CAP] load file:");
        displayManager.printSeparator();

        if (fileCount == 0) {
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_RED);
            displayManager.println("No .ble files in /apps/bleinfo/");
            vTaskDelay(pdMS_TO_TICKS(1500));
            return;
        }
        for (int i = 0; i < fileCount; i++) {
            // Show just filename portion
            const char* nm = strrchr(fileNames[i], '/');
            nm = nm ? nm + 1 : fileNames[i];
            char buf[48]; snprintf(buf, sizeof(buf), "[%d] %s", i, nm);
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            displayManager.println(buf);
        }
        displayManager.printSeparator();
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("num=select  q=cancel");

        int fileSel = -1;
        while (fileSel < 0) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') return;
            if (k >= '0' && k <= '9') {
                int n = k - '0';
                if (n < fileCount) fileSel = n;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Loading...");

        if (!loadReplayFromSd(fileNames[fileSel])) {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Load failed.");
            vTaskDelay(pdMS_TO_TICKS(1500));
            return;
        }

        pkts       = s_loadedPkts;
        pktCount   = s_loadedCount;
        replayMac  = s_loadedMac[0] ? s_loadedMac : macStr;
        replayType = s_loadedType;
    } else {
        // Use live ring buffer
        uint16_t tot    = s_nfTotal;
        uint8_t  actual = tot < BI_NOTIF_MAX ? (uint8_t)tot : BI_NOTIF_MAX;
        uint8_t  oldest = (s_nfWrite - actual + BI_NOTIF_MAX) % BI_NOTIF_MAX;
        // Copy ring buffer slice into loadedPkts for uniform access
        s_loadedCount = 0;
        for (uint8_t i = 0; i < actual; i++) {
            s_loadedPkts[s_loadedCount++] = s_notifs[(oldest + i) % BI_NOTIF_MAX];
        }
        pkts     = s_loadedPkts;
        pktCount = s_loadedCount;
    }

    if (pktCount == 0) return;

    // Show packet list
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);
    char hdr[40]; snprintf(hdr, sizeof(hdr), "[WRITE-CAP] %.17s", replayMac);
    displayManager.println(hdr);
    displayManager.printSeparator();

    for (uint8_t i = 0; i < pktCount && i < 9; i++) {
        char line[52];
        uint32_t e = pkts[i].elapsed;
        snprintf(line, sizeof(line), "[%d] %2lu.%1lus %-9s %s",
                 i, e / 1000, (e % 1000) / 100,
                 pkts[i].uuid, pkts[i].value);
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.println(line);
    }

    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("num=pick packet  q=cancel");

    int pktSel = -1;
    while (pktSel < 0) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if (k >= '0' && k <= '9') {
            int n = k - '0';
            if (n < (int)pktCount) pktSel = n;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    const BiNotif& selectedPkt = pkts[pktSel];

    // Show writable char list for target selection
    if (s_writableCount == 0) {
        displayManager.clearScreen();
        displayManager.setDefaultTextSize();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("No writable chars to replay to.");
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[WRITE-CAP] target char:");
    displayManager.printSeparator();

    for (uint8_t i = 0; i < s_writableCount; i++) {
        BiChar& bc = s_svcs[s_writable[i].svc].chars[s_writable[i].chr];
        bool match = strcmp(bc.uuid, selectedPkt.uuid) == 0;
        char buf[48];
        snprintf(buf, sizeof(buf), "[%d] %-9s [%s]%s", i, bc.uuid, bc.props, match ? " <--" : "");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(match ? TFT_GREEN : TFT_WHITE);
        displayManager.println(buf);
    }

    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("num=select  q=cancel");

    int chrSel = -1;
    while (chrSel < 0) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if (k >= '0' && k <= '9') {
            int n = k - '0';
            if (n < s_writableCount) chrSel = n;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    BiChar& target = s_svcs[s_writable[chrSel].svc].chars[s_writable[chrSel].chr];

    // Reconnect and write
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    char info[52];
    snprintf(info, sizeof(info), "Writing %dB → %s", selectedPkt.rawLen, target.uuid);
    displayManager.println(info);
    snprintf(info, sizeof(info), "Target: %.17s", replayMac);
    displayManager.println(info);

    NimBLEDevice::init("");
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5000);   // v2.x: unit is ms
    if (!client->connect(NimBLEAddress(replayMac, replayType))) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Connect failed.");
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    applyPairing(client);

    bool sent = false;
    const auto& svcs = client->getServices(true);
    if (!svcs.empty()) {
        for (auto* svc : svcs) {
            const auto& chars = svc->getCharacteristics(true);
            if (chars.empty()) continue;
            for (auto* chr : chars) {
                char uuidBuf[12];
                shortUuid(chr->getUUID().toString(), uuidBuf, sizeof(uuidBuf));
                if (strcmp(uuidBuf, target.uuid) == 0 && chr->canWrite()) {
                    sent = chr->writeValue(selectedPkt.raw, selectedPkt.rawLen, true);
                    break;
                }
            }
            if (sent) break;
        }
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(sent ? TFT_GREEN : TFT_RED);
    displayManager.println(sent ? "Write OK." : "Write failed.");
    vTaskDelay(pdMS_TO_TICKS(1200));
}

// ── Write support ─────────────────────────────────────────────────────────────

// Read up to 20 hex bytes from keyboard; returns byte count or -1 on cancel.
// Prompt already shown by caller. Input: "DE AD BE EF" or "DEADBEEF" or "hello"
static int readHexInput(uint8_t* buf, size_t bufLen) {
    char line[48] = {};
    int  len       = 0;   // total chars in buffer
    int  cur       = 0;   // cursor position (0..len)

    int inputY = displayManager.getCursorY();

    // Redraw the full input line with cursor block at `cur`
    auto redrawInput = [&]() {
        displayManager.fillRect(4, inputY, SCREEN_WIDTH - 4, LINE_HEIGHT, TFT_BLACK);
        displayManager.setCursor(4, inputY);
        displayManager.setTextColor(TFT_WHITE);
        // Print "> " + text before cursor + "_" + text after cursor
        char pre[50] = "> ";
        strncat(pre, line, cur);
        displayManager.printText(pre);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.printText("_");          // cursor block
        displayManager.setTextColor(TFT_WHITE);
        if (cur < len) displayManager.printText(line + cur);
    };
    redrawInput();

    while (true) {
        // Check trackball for cursor movement
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_LEFT  && cur > 0)   { cur--; redrawInput(); }
        if (tb == TBALL_RIGHT && cur < len) { cur++; redrawInput(); }

        char k = inputHandler.getKeyboardInput();
        if (!k) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

        if (k == '\r' || k == '\n') break;

        if (k == '\b' || k == 0x7F) {
            if (cur > 0) {
                // Delete char to the left of cursor
                memmove(line + cur - 1, line + cur, len - cur);
                len--; cur--;
                line[len] = '\0';
                redrawInput();
            }
            continue;
        }

        if (k == 'q' || k == 'Q') return -1;

        if (len < (int)sizeof(line) - 1) {
            // Insert char at cursor position
            memmove(line + cur + 1, line + cur, len - cur);
            line[cur++] = k;
            len++;
            line[len] = '\0';
            redrawInput();
        }
    }

    // Remove cursor, finalise display
    displayManager.fillRect(4, inputY, SCREEN_WIDTH - 4, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(4, inputY);
    displayManager.setTextColor(TFT_WHITE);
    char final[52]; snprintf(final, sizeof(final), "> %s", line);
    displayManager.printText(final);
    displayManager.println("");

    // Try to decode: if all printable ASCII, treat as raw string
    bool allPrint = (len > 0);
    for (int i = 0; i < len; i++) if (!isprint((unsigned char)line[i])) { allPrint = false; break; }

    // Strip spaces and try hex decode
    char hex[48] = {}; int hi = 0;
    for (int i = 0; i < len; i++) if (line[i] != ' ') hex[hi++] = line[i];

    bool isHex = (hi > 0 && hi % 2 == 0);
    for (int i = 0; i < hi && isHex; i++)
        if (!isxdigit((unsigned char)hex[i])) isHex = false;

    if (isHex) {
        int n = hi / 2;
        if (n > (int)bufLen) n = bufLen;
        for (int i = 0; i < n; i++) {
            char b[3] = { hex[i*2], hex[i*2+1], '\0' };
            buf[i] = (uint8_t)strtoul(b, nullptr, 16);
        }
        return n;
    }
    // Fall back: raw ASCII bytes
    int n = len < (int)bufLen ? len : (int)bufLen;
    memcpy(buf, line, n);
    return n;
}

static void runWrite(const char* macStr, uint8_t addrType) {
    if (s_writableCount == 0) return;

    // Show writable char list
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[WRITE] select char:");
    displayManager.printSeparator();

    for (uint8_t i = 0; i < s_writableCount; i++) {
        BiChar& bc = s_svcs[s_writable[i].svc].chars[s_writable[i].chr];
        char buf[52];
        snprintf(buf, sizeof(buf), "[%d] %-9s [%s] %.8s",
                 i, bc.uuid, bc.props, bc.value[0] ? bc.value : "");
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.println(buf);
    }

    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("num=select  q=cancel");

    // Wait for selection
    int sel = -1;
    while (sel < 0) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if (k >= '0' && k <= '9') {
            int n = k - '0';
            if (n < s_writableCount) sel = n;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    BiChar& bc = s_svcs[s_writable[sel].svc].chars[s_writable[sel].chr];

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    char hdr[48]; snprintf(hdr, sizeof(hdr), "Write to %s", bc.uuid);
    displayManager.println(hdr);
    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("Hex bytes or ASCII text.");
    displayManager.println("q=cancel  Enter=send");
    displayManager.printSeparator();

    uint8_t payload[20];
    int     payLen = readHexInput(payload, sizeof(payload));
    if (payLen <= 0) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Cancelled.");
        vTaskDelay(pdMS_TO_TICKS(800));
        return;
    }

    // Reconnect and write
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Connecting...");

    // Do NOT call NimBLEDevice::init() — stack is already up
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5000);   // v2.x: unit is ms
    if (!client->connect(NimBLEAddress(macStr, addrType))) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Connect failed.");
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    applyPairing(client);

    // Find the characteristic by UUID and write
    // Try with-response first; fall back to without-response (e.g. Da Fit 0xffd1)
    bool sent = false;
    const auto& svcs = client->getServices(true);
    if (!svcs.empty()) {
        for (auto* svc : svcs) {
            const auto& chars = svc->getCharacteristics(true);
            if (chars.empty()) continue;
            for (auto* chr : chars) {
                char uuidBuf[12]; shortUuid(chr->getUUID().toString(), uuidBuf, sizeof(uuidBuf));
                if (strcmp(uuidBuf, bc.uuid) == 0) {
                    if (chr->canWrite())
                        sent = chr->writeValue(payload, payLen, true);
                    if (!sent && chr->canWriteNoResponse())
                        sent = chr->writeValue(payload, payLen, false);
                    break;
                }
            }
            if (sent) break;
        }
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    displayManager.setTextColor(sent ? TFT_GREEN : TFT_RED);
    displayManager.println(sent ? "Write OK." : "Write failed.");
    vTaskDelay(pdMS_TO_TICKS(1200));
}

// ── Fuzz support ──────────────────────────────────────────────────────────────

static void runFuzz(const char* macStr, uint8_t addrType) {
    if (s_writableCount == 0) return;

    // Select target char (same UI as runWrite)
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(0x7BEF);
    displayManager.println("[FUZZ] select target char:");
    displayManager.printSeparator();

    for (uint8_t i = 0; i < s_writableCount; i++) {
        BiChar& bc = s_svcs[s_writable[i].svc].chars[s_writable[i].chr];
        char buf[48]; snprintf(buf, sizeof(buf), "[%d] %-9s [%s]", i, bc.uuid, bc.props);
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);
        displayManager.println(buf);
    }
    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("num=select  q=cancel");

    int sel = -1;
    while (sel < 0) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if (k >= '0' && k <= '9') { int n = k - '0'; if (n < s_writableCount) sel = n; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    BiChar& bc = s_svcs[s_writable[sel].svc].chars[s_writable[sel].chr];

    // Select fuzz mode
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("[FUZZ] mode:");
    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("[1] seq  0x00-0xFF (1-byte)");
    displayManager.println("[2] rand  random 1-4 bytes");
    displayManager.println("[3] boundary  0,1,7F,80,FE,FF");
    displayManager.println("[q] cancel");

    char modeKey = 0;
    while (!modeKey) {
        char k = inputHandler.getKeyboardInput();
        if (k == '1' || k == '2' || k == '3') modeKey = k;
        if (k == 'q' || k == 'Q') return;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Connect once, fuzz in loop
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Connecting for fuzz...");

    NimBLEDevice::init("");
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5000);   // v2.x: unit is ms
    if (!client->connect(NimBLEAddress(macStr, addrType))) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Connect failed.");
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    applyPairing(client);

    // Find the characteristic
    NimBLERemoteCharacteristic* target = nullptr;
    const auto& svcs = client->getServices(true);
    if (!svcs.empty()) {
        for (auto* svc : svcs) {
            const auto& chars = svc->getCharacteristics(true);
            if (chars.empty()) continue;
            for (auto* chr : chars) {
                char uuidBuf[12]; shortUuid(chr->getUUID().toString(), uuidBuf, sizeof(uuidBuf));
                if (strcmp(uuidBuf, bc.uuid) == 0 && chr->canWrite()) { target = chr; break; }
            }
            if (target) break;
        }
    }

    if (!target) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Char not found.");
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    // Build payload list
    const uint8_t boundary[] = { 0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF };
    int     total   = (modeKey == '1') ? 256 : (modeKey == '3') ? 6 : 64;
    int     sent    = 0;
    int     errors  = 0;
    uint32_t seed   = millis();

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();

    while (client->isConnected() && sent < total) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        uint8_t payload[4]; int plen = 1;
        if (modeKey == '1') {
            payload[0] = (uint8_t)sent;
        } else if (modeKey == '3') {
            payload[0] = boundary[sent % 6];
        } else {
            // LCG random
            seed = seed * 1664525 + 1013904223;
            plen = (seed & 3) + 1;
            for (int i = 0; i < plen; i++) {
                seed = seed * 1664525 + 1013904223;
                payload[i] = (uint8_t)(seed >> 24);
            }
        }

        bool ok = target->writeValue(payload, plen, false);
        if (!ok) errors++;
        sent++;

        // Redraw every 8 writes
        if (sent % 8 == 1 || sent == total) {
            displayManager.clearScreen();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(0x7BEF); displayManager.printText("[FUZZ] ");
            displayManager.setTextColor(TFT_YELLOW); displayManager.println(bc.uuid);
            displayManager.printSeparator();
            displayManager.setCursor(4, displayManager.getCursorY());
            char stat[48];
            snprintf(stat, sizeof(stat), "sent:%d/%d  err:%d", sent, total, errors);
            displayManager.setTextColor(errors ? TFT_RED : TFT_GREEN);
            displayManager.println(stat);
            // Show last payload
            char hex[16] = {};
            int show = plen < 4 ? plen : 4;
            for (int i = 0; i < show; i++) snprintf(hex + i*3, 4, "%02X ", payload[i]);
            displayManager.setTextColor(TFT_WHITE);
            char last[32]; snprintf(last, sizeof(last), "last: %s", hex);
            displayManager.println(last);
            displayManager.printSeparator();
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("[q] stop");
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    bool disconnected = !client->isConnected();
    client->disconnect();
    NimBLEDevice::deleteClient(client);

    displayManager.setCursor(4, displayManager.getCursorY());
    if (disconnected)
        displayManager.setTextColor(TFT_RED), displayManager.println("Device disconnected!");
    else
        displayManager.setTextColor(TFT_GREEN), displayManager.println("Fuzz complete.");
    char sum[40]; snprintf(sum, sizeof(sum), "sent:%d err:%d", sent, errors);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println(sum);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ── SD save helpers ───────────────────────────────────────────────────────────

// "aa:bb:cc:dd:ee:ff" → "aa-bb-cc-dd-ee-ff"
static void macToFilename(const char* mac, char* out, size_t outLen) {
    strncpy(out, mac, outLen - 1);
    out[outLen - 1] = '\0';
    for (char* p = out; *p; p++) if (*p == ':') *p = '-';
}

static bool saveGattToSd(const char* macStr) {
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir(SD_DIR_BLEINFO);

    char fname[40];
    char stem[18];
    macToFilename(macStr, stem, sizeof(stem));
    snprintf(fname, sizeof(fname), SD_DIR_BLEINFO "/%s.txt", stem);

    File f = SD.open(fname, FILE_WRITE);
    if (!f) return false;

    f.print("MAC: "); f.println(macStr);
    for (int i = 0; i < s_svcCount; i++) {
        f.println();
        // Use full UUID in saved file (display uses short/truncated version)
        const char* sUuid = s_svcs[i].fullUuid[0] ? s_svcs[i].fullUuid : s_svcs[i].uuid;
        if (s_svcs[i].name[0]) {
            f.print("SVC "); f.print(sUuid);
            f.print("  "); f.println(s_svcs[i].name);
        } else {
            f.print("SVC "); f.println(sUuid);
        }
        for (int j = 0; j < s_svcs[i].nChars; j++) {
            BiChar& c = s_svcs[i].chars[j];
            const char* cUuid = c.fullUuid[0] ? c.fullUuid : c.uuid;
            f.print("  "); f.print(cUuid);
            f.print(" ["); f.print(c.props); f.print("] ");
            // Write full hex dump if we have raw bytes, otherwise fall back to display value
            if (c.rawLen > 0) {
                for (int b = 0; b < c.rawLen; b++) {
                    char hx[4]; snprintf(hx, sizeof(hx), "%02X", c.rawVal[b]);
                    f.print(hx);
                    if (b < c.rawLen - 1) f.print(" ");
                }
                // Also show decoded label if we have one (e.g. DeviceName:"Polar")
                if (c.value[0]) { f.print("  // "); f.print(c.value); }
                f.println();
            } else if (c.value[0])      { f.println(c.value); }
            else if (c.udesc[0]) { f.print("("); f.print(c.udesc); f.println(")"); }
            else                 f.println();
        }
    }
    f.close();
    return true;
}

static bool saveSniffToSd(const char* macStr) {
    if (!sdCardManager.canAccessSD() || s_nfTotal == 0) return false;
    sdCardManager.ensureDir(SD_DIR_BLEINFO);

    char fname[46];
    char stem[18];
    macToFilename(macStr, stem, sizeof(stem));
    snprintf(fname, sizeof(fname), SD_DIR_BLEINFO "/%s_sniff.txt", stem);

    File f = SD.open(fname, FILE_APPEND);
    if (!f) return false;

    f.print("MAC: "); f.println(macStr);
    f.println("--- notify sniff ---");

    uint16_t tot    = s_nfTotal;
    uint8_t  actual = tot < BI_NOTIF_MAX ? (uint8_t)tot : BI_NOTIF_MAX;
    // Walk ring from oldest to newest
    uint8_t  oldest = (s_nfWrite - actual + BI_NOTIF_MAX) % BI_NOTIF_MAX;
    for (uint8_t i = 0; i < actual; i++) {
        uint8_t idx = (oldest + i) % BI_NOTIF_MAX;
        uint32_t e  = s_notifs[idx].elapsed;
        const char* uid = s_notifs[idx].fullUuid[0] ? s_notifs[idx].fullUuid : s_notifs[idx].uuid;
        // Write timestamp + UUID
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "  %2lu.%1lus %s  ", e / 1000, (e % 1000) / 100, uid);
        f.print(hdr);
        // Write full raw hex bytes
        for (int b = 0; b < s_notifs[idx].rawLen; b++) {
            char hx[4]; snprintf(hx, sizeof(hx), "%02X", s_notifs[idx].raw[b]);
            f.print(hx);
            if (b < s_notifs[idx].rawLen - 1) f.print(" ");
        }
        f.println();
    }
    f.println();
    f.close();
    return true;
}

// Replay file format (.ble):
//   TREX_BLE_REPLAY
//   MAC aa:bb:cc:dd:ee:ff
//   TYPE 1
//   PKT <uuid> <elapsed_ms> <HEXBYTES>
static bool saveReplayToSd(const char* macStr, uint8_t addrType) {
    if (!sdCardManager.canAccessSD() || s_nfTotal == 0) return false;
    sdCardManager.ensureDir(SD_DIR_BLEINFO);

    char fname[46], stem[18];
    macToFilename(macStr, stem, sizeof(stem));
    snprintf(fname, sizeof(fname), SD_DIR_BLEINFO "/%s_replay.ble", stem);

    File f = SD.open(fname, FILE_WRITE);
    if (!f) return false;

    f.println("TREX_BLE_REPLAY");
    f.print("MAC "); f.println(macStr);
    f.print("TYPE "); f.println(addrType);

    uint16_t tot    = s_nfTotal;
    uint8_t  actual = tot < BI_NOTIF_MAX ? (uint8_t)tot : BI_NOTIF_MAX;
    uint8_t  oldest = (s_nfWrite - actual + BI_NOTIF_MAX) % BI_NOTIF_MAX;

    for (uint8_t i = 0; i < actual; i++) {
        uint8_t  idx = (oldest + i) % BI_NOTIF_MAX;
        BiNotif& n   = s_notifs[idx];
        f.print("PKT ");
        f.print(n.uuid);
        f.print(" ");
        f.print(n.elapsed);
        f.print(" ");
        for (uint8_t b = 0; b < n.rawLen; b++) {
            char hx[3]; snprintf(hx, sizeof(hx), "%02X", n.raw[b]);
            f.print(hx);
        }
        f.println();
    }
    f.close();
    return true;
}

// ── Display ───────────────────────────────────────────────────────────────────

static void addLine(const char* text, uint16_t color) {
    if (s_lineCount >= BI_MAX_LINES) return;
    strncpy(s_lines[s_lineCount].text, text, 51);
    s_lines[s_lineCount].color = color;
    s_lineCount++;
}

static void buildLines() {
    s_lineCount = 0;
    char buf[52];
    for (int i = 0; i < s_svcCount; i++) {
        if (s_svcs[i].name[0])
            snprintf(buf, sizeof(buf), "SVC %-9s %s", s_svcs[i].uuid, s_svcs[i].name);
        else
            snprintf(buf, sizeof(buf), "SVC %s", s_svcs[i].uuid);
        addLine(buf, TFT_CYAN);

        for (int j = 0; j < s_svcs[i].nChars; j++) {
            BiChar& c = s_svcs[i].chars[j];
            const char* pfx = (c.risk >= 3) ? "! " : (c.risk == 2) ? "~ " : "  ";
            if (c.value[0])
                snprintf(buf, sizeof(buf), "%s%-9s [%-3s] %s", pfx, c.uuid, c.props, c.value);
            else if (c.udesc[0])
                snprintf(buf, sizeof(buf), "%s%-9s [%-3s] (%s)", pfx, c.uuid, c.props, c.udesc);
            else
                snprintf(buf, sizeof(buf), "%s%-9s [%-3s]", pfx, c.uuid, c.props);
            uint16_t col = (c.risk >= 3) ? TFT_RED :
                           (c.risk == 2) ? TFT_YELLOW :
                           (c.risk == 1) ? 0xFD20 :   // orange
                                           TFT_WHITE;
            addLine(buf, col);
        }
    }
}

static void renderPage(const char* mac, int page) {
    int totalPages = (s_lineCount + BI_PER_PAGE - 1) / BI_PER_PAGE;
    if (totalPages < 1) totalPages = 1;

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);

    displayManager.setTextColor(0x7BEF);     displayManager.printText("[GATT] ");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText(mac);
    char pg[8]; snprintf(pg, sizeof(pg), " %d/%d", page + 1, totalPages);
    displayManager.setTextColor(0x7BEF);     displayManager.println(pg);
    displayManager.printSeparator();

    int start = page * BI_PER_PAGE;
    int end   = start + BI_PER_PAGE < s_lineCount ? start + BI_PER_PAGE : s_lineCount;
    for (int i = start; i < end; i++) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(s_lines[i].color);
        displayManager.println(s_lines[i].text);
    }

    displayManager.printSeparator();
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    bool sdReady = sdCardManager.canAccessSD();
    // Build footer from available capabilities
    char foot[72] = "[q]qt [a/l]pg";
    if (s_hasNotify)   strncat(foot, " [n]sniff",   sizeof(foot) - strlen(foot) - 1);
    if (s_hasReplay)   strncat(foot, " [r]wcap",    sizeof(foot) - strlen(foot) - 1);
    if (s_hasWrite)    strncat(foot, " [w]wr[f]fz", sizeof(foot) - strlen(foot) - 1);
    if (s_hasRisk)     strncat(foot, " [b]audit",   sizeof(foot) - strlen(foot) - 1);
    if (sdReady)       strncat(foot, " [s]save",    sizeof(foot) - strlen(foot) - 1);
    if (s_pairEnabled) strncat(foot, " [p]PAIR ON", sizeof(foot) - strlen(foot) - 1);
    else               strncat(foot, " [p]pair",    sizeof(foot) - strlen(foot) - 1);
    displayManager.println(foot);
}

// ── Notify sniffer ────────────────────────────────────────────────────────────

static void runSniff(const char* macStr, uint8_t addrType) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("Reconnecting for sniff...");

    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5000);   // v2.x: unit is ms

    if (!client->connect(NimBLEAddress(macStr, addrType))) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Reconnect failed.");
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    applyPairing(client);

    // Subscribe to every notify characteristic
    int subCount = 0;
    const auto& services = client->getServices(true);
    if (!services.empty()) {
        for (auto* svc : services) {
            const auto& chars = svc->getCharacteristics(true);
            if (chars.empty()) continue;
            for (auto* chr : chars) {
                if (chr->canNotify()) {
                    chr->subscribe(true, onNotify);
                    subCount++;
                } else if (chr->canIndicate()) {
                    chr->subscribe(false, onNotify);
                    subCount++;
                }
            }
        }
    }

    if (subCount == 0) {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No notify chars found.");
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }

    // Reset ring buffer
    s_nfTotal  = 0;
    s_nfWrite  = 0;
    s_nfFlag   = false;
    s_sniffT0  = millis();

    bool     needDraw = true;
    uint32_t lastDraw = 0;

    while (client->isConnected() && millis() - s_sniffT0 < BI_SNIFF_MS) {
        if (s_nfFlag || needDraw || millis() - lastDraw > 500) {
            s_nfFlag  = false;
            needDraw  = false;
            lastDraw  = millis();

            uint32_t elapsed   = millis() - s_sniffT0;
            uint32_t remaining = (BI_SNIFF_MS - elapsed) / 1000;

            displayManager.clearScreen();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(0x7BEF);    displayManager.printText("[SNIFF] ");
            displayManager.setTextColor(TFT_YELLOW); displayManager.printText(macStr);
            char tmr[8]; snprintf(tmr, sizeof(tmr), " %02lu:%02lu",
                                  remaining / 60, remaining % 60);
            displayManager.setTextColor(TFT_GREEN);  displayManager.println(tmr);
            displayManager.printSeparator();

            // Show last BI_SNIFF_LINES entries from ring buffer
            uint16_t tot    = s_nfTotal;
            uint8_t  actual = tot < BI_NOTIF_MAX ? (uint8_t)tot : BI_NOTIF_MAX;
            uint8_t  show   = actual < BI_SNIFF_LINES ? actual : BI_SNIFF_LINES;
            uint8_t  wrt    = s_nfWrite;
            uint8_t  rstart = (wrt - show + BI_NOTIF_MAX) % BI_NOTIF_MAX;

            if (show == 0) {
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(0x7BEF);
                displayManager.println("Waiting for notifications...");
            }
            for (uint8_t i = 0; i < show; i++) {
                uint8_t idx = (rstart + i) % BI_NOTIF_MAX;
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(TFT_WHITE);
                char line[52];
                uint32_t e = s_notifs[idx].elapsed;
                snprintf(line, sizeof(line), " %2lu.%1lus %-9s %s",
                         e / 1000, (e % 1000) / 100,
                         s_notifs[idx].uuid, s_notifs[idx].value);
                displayManager.println(line);
            }

            displayManager.printSeparator();
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            char foot[64];
            snprintf(foot, sizeof(foot), "subs:%d total:%lu [w]write [q]stop",
                     subCount, (unsigned long)s_nfTotal);
            displayManager.println(foot);
        }
        vTaskDelay(pdMS_TO_TICKS(50));

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        // [w] — write to a char while staying connected and subscribed
        if ((k == 'w' || k == 'W') && s_hasWrite && s_writableCount > 0) {
            // Show char picker
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(0x7BEF);
            displayManager.println("[WRITE] select char:");
            displayManager.printSeparator();
            for (uint8_t i = 0; i < s_writableCount; i++) {
                BiChar& bc = s_svcs[s_writable[i].svc].chars[s_writable[i].chr];
                char buf[52];
                snprintf(buf, sizeof(buf), "[%d] %-9s [%s]", i, bc.uuid, bc.props);
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(TFT_WHITE);
                displayManager.println(buf);
            }
            displayManager.printSeparator();
            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("num=select  q=cancel");

            int sel = -1;
            while (sel < 0) {
                char sk = inputHandler.getKeyboardInput();
                if (sk == 'q' || sk == 'Q') { sel = -2; break; }
                if (sk >= '0' && sk <= '9') {
                    int n = sk - '0';
                    if (n < s_writableCount) sel = n;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            if (sel >= 0) {
                BiChar& bc = s_svcs[s_writable[sel].svc].chars[s_writable[sel].chr];
                displayManager.clearScreen();
                displayManager.setDefaultTextSize();
                displayManager.setCursor(4, outputY);
                displayManager.setTextColor(TFT_CYAN);
                char hdr[48]; snprintf(hdr, sizeof(hdr), "Write to %s", bc.uuid);
                displayManager.println(hdr);
                displayManager.printSeparator();
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(0x7BEF);
                displayManager.println("Hex or ASCII. q=cancel");
                displayManager.printSeparator();

                uint8_t payload[20]; int payLen = readHexInput(payload, sizeof(payload));
                if (payLen > 0) {
                    // Write using the existing connected client — no reconnect
                    bool sent = false;
                    const auto& svcs2 = client->getServices(false); // cached, no re-discover
                    if (!svcs2.empty()) {
                        for (auto* svc : svcs2) {
                            const auto& chars2 = svc->getCharacteristics(false);
                            if (chars2.empty()) continue;
                            for (auto* chr : chars2) {
                                char uuidBuf[12];
                                shortUuid(chr->getUUID().toString(), uuidBuf, sizeof(uuidBuf));
                                if (strcmp(uuidBuf, bc.uuid) == 0) {
                                    if (chr->canWrite())
                                        sent = chr->writeValue(payload, payLen, true);
                                    if (!sent && chr->canWriteNoResponse())
                                        sent = chr->writeValue(payload, payLen, false);
                                    break;
                                }
                            }
                            if (sent) break;
                        }
                    }
                    displayManager.setTextColor(sent ? TFT_GREEN : TFT_RED);
                    displayManager.println(sent ? "Sent! Watch sniff for reply." : "Write failed.");
                    vTaskDelay(pdMS_TO_TICKS(800));
                }
            }
            // Resume sniff display
            needDraw = true;
        }
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    if (s_nfTotal > 0) s_hasReplay = true;

    // Auto-save sniff log + replay file to SD if any notifications were captured
    if (s_nfTotal > 0 && sdCardManager.canAccessSD()) {
        bool ok1 = saveSniffToSd(macStr);
        bool ok2 = saveReplayToSd(macStr, addrType);
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        char sfn[22]; macToFilename(macStr, sfn, sizeof(sfn));
        char msg[52];
        snprintf(msg, sizeof(msg), "Saved: %s_sniff.txt", sfn);
        if (ok1) displayManager.println(msg);
        snprintf(msg, sizeof(msg), "Saved: %s_replay.ble", sfn);
        if (ok2) displayManager.println(msg);
        vTaskDelay(pdMS_TO_TICKS(1400));
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runBleInfo(char* arg) {
    ensureBiBuffers();
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("BLE GATT Info");

    if (!arg || !arg[0]) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Usage: bi <index | mac>");
        displayManager.println("  bi 0              (index from sbl)");
        displayManager.println("  bi aa:bb:cc:dd:ee:ff");
        displayManager.printCommandScreen();
        return;
    }

    char    macStr[18] = {};
    uint8_t addrType   = BLE_ADDR_RANDOM;

    // "bi all" — enumerate every device from last sbl scan, save each to SD
    if (strcmp(arg, "all") == 0) {
        if (s_bleCount == 0) {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("No scan data. Run sbl first.");
            displayManager.printCommandScreen();
            return;
        }
        for (int di = 0; di < (int)s_bleCount; di++) {
            displayManager.clearScreen();
            displayManager.setDefaultTextSize();
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_CYAN);
            char hdr[44]; snprintf(hdr, sizeof(hdr), "[%d/%d] %.17s",
                                   di + 1, (int)s_bleCount, s_bleDevices[di].addr);
            displayManager.println(hdr);

            NimBLEDevice::init("");
            NimBLEDevice::getScan()->stop();   // v2.x: ensure scanner idle before connecting
            NimBLEClient* client = NimBLEDevice::createClient();
            client->setConnectTimeout(4000);   // v2.x: unit is ms (was seconds in v1.x)
            bool ok = client->connect(NimBLEAddress(s_bleDevices[di].addr,
                                                     s_bleDevices[di].addrType));
            if (ok) {
                enumerate(client);
                client->disconnect();
                if (s_svcCount > 0 && sdCardManager.canAccessSD())
                    saveGattToSd(s_bleDevices[di].addr);
            }
            NimBLEDevice::deleteClient(client);

            displayManager.setCursor(4, displayManager.getCursorY());
            displayManager.setTextColor(ok ? TFT_GREEN : TFT_RED);
            char status[40];
            snprintf(status, sizeof(status), ok ? "ok (%d svcs)" : "failed", s_svcCount);
            displayManager.println(status);

            // Check for abort
            if (inputHandler.getKeyboardInput() == 'q') break;
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Done. Results in /apps/bleinfo/");
        vTaskDelay(pdMS_TO_TICKS(2000));
        displayManager.printCommandScreen();
        return;
    }

    bool isIdx = true;
    for (char* p = arg; *p; p++) if (!isdigit((unsigned char)*p)) { isIdx = false; break; }

    if (isIdx) {
        int idx = atoi(arg);
        if (s_bleCount == 0) {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("No scan data. Run sbl first.");
            displayManager.printCommandScreen();
            return;
        }
        if (idx < 0 || idx >= (int)s_bleCount) {
            displayManager.setTextColor(TFT_RED);
            char buf[40]; snprintf(buf, sizeof(buf), "Index %d out of range (0-%d)",
                                   idx, (int)s_bleCount - 1);
            displayManager.println(buf);
            displayManager.printCommandScreen();
            return;
        }
        strncpy(macStr, s_bleDevices[idx].addr, 17);
        addrType = s_bleDevices[idx].addrType;
    } else {
        strncpy(macStr, arg, 17);
    }
    macStr[17] = '\0';

    // Connect
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    char msg[44]; snprintf(msg, sizeof(msg), "Connecting to %.17s ...", macStr);
    displayManager.println(msg);

    NimBLEDevice::init("");
    NimBLEDevice::getScan()->stop();   // v2.x: ensure scanner idle before connecting
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5000);   // v2.x: unit is ms (was seconds in v1.x)

    bool connected = client->connect(NimBLEAddress(macStr, addrType));
    if (!connected && !isIdx)
        connected = client->connect(NimBLEAddress(macStr, BLE_ADDR_PUBLIC));

    if (!connected) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Connection failed.");
        NimBLEDevice::deleteClient(client);
        displayManager.printCommandScreen();
        return;
    }

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Connected. Enumerating GATT...");

    enumerate(client);

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    if (s_svcCount == 0) {
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No GATT services found.");
        displayManager.printCommandScreen();
        return;
    }

    buildLines();
    int page       = 0;
    int totalPages = (s_lineCount + BI_PER_PAGE - 1) / BI_PER_PAGE;
    if (totalPages < 1) totalPages = 1;

    while (true) {
        renderPage(macStr, page);
        while (true) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (page < totalPages - 1) { page++; break; } }
            if (k == 'a' || k == 'A') { if (page > 0)              { page--; break; } }
            if ((k == 'n' || k == 'N') && s_hasNotify) {
                runSniff(macStr, addrType);
                break;
            }
            if ((k == 'r' || k == 'R') && s_hasReplay) {
                runReplay(macStr, addrType);
                break;
            }
            if ((k == 'b' || k == 'B') && s_hasRisk) {
                runAudit();
                break;
            }
            if ((k == 'w' || k == 'W') && s_hasWrite) {
                runWrite(macStr, addrType);
                break;
            }
            if ((k == 'f' || k == 'F') && s_hasWrite) {
                runFuzz(macStr, addrType);
                break;
            }
            if (k == 'p' || k == 'P') {
                s_pairEnabled = !s_pairEnabled;
                break; // re-render footer to show new state
            }
            if (k == 's' || k == 'S') {
                bool ok = saveGattToSd(macStr);
                // Show 1-line status then re-render
                displayManager.printSeparator();
                displayManager.setCursor(4, displayManager.getCursorY());
                displayManager.setTextColor(ok ? TFT_GREEN : TFT_RED);
                if (ok) {
                    char sfn[22]; macToFilename(macStr, sfn, sizeof(sfn));
                    char msg[48]; snprintf(msg, sizeof(msg), "Saved: %s.txt", sfn);
                    displayManager.println(msg);
                } else {
                    displayManager.println("Save failed (no SD card?)");
                }
                vTaskDelay(pdMS_TO_TICKS(1200));
                break;
            }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
