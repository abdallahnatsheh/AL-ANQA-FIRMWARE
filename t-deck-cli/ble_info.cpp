#include "ble_info.h"
#include "bluetooth_functions.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
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
    char    uuid[12];
    char    props[6];    // R/W/N/I/- flags
    char    value[22];
    char    udesc[14];   // 0x2901 User Description
    uint8_t fmtType;     // 0x2904 format type (0=unknown)
    int8_t  fmtExp;      // 0x2904 exponent
};

struct BiSvc {
    char    uuid[12];
    char    name[16];
    BiChar  chars[BI_MAX_CHARS];
    uint8_t nChars;
};

static BiSvc   s_svcs[BI_MAX_SVCS];
static uint8_t s_svcCount;
static bool    s_hasNotify;
static bool    s_hasWrite;
static bool    s_pairEnabled = false;   // [p] toggles; persists for session

// Writable char index table: (svc, chr) pairs, max 16
struct BiWriteRef { uint8_t svc; uint8_t chr; };
static BiWriteRef s_writable[16];
static uint8_t    s_writableCount;

struct BiLine { char text[52]; uint16_t color; };
static BiLine s_lines[BI_MAX_LINES];
static int    s_lineCount;

// Notification ring buffer (written from NimBLE task, read from main task)
struct BiNotif { char uuid[12]; char value[22]; uint32_t elapsed; };
static BiNotif           s_notifs[BI_NOTIF_MAX];
static volatile uint16_t s_nfTotal = 0;
static volatile uint8_t  s_nfWrite = 0;
static volatile bool     s_nfFlag  = false;
static uint32_t          s_sniffT0 = 0;

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
    std::string val((char*)data, len < 20 ? len : 20);
    fmtVal(val, s_notifs[idx].value, sizeof(s_notifs[idx].value));
    s_notifs[idx].elapsed = millis() - s_sniffT0;
    s_nfWrite = (s_nfWrite + 1) % BI_NOTIF_MAX;
    s_nfTotal++;
    s_nfFlag = true;
}

// ── Enumeration ───────────────────────────────────────────────────────────────

static void enumerate(NimBLEClient* client) {
    s_svcCount      = 0;
    s_hasNotify     = false;
    s_hasWrite      = false;
    s_writableCount = 0;
    auto* services = client->getServices(true);
    if (!services) return;

    for (auto* svc : *services) {
        if (s_svcCount >= BI_MAX_SVCS) break;
        BiSvc& bs = s_svcs[s_svcCount++];
        memset(&bs, 0, sizeof(bs));

        shortUuid(svc->getUUID().toString(), bs.uuid, sizeof(bs.uuid));
        uint16_t su = (svc->getUUID().bitSize() == 16)
                      ? svc->getUUID().getNative()->u16.value : 0;
        const char* sn = su ? svcName(su) : nullptr;
        if (sn) strncpy(bs.name, sn, sizeof(bs.name) - 1);

        auto* chars = svc->getCharacteristics(true);
        if (!chars) continue;

        for (auto* chr : *chars) {
            if (bs.nChars >= BI_MAX_CHARS) break;
            BiChar& bc = bs.chars[bs.nChars++];
            memset(&bc, 0, sizeof(bc));

            shortUuid(chr->getUUID().toString(), bc.uuid, sizeof(bc.uuid));

            uint8_t pi = 0;
            if (chr->canRead())     bc.props[pi++] = 'R';
            if (chr->canWrite())  {
                bc.props[pi++] = 'W'; s_hasWrite = true;
                if (s_writableCount < 16) {
                    s_writable[s_writableCount++] = { (uint8_t)(s_svcCount - 1),
                                                       bs.nChars };
                }
            }
            if (chr->canNotify())   { bc.props[pi++] = 'N'; s_hasNotify = true; }
            if (chr->canIndicate()) { bc.props[pi++] = 'I'; s_hasNotify = true; }
            if (pi == 0)            bc.props[pi++] = '-';
            bc.props[pi] = '\0';

            uint16_t cu = (chr->getUUID().bitSize() == 16)
                          ? chr->getUUID().getNative()->u16.value : 0;
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
    uint32_t onPassKeyRequest() override {
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
        return (uint32_t)atol(buf);
    }
    bool onConfirmPIN(uint32_t pin) override {
        // Numeric comparison — show pin, ask y/n
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);
        char msg[32]; snprintf(msg, sizeof(msg), "Confirm PIN %06lu? [y/n]", (unsigned long)pin);
        displayManager.println(msg);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'y' || k == 'Y') return true;
            if (k == 'n' || k == 'N') return false;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
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

// ── Write support ─────────────────────────────────────────────────────────────

// Read up to 20 hex bytes from keyboard; returns byte count or -1 on cancel.
// Prompt already shown by caller. Input: "DE AD BE EF" or "DEADBEEF" or "hello"
static int readHexInput(uint8_t* buf, size_t bufLen) {
    char line[48] = {};
    int  pos = 0;

    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("> ");

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (!k) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (k == '\r' || k == '\n') break;
        if (k == '\b' || k == 0x7F) {
            if (pos > 0) {
                line[--pos] = '\0';
                // Redraw input line
                displayManager.fillRect(4, displayManager.getCursorY() - LINE_HEIGHT,
                                        SCREEN_WIDTH - 4, LINE_HEIGHT, TFT_BLACK);
                displayManager.setCursor(4, displayManager.getCursorY() - LINE_HEIGHT);
                displayManager.setTextColor(TFT_WHITE);
                char redraw[52]; snprintf(redraw, sizeof(redraw), "> %s", line);
                displayManager.printText(redraw);
            }
            continue;
        }
        if (k == 'q' || k == 'Q') return -1;
        if (pos < (int)sizeof(line) - 1) {
            line[pos++] = k; line[pos] = '\0';
            char ch[2] = { k, '\0' };
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printText(ch);
        }
    }

    // Try to decode: if all printable ASCII, treat as raw string
    bool allPrint = (pos > 0);
    for (int i = 0; i < pos; i++) if (!isprint((unsigned char)line[i])) { allPrint = false; break; }

    // Strip spaces and try hex decode
    char hex[48] = {}; int hi = 0;
    for (int i = 0; i < pos; i++) if (line[i] != ' ') hex[hi++] = line[i];

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
    int n = pos < (int)bufLen ? pos : (int)bufLen;
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
    displayManager.setCursor(4, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.println("Connecting...");

    NimBLEDevice::init("");
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5);
    if (!client->connect(NimBLEAddress(macStr, addrType))) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Connect failed.");
        NimBLEDevice::deleteClient(client);
        vTaskDelay(pdMS_TO_TICKS(1500));
        return;
    }
    applyPairing(client);

    // Find the characteristic by UUID
    bool sent = false;
    auto* svcs = client->getServices(true);
    if (svcs) {
        for (auto* svc : *svcs) {
            auto* chars = svc->getCharacteristics(true);
            if (!chars) continue;
            for (auto* chr : *chars) {
                char uuidBuf[12]; shortUuid(chr->getUUID().toString(), uuidBuf, sizeof(uuidBuf));
                if (strcmp(uuidBuf, bc.uuid) == 0 && chr->canWrite()) {
                    sent = chr->writeValue(payload, payLen, true);
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
    client->setConnectTimeout(5);
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
    auto* svcs = client->getServices(true);
    if (svcs) {
        for (auto* svc : *svcs) {
            auto* chars = svc->getCharacteristics(true);
            if (!chars) continue;
            for (auto* chr : *chars) {
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
    sdCardManager.ensureDir("/logs/bleinfo");

    char fname[40];
    char stem[18];
    macToFilename(macStr, stem, sizeof(stem));
    snprintf(fname, sizeof(fname), "/logs/bleinfo/%s.txt", stem);

    File f = SD.open(fname, FILE_WRITE);
    if (!f) return false;

    f.print("MAC: "); f.println(macStr);
    for (int i = 0; i < s_svcCount; i++) {
        f.println();
        if (s_svcs[i].name[0]) {
            f.print("SVC "); f.print(s_svcs[i].uuid);
            f.print("  "); f.println(s_svcs[i].name);
        } else {
            f.print("SVC "); f.println(s_svcs[i].uuid);
        }
        for (int j = 0; j < s_svcs[i].nChars; j++) {
            BiChar& c = s_svcs[i].chars[j];
            f.print("  "); f.print(c.uuid);
            f.print(" ["); f.print(c.props); f.print("] ");
            if (c.value[0])      f.println(c.value);
            else if (c.udesc[0]) { f.print("("); f.print(c.udesc); f.println(")"); }
            else                 f.println();
        }
    }
    f.close();
    return true;
}

static bool saveSniffToSd(const char* macStr) {
    if (!sdCardManager.canAccessSD() || s_nfTotal == 0) return false;
    sdCardManager.ensureDir("/logs/bleinfo");

    char fname[46];
    char stem[18];
    macToFilename(macStr, stem, sizeof(stem));
    snprintf(fname, sizeof(fname), "/logs/bleinfo/%s_sniff.txt", stem);

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
        char line[52];
        snprintf(line, sizeof(line), "  %2lu.%1lus %-9s %s",
                 e / 1000, (e % 1000) / 100,
                 s_notifs[idx].uuid, s_notifs[idx].value);
        f.println(line);
    }
    f.println();
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
            if (c.value[0])
                snprintf(buf, sizeof(buf), "  %-9s [%-3s] %s", c.uuid, c.props, c.value);
            else if (c.udesc[0])
                snprintf(buf, sizeof(buf), "  %-9s [%-3s] (%s)", c.uuid, c.props, c.udesc);
            else
                snprintf(buf, sizeof(buf), "  %-9s [%-3s]", c.uuid, c.props);
            addLine(buf, TFT_WHITE);
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
    char foot[64] = "[q]qt [a/l]pg";
    if (s_hasNotify)   strncat(foot, " [n]sniff",  sizeof(foot) - strlen(foot) - 1);
    if (s_hasWrite)    strncat(foot, " [w]wr[f]fz", sizeof(foot) - strlen(foot) - 1);
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

    NimBLEDevice::init("");
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5);

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
    auto* services = client->getServices(true);
    if (services) {
        for (auto* svc : *services) {
            auto* chars = svc->getCharacteristics(true);
            if (!chars) continue;
            for (auto* chr : *chars) {
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
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

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
            char foot[48];
            snprintf(foot, sizeof(foot), "subs:%d  total:%lu  [q]stop",
                     subCount, (unsigned long)s_nfTotal);
            displayManager.println(foot);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    // Auto-save sniff log to SD if any notifications were captured
    if (s_nfTotal > 0 && sdCardManager.canAccessSD()) {
        saveSniffToSd(macStr);
        displayManager.setCursor(4, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        char sfn[22]; macToFilename(macStr, sfn, sizeof(sfn));
        char msg[48]; snprintf(msg, sizeof(msg), "Saved: %s_sniff.txt", sfn);
        displayManager.println(msg);
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

void runBleInfo(char* arg) {
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
            NimBLEClient* client = NimBLEDevice::createClient();
            client->setConnectTimeout(4);
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
        displayManager.println("Done. Results in /logs/bleinfo/");
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
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5);

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
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (page < totalPages - 1) { page++; break; } }
            if (k == 'a' || k == 'A') { if (page > 0)              { page--; break; } }
            if ((k == 'n' || k == 'N') && s_hasNotify) {
                runSniff(macStr, addrType);
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
