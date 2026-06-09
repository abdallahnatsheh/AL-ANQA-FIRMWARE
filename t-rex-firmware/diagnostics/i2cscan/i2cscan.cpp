// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// [EXPERIMENTAL] i2cscan — I2C bus scanner / register explorer

#include "i2cscan.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include "lockscreen_manager.h"
#include "utilities.h"
#include <Wire.h>
#include <SD.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── chip table ────────────────────────────────────────────────────────────────
struct I2cChip { uint8_t addr; const char* name; const char* type; };
static const I2cChip CHIPS[] = {
    { 0x18, "ES8311 audio",    "audio"  },
    { 0x19, "LIS3DH accel",    "accel"  },
    { 0x1C, "MMA8452Q accel",  "accel"  },
    { 0x1D, "ADXL345 accel",   "accel"  },
    { 0x1E, "HMC5883L mag",    "mag"    },
    { 0x20, "PCF8574/MCP23017","gpio"   },
    { 0x27, "PCF8574T",        "gpio"   },
    { 0x28, "WS1850S RFID2",    "rfid"   },
    { 0x29, "VL53L0X/TCS34725","sensor" },
    { 0x34, "AXP2101 PMIC",    "power"  },
    { 0x3C, "SSD1306 OLED",    "disp"   },
    { 0x3D, "SSD1306 OLED",    "disp"   },
    { 0x40, "ES7210/INA219",   "audio"  },
    { 0x44, "SHT31/OPT3001",   "sensor" },
    { 0x45, "SHT31",           "sensor" },
    { 0x48, "ADS1115/TMP102",  "sensor" },
    { 0x49, "ADS1115",         "sensor" },
    { 0x50, "AT24Cxx EEPROM",  "mem"    },
    { 0x51, "AT24Cxx EEPROM",  "mem"    },
    { 0x52, "AT24Cxx EEPROM",  "mem"    },
    { 0x53, "ADXL345/EEPROM",  "mem"    },
    { 0x54, "AT24Cxx EEPROM",  "mem"    },
    { 0x55, "Keyboard MCU",    "input"  },
    { 0x56, "AT24Cxx EEPROM",  "mem"    },
    { 0x57, "AT24C32/MAX30100", "mem"    },
    { 0x5A, "MLX90614 IR",     "sensor" },
    { 0x5B, "MPR121 touch",    "touch"  },
    { 0x5C, "MPR121 touch",    "touch"  },
    { 0x5D, "GT911 trackpad",   "input"  },
    { 0x60, "MPL3115A2",       "sensor" },
    { 0x68, "DS3231/MPU6050",  "sensor" },
    { 0x69, "MPU6050/MPU6886", "sensor" },
    { 0x70, "QMP6988",         "sensor" },
    { 0x73, "PAJ7620 gesture", "sensor" },
    { 0x76, "BME280/BMP280",   "sensor" },
    { 0x77, "BME280/BMP280",   "sensor" },
};
static const int CHIPS_N = (int)(sizeof(CHIPS) / sizeof(CHIPS[0]));

static const char* chipName(uint8_t addr) {
    for (int i = 0; i < CHIPS_N; i++)
        if (CHIPS[i].addr == addr) return CHIPS[i].name;
    return "unknown";
}
static const char* chipType(uint8_t addr) {
    for (int i = 0; i < CHIPS_N; i++)
        if (CHIPS[i].addr == addr) return CHIPS[i].type;
    return "???";
}

// ── type → accent color ───────────────────────────────────────────────────────
static uint16_t typeColor(const char* t) {
    if (!t) return 0x7BEF;
    if (strcmp(t, "input")  == 0) return TFT_CYAN;
    if (strcmp(t, "audio")  == 0) return 0xFD20;   // orange
    if (strcmp(t, "power")  == 0) return TFT_YELLOW;
    if (strcmp(t, "sensor") == 0) return TFT_GREEN;
    if (strcmp(t, "accel")  == 0) return 0x07FF;   // sky blue
    if (strcmp(t, "mag")    == 0) return 0x801F;   // purple
    if (strcmp(t, "mem")    == 0) return 0x9CD3;   // steel blue
    if (strcmp(t, "disp")   == 0) return 0xF81F;   // magenta
    if (strcmp(t, "touch")  == 0) return 0xFFE0;   // yellow-white
    if (strcmp(t, "gpio")   == 0) return 0x04FF;   // teal
    if (strcmp(t, "rfid")   == 0) return 0xF800;   // red — NFC/RFID
    return 0x7BEF;                                  // grey = unknown
}

// T-Deck built-in I2C chips
static bool isBuiltin(uint8_t addr) {
    return addr == 0x55   // keyboard MCU
        || addr == 0x18   // ES8311 audio codec
        || addr == 0x34   // AXP2101 PMIC (Plus)
        || addr == 0x40   // ES7210 mic ADC (Plus)
        || addr == 0x5D;  // GT911 trackpad (Plus)
}

// ── scan state ────────────────────────────────────────────────────────────────
#define ISC_MAX    20
#define ISC_ROWS    7
#define RY(n)      (outputY + (n) * LINE_HEIGHT)

// Column x positions (6px/char)
static const int16_t CX_SEL  =   4;
static const int16_t CX_IDX  =  10;
static const int16_t CX_ADDR =  22;
static const int16_t CX_NAME =  52;   // chip name 14 ch = 84px → ends x=136
static const int16_t CX_TYPE = 142;   // type 6 ch = 36px → ends x=178
static const int16_t CX_TAG  = 184;   // [BUILTIN] 9ch

static uint8_t  s_found[ISC_MAX];
static bool     s_alive[ISC_MAX];     // ACK status per device (live-updated)
static int      s_count      = 0;
static int      s_sel        = 0;
static char     s_status[52] = "";
static uint16_t s_statusClr  = TFT_GREEN;

// Detail pane cache
static int      s_detailFor  = -1;
static bool     s_detailOk   = false;
static bool     s_detailRaw  = false; // true = got bytes via raw (no reg-ptr)
static uint8_t  s_detailBytes[4] = {0};

// ── probe ─────────────────────────────────────────────────────────────────────
static bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// ── refresh detail for selected device ───────────────────────────────────────
// Two-path read:
//   Path 1 (reg-ptr): write 0x00, then requestFrom — works on most chips
//   Path 2 (raw):     requestFrom without reg write — for GT911 / stream chips
static void refreshDetail(int idx) {
    if (idx < 0 || idx >= s_count) return;
    if (idx == s_detailFor) return;
    s_detailFor  = idx;
    s_detailOk   = false;
    s_detailRaw  = false;
    uint8_t addr = s_found[idx];

    // Path 1: standard reg-pointer read
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    bool regOk = (Wire.endTransmission(false) == 0);
    if (regOk) {
        uint8_t got = Wire.requestFrom(addr, (uint8_t)4);
        if (got > 0) {
            for (int i = 0; i < 4; i++)
                s_detailBytes[i] = (i < got) ? Wire.read() : 0xFF;
            s_detailOk  = true;
            s_detailRaw = false;
            return;
        }
    }

    // Path 2: raw stream read (no register pointer)
    uint8_t got = Wire.requestFrom(addr, (uint8_t)4);
    if (got > 0) {
        for (int i = 0; i < 4; i++)
            s_detailBytes[i] = (i < got) ? Wire.read() : 0xFF;
        s_detailOk  = true;
        s_detailRaw = true;
    }
}

// ── re-probe all found devices and update s_alive[] ──────────────────────────
static void verifyAll() {
    for (int i = 0; i < s_count; i++)
        s_alive[i] = probe(s_found[i]);
    s_detailFor = -1;   // invalidate detail cache
    if (s_count > 0) refreshDetail(s_sel);
}

// ── scan bus with progress bar + live found list ─────────────────────────────
static void doScan() {
    auto& dm = displayManager;
    s_count     = 0;
    s_sel       = 0;
    s_detailFor = -1;
    s_status[0] = '\0';

    dm.clearScreen();
    dm.setDefaultTextSize();

    dm.setCursor(4, RY(0));
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("I2CSCAN");
    dm.setTextColor(0x7BEF);   dm.printText("]");
    char buf[36];
    snprintf(buf, sizeof(buf), "  SDA:%d  SCL:%d", BOARD_I2C_SDA, BOARD_I2C_SCL);
    dm.setTextColor(TFT_WHITE); dm.printText(buf);

    dm.setCursor(4, RY(1)); dm.printSeparator();

    dm.setCursor(4, RY(2));
    dm.setTextColor(TFT_YELLOW);
    dm.printText("[EXPERIMENTAL] Not field-tested.");

    dm.setCursor(4, RY(3));
    dm.setTextColor(0x7BEF);
    dm.printText("Scanning 0x08-0x77...");

    const int TOTAL = 0x77 - 0x08 + 1;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        int done = addr - 0x08 + 1;
        int fill = done * 26 / TOTAL;

        char bar[40];
        bar[0] = '[';
        for (int i = 0; i < 26; i++) bar[1+i] = (i < fill) ? '=' : ' ';
        bar[27] = ']';
        snprintf(bar+28, sizeof(bar)-28, " %3d%%", done * 100 / TOTAL);

        dm.fillRect(0, RY(4), SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(4, RY(4));
        dm.setTextColor(TFT_GREEN); dm.printText(bar);

        if (probe(addr) && s_count < ISC_MAX) {
            s_found[s_count] = addr;
            s_alive[s_count] = true;   // just ACKed
            s_count++;

            // Live found list — external devices shown brighter
            dm.fillRect(0, RY(5), SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            dm.setCursor(4, RY(5));
            dm.setTextColor(0x7BEF); dm.printText("Found: ");
            for (int j = 0; j < s_count; j++) {
                char ab[8];
                if (isBuiltin(s_found[j])) {
                    snprintf(ab, sizeof(ab), "0x%02X ", s_found[j]);
                    dm.setTextColor(0x7BEF);   // grey = expected, always there
                } else {
                    snprintf(ab, sizeof(ab), "0x%02X*", s_found[j]);
                    dm.setTextColor(TFT_GREEN); // green + * = external device!
                }
                dm.printText(ab);
            }
        }
        dm.flushSPI();
    }
}

// ── draw one device row ───────────────────────────────────────────────────────
static void drawDeviceRow(int16_t ry, int idx, bool sel) {
    auto& dm  = displayManager;
    uint8_t addr  = s_found[idx];
    bool    alive = s_alive[idx];
    const char* type = chipType(addr);
    uint16_t tc  = typeColor(type);

    if (sel) dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);

    // Selector
    dm.setCursor(CX_SEL, ry);
    dm.setTextColor(TFT_CYAN);
    dm.printText(sel ? ">" : " ");

    // Index
    char ibuf[4]; snprintf(ibuf, sizeof(ibuf), "%d", idx + 1);
    dm.setCursor(CX_IDX, ry);
    dm.setTextColor(sel ? TFT_WHITE : 0x7BEF);
    dm.printText(ibuf);

    // Address — green if alive, red if dead (after [v] verify)
    char abuf[6]; snprintf(abuf, sizeof(abuf), "0x%02X", addr);
    dm.setCursor(CX_ADDR, ry);
    dm.setTextColor(alive ? TFT_GREEN : TFT_RED);
    dm.printText(abuf);

    // Chip name — colored by type
    char nbuf[16]; snprintf(nbuf, sizeof(nbuf), "%-14s", chipName(addr));
    dm.setCursor(CX_NAME, ry);
    dm.setTextColor(sel ? TFT_WHITE : tc);
    dm.printText(nbuf);

    // Type tag — always type color
    char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%-6s", type);
    dm.setCursor(CX_TYPE, ry);
    dm.setTextColor(sel ? TFT_YELLOW : tc);
    dm.printText(tbuf);

    // Tag: [EXT] in bright green for Grove/external, [BUILTIN] in grey for T-Deck internals
    dm.setCursor(CX_TAG, ry);
    if (isBuiltin(addr)) {
        dm.setTextColor(0x4A49);          // subtle dark grey
        dm.printText("[BUILTIN]");
    } else {
        dm.setTextColor(TFT_GREEN);       // bright green — something is plugged in!
        dm.printText("[EXT]");
    }
}

// ── draw scan results screen ──────────────────────────────────────────────────
static void drawScan(int page) {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    int totalPages = max(1, (s_count + ISC_ROWS - 1) / ISC_ROWS);
    int start      = page * ISC_ROWS;
    int end        = min(start + ISC_ROWS, s_count);

    if (s_count > 0 && s_sel >= s_count) s_sel = s_count - 1;

    // Row 0: header — count split into built-in vs external
    int extCount = 0;
    for (int i = 0; i < s_count; i++) if (!isBuiltin(s_found[i])) extCount++;
    int builtinCount = s_count - extCount;

    dm.setCursor(4, RY(0));
    dm.setTextColor(0x7BEF);   dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("I2CSCAN");
    dm.setTextColor(0x7BEF);   dm.printText("] ");
    // Built-in count in grey
    char bstr[16]; snprintf(bstr, sizeof(bstr), "%d built-in", builtinCount);
    dm.setTextColor(0x7BEF); dm.printText(bstr);
    // External count — green if >0, grey if 0
    char estr[12]; snprintf(estr, sizeof(estr), " + %d ext", extCount);
    dm.setTextColor(extCount > 0 ? TFT_GREEN : 0x4A49);
    dm.printText(estr);

    // SD indicator
    dm.setCursor(278, RY(0));
    if (sdCardManager.isReady()) {
        dm.setTextColor(TFT_GREEN); dm.printText("[SD]");
    } else {
        dm.setCursor(256, RY(0));
        dm.setTextColor(TFT_RED);   dm.printText("[NO SD]");
    }

    // Row 1: separator
    dm.setCursor(4, RY(1)); dm.printSeparator();

    // Row 2: column headers
    dm.setCursor(CX_IDX,  RY(2)); dm.setTextColor(0x7BEF); dm.printText("#");
    dm.setCursor(CX_ADDR, RY(2)); dm.printText("ADDR");
    dm.setCursor(CX_NAME, RY(2)); dm.printText("CHIP");
    dm.setCursor(CX_TYPE, RY(2)); dm.printText("TYPE");
    if (totalPages > 1) {
        char pbuf[10]; snprintf(pbuf, sizeof(pbuf), "p%d/%d", page+1, totalPages);
        dm.setCursor(CX_TAG, RY(2));
        dm.setTextColor(TFT_WHITE); dm.printText(pbuf);
    }

    // Rows 3-9: devices
    if (s_count == 0) {
        dm.setCursor(4, RY(3));
        dm.setTextColor(0x7BEF);
        dm.printText("No devices found. [f] to rescan.");
    } else {
        for (int i = start; i < end; i++)
            drawDeviceRow(RY(3 + (i - start)), i, (i == s_sel));
    }

    // Row 10: separator
    dm.setCursor(4, RY(10)); dm.printSeparator();

    // Row 11: detail pane
    dm.setCursor(4, RY(11));
    if (s_status[0]) {
        dm.setTextColor(s_statusClr);
        dm.printText(s_status);
    } else if (s_count > 0 && s_detailFor == s_sel) {
        uint8_t addr = s_found[s_sel];
        if (s_detailOk) {
            char det[52];
            const char* prefix = s_detailRaw ? "raw:" : " r0:";
            snprintf(det, sizeof(det),
                     "0x%02X %s %02X %02X %02X %02X  [r]regs [d]dmp [w]write",
                     addr, prefix,
                     s_detailBytes[0], s_detailBytes[1],
                     s_detailBytes[2], s_detailBytes[3]);
            dm.setTextColor(s_detailRaw ? TFT_YELLOW : TFT_WHITE);
            dm.printText(det);
        } else {
            char det[44];
            snprintf(det, sizeof(det), "0x%02X no read response — try [r]", addr);
            dm.setTextColor(0x7BEF); dm.printText(det);
        }
    } else if (s_count > 0) {
        char det[44];
        snprintf(det, sizeof(det), "0x%02X  %s  [p]probe  [v]verify all",
                 s_found[s_sel], chipName(s_found[s_sel]));
        dm.setTextColor(0x7BEF); dm.printText(det);
    }

    // Row 12: separator
    dm.setCursor(4, RY(12)); dm.printSeparator();

    // Row 13: footer
    dm.setCursor(4, RY(13));
    dm.setTextColor(0x7BEF);
    dm.printText("[pad]sel [CLICK/r]regs [d]dmp [p]probe [v]verify [s]sv [f]scan [q]");
}

// ── hex input dialog (1-2 hex chars + Enter, Esc to cancel) ──────────────────
static bool hexDialog(int16_t y, const char* label, uint8_t* out) {
    auto& dm = displayManager;
    char buf[3] = "";
    int  len    = 0;

    while (true) {
        dm.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
        dm.setCursor(4, y);
        dm.setTextColor(TFT_CYAN);  dm.printText(label);
        dm.setTextColor(TFT_WHITE); dm.printText(buf);
        dm.printText("_");
        dm.flushSPI();

        char k = inputHandler.getKeyboardInput();
        if (!k) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        if (k == 27) return false;

        if ((k == '\n' || k == '\r') && len > 0) {
            *out = (uint8_t)strtol(buf, nullptr, 16);
            return true;
        }
        if (k == '\b' && len > 0) { buf[--len] = '\0'; }
        if (len < 2 &&
            ((k >= '0' && k <= '9') ||
             (k >= 'a' && k <= 'f') ||
             (k >= 'A' && k <= 'F'))) {
            buf[len++] = k; buf[len] = '\0';
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// ── navigable register browser ────────────────────────────────────────────────
// Reads all 256 bytes upfront (16-byte raw fallback if reg-ptr fails).
// 16 pages × 16 bytes = 256 regs.  Trackpad/a/l pages.  [w] write.
static void doRegBrowser(uint8_t addr) {
    auto& dm = displayManager;

    // Read all 256 registers — try reg-pointer mode first
    uint8_t regs[256];
    memset(regs, 0xFF, sizeof(regs));
    bool    regPtrOk = false;

    // Attempt reg-pointer bulk read in 32-byte chunks
    for (int base = 0; base < 256; base += 32) {
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)base);
        if (Wire.endTransmission(false) == 0) {
            uint8_t got = Wire.requestFrom((uint8_t)addr, (uint8_t)32);
            for (uint8_t i = 0; i < got && (base+i) < 256; i++) {
                regs[base+i] = Wire.read();
                if (base == 0) regPtrOk = true;
            }
        }
    }

    // If reg-ptr failed, try raw stream read for as many bytes as possible
    bool rawMode = !regPtrOk;
    if (rawMode) {
        memset(regs, 0xFF, sizeof(regs));
        uint8_t got = Wire.requestFrom(addr, (uint8_t)32);
        for (uint8_t i = 0; i < got; i++) regs[i] = Wire.read();
    }

    int  page        = 0;
    bool needDraw    = true;
    char writeResult[40] = "";

    while (true) {
        if (needDraw) {
            dm.clearScreen();
            dm.setDefaultTextSize();

            int pageBase = page * 16;

            // Row 0: header
            dm.setCursor(4, RY(0));
            dm.setTextColor(0x7BEF);     dm.printText("[");
            dm.setTextColor(TFT_CYAN);   dm.printText("REGS");
            dm.setTextColor(0x7BEF);     dm.printText("]");
            char hbuf[44];
            snprintf(hbuf, sizeof(hbuf), "  0x%02X  %s", addr, chipName(addr));
            dm.setTextColor(typeColor(chipType(addr))); dm.printText(hbuf);

            // Mode tag + page indicator
            dm.setCursor(230, RY(0));
            if (rawMode) { dm.setTextColor(TFT_YELLOW); dm.printText("[RAW]"); }
            char pbuf[10]; snprintf(pbuf, sizeof(pbuf), " p%02d/16", page+1);
            dm.setTextColor(0x7BEF); dm.printText(pbuf);

            // Row 1: reg range
            dm.setCursor(4, RY(1));
            dm.setTextColor(0x7BEF);
            char rl[36];
            snprintf(rl, sizeof(rl), "Reg  0x%02X - 0x%02X   (type: %s)",
                     pageBase, pageBase+15, chipType(addr));
            dm.printText(rl);

            // Row 2: separator
            dm.setCursor(4, RY(2)); dm.printSeparator();

            // Rows 3-4: 2 × 8 bytes = 16 bytes visible
            for (int row = 0; row < 2; row++) {
                int off = pageBase + row * 8;
                char line[48];
                snprintf(line, sizeof(line),
                         "%02X: %02X %02X %02X %02X  %02X %02X %02X %02X",
                         off,
                         regs[off],   regs[off+1], regs[off+2], regs[off+3],
                         regs[off+4], regs[off+5], regs[off+6], regs[off+7]);
                dm.setCursor(4, RY(3+row));
                dm.setTextColor(TFT_WHITE); dm.printText(line);
            }

            // Row 5: separator
            dm.setCursor(4, RY(5)); dm.printSeparator();

            // Row 6: chip-specific hint (page-aware)
            dm.setCursor(4, RY(6));
            dm.setTextColor(TFT_YELLOW);
            if (addr == 0x68 && page == 0)
                dm.printText("DS3231: sec min hr day date mon yr");
            else if ((addr == 0x76||addr == 0x77) && page == 0xD)
                dm.printText("BME280: 0xD0=chip_id (expect 0x60)");
            else if (addr >= 0x50 && addr <= 0x57)
                dm.printText("EEPROM: raw stored data");
            else if (addr == 0x5D)
                dm.printText("GT911: 16-bit regs — [RAW] = stream bytes only");
            else if (rawMode)
                dm.printText("RAW mode: no reg-ptr ack. Stream bytes shown.");
            else
                dm.printText("[w] write reg   [a/l] or pad to page");

            // Row 7: write result
            if (writeResult[0]) {
                dm.setCursor(4, RY(7));
                dm.setTextColor(TFT_GREEN); dm.printText(writeResult);
            }

            // Row 10: separator
            dm.setCursor(4, RY(10)); dm.printSeparator();

            // Row 11: footer
            dm.setCursor(4, RY(11));
            dm.setTextColor(0x7BEF);
            dm.printText("[pad/a/l]page  [w]write  [q]back");

            needDraw = false;
        }

        char           k  = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();

        if (k == 'q' || k == 'Q') break;

        if ((tb == TBALL_DOWN || k == 'l' || k == 'L') && page < 15) {
            page++; writeResult[0] = '\0'; needDraw = true;
        }
        if ((tb == TBALL_UP   || k == 'a' || k == 'A') && page > 0) {
            page--; writeResult[0] = '\0'; needDraw = true;
        }

        if ((k == 'w' || k == 'W') && !rawMode) {
            uint8_t wreg = 0, wval = 0;
            if (hexDialog(RY(8), "Write reg 0x: ", &wreg) &&
                hexDialog(RY(9), "Value   0x: ",   &wval)) {
                Wire.beginTransmission(addr);
                Wire.write(wreg); Wire.write(wval);
                uint8_t err = Wire.endTransmission();
                if (err == 0) {
                    snprintf(writeResult, sizeof(writeResult),
                             "Write OK: reg 0x%02X = 0x%02X", wreg, wval);
                    // Small delay — some chips need time to commit the write
                    vTaskDelay(pdMS_TO_TICKS(10));
                    // Re-read the affected 16-byte page
                    page = wreg / 16;
                    int base = page * 16;
                    Wire.beginTransmission(addr);
                    Wire.write((uint8_t)base);
                    if (Wire.endTransmission(false) == 0) {
                        uint8_t got = Wire.requestFrom((uint8_t)addr, (uint8_t)16);
                        for (uint8_t i = 0; i < got && (base+i) < 256; i++)
                            regs[base+i] = Wire.read();
                    }
                } else {
                    snprintf(writeResult, sizeof(writeResult),
                             "Write FAIL err:%d", err);
                }
            }
            needDraw = true;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── compact full hex dump — 128 bytes/page, 2 pages ──────────────────────────
static void doHexDump(uint8_t addr) {
    auto& dm = displayManager;

    uint8_t regs[256];
    memset(regs, 0xFF, sizeof(regs));
    for (int base = 0; base < 256; base += 32) {
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)base);
        if (Wire.endTransmission(false) != 0) continue; // skip chunk, keep going
        uint8_t got = Wire.requestFrom((uint8_t)addr, (uint8_t)32);
        for (uint8_t i = 0; i < got && (base+i) < 256; i++)
            regs[base+i] = Wire.read();
    }

    int  page     = 0;
    bool needDraw = true;

    while (true) {
        if (needDraw) {
            dm.clearScreen(); dm.setDefaultTextSize();

            dm.setCursor(4, RY(0));
            dm.setTextColor(0x7BEF);     dm.printText("[");
            dm.setTextColor(TFT_CYAN);   dm.printText("DUMP");
            dm.setTextColor(0x7BEF);     dm.printText("]");
            char hbuf[44];
            snprintf(hbuf, sizeof(hbuf), "  0x%02X  %s   p%d/2",
                     addr, chipName(addr), page+1);
            dm.setTextColor(typeColor(chipType(addr))); dm.printText(hbuf);

            dm.setCursor(4, RY(1));
            dm.setTextColor(0x7BEF);
            dm.printText("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");

            dm.setCursor(4, RY(2)); dm.printSeparator();

            int base = page * 8 * 16;
            for (int row = 0; row < 8; row++) {
                int off = base + row * 16;
                char line[58];
                snprintf(line, sizeof(line),
                    "%02X:  %02X %02X %02X %02X %02X %02X %02X %02X"
                    " %02X %02X %02X %02X %02X %02X %02X %02X",
                    off,
                    regs[off],    regs[off+1],  regs[off+2],  regs[off+3],
                    regs[off+4],  regs[off+5],  regs[off+6],  regs[off+7],
                    regs[off+8],  regs[off+9],  regs[off+10], regs[off+11],
                    regs[off+12], regs[off+13], regs[off+14], regs[off+15]);
                dm.setCursor(4, RY(3+row)); dm.setTextColor(TFT_WHITE);
                dm.printText(line);
            }

            dm.setCursor(4, RY(11)); dm.printSeparator();
            dm.setCursor(4, RY(12));
            dm.setTextColor(0x7BEF); dm.printText("[a/l]page  [q]back");
            needDraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if ((k == 'l' || k == 'L') && page < 1) { page++; needDraw = true; }
        if ((k == 'a' || k == 'A') && page > 0) { page--; needDraw = true; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── save to SD ────────────────────────────────────────────────────────────────
static void saveResults() {
    if (!sdCardManager.isReady()) {
        snprintf(s_status, sizeof(s_status), "ERR: SD not ready");
        s_statusClr = TFT_RED; return;
    }
    if (!SD.exists("/logs") && !SD.mkdir("/logs")) {
        snprintf(s_status, sizeof(s_status), "ERR: cannot create /logs");
        s_statusClr = TFT_RED; return;
    }
    // FILE_APPEND creates the file if absent and never truncates existing data
    File f = SD.open("/logs/i2cscan.csv", FILE_APPEND);
    if (!f) {
        snprintf(s_status, sizeof(s_status), "ERR: open failed");
        s_statusClr = TFT_RED; return;
    }

    char ts[24];
    ClockManager::instance().getTimestamp(ts, sizeof(ts));
    if (!ts[0]) snprintf(ts, sizeof(ts), "@%lums", millis());

    for (int i = 0; i < s_count; i++) {
        char line[72];
        snprintf(line, sizeof(line), "%s,0x%02X,%s,%s,%s",
                 ts, s_found[i],
                 chipName(s_found[i]), chipType(s_found[i]),
                 s_alive[i] ? "ACK" : "DEAD");
        f.println(line);
    }
    f.flush(); f.close();

    snprintf(s_status, sizeof(s_status), "Saved %d dev -> /logs/i2cscan.csv", s_count);
    s_statusClr = TFT_GREEN;
}

// ── non-interactive: isc r <addr> <reg> [len] ────────────────────────────────
static void cmdRead(char* args) {
    auto& dm = displayManager;
    unsigned int addr = 0, reg = 0, len = 16;
    int parsed = sscanf(args, "%x %x %u", &addr, &reg, &len);
    if (parsed < 2) {
        dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Usage: isc r <addr_hex> <reg_hex> [len]");
        return;
    }
    if (len < 1 || len > 32) len = 16;

    uint8_t data[32]; memset(data, 0xFF, sizeof(data));
    Wire.beginTransmission((uint8_t)addr);
    Wire.write((uint8_t)reg);
    bool ok = (Wire.endTransmission(false) == 0);
    if (ok) {
        uint8_t got = Wire.requestFrom((uint8_t)addr, (uint8_t)len);
        for (uint8_t i = 0; i < got; i++) data[i] = Wire.read();
    }

    dm.clearScreen(); dm.setDefaultTextSize();
    char hbuf[52];
    snprintf(hbuf, sizeof(hbuf), "READ 0x%02X  reg:0x%02X  len:%u  %s",
             addr, reg, len, chipName(addr));
    dm.setCursor(4, RY(0)); dm.setTextColor(TFT_CYAN); dm.println(hbuf);
    dm.printSeparator();
    if (!ok) { dm.setTextColor(TFT_RED); dm.println("No ACK from device."); return; }
    for (unsigned int row = 0; row < (len+7)/8; row++) {
        char line[52]; unsigned int base = row * 8;
        int pos = snprintf(line, sizeof(line), "%02X: ", (unsigned)(reg+base));
        for (unsigned int j = 0; j < 8 && (base+j) < len; j++)
            pos += snprintf(line+pos, sizeof(line)-pos, " %02X", data[base+j]);
        dm.setTextColor(TFT_WHITE); dm.println(line);
    }
}

// ── non-interactive: isc raw <addr> <len> ────────────────────────────────────
static void cmdRaw(char* args) {
    auto& dm = displayManager;
    unsigned int addr = 0, len = 16;
    if (sscanf(args, "%x %u", &addr, &len) < 1) {
        dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Usage: isc raw <addr_hex> [len]");
        return;
    }
    if (len < 1 || len > 32) len = 16;

    uint8_t data[32]; memset(data, 0xFF, sizeof(data));
    uint8_t got = Wire.requestFrom((uint8_t)addr, (uint8_t)len);

    dm.clearScreen(); dm.setDefaultTextSize();
    char hbuf[48];
    snprintf(hbuf, sizeof(hbuf), "RAW READ 0x%02X  len:%u  %s", addr, len, chipName(addr));
    dm.setCursor(4, RY(0)); dm.setTextColor(TFT_YELLOW); dm.println(hbuf);
    dm.printSeparator();

    if (got == 0) { dm.setTextColor(TFT_RED); dm.println("No bytes returned."); return; }
    for (uint8_t i = 0; i < got; i++) data[i] = Wire.read();
    for (unsigned int row = 0; row < (got+7)/8; row++) {
        char line[52]; unsigned int base = row * 8;
        int pos = snprintf(line, sizeof(line), "%02X: ", base);
        for (unsigned int j = 0; j < 8 && (base+j) < got; j++)
            pos += snprintf(line+pos, sizeof(line)-pos, " %02X", data[base+j]);
        dm.setTextColor(TFT_WHITE); dm.println(line);
    }
}

// ── non-interactive: isc w <addr> <reg> <val> ────────────────────────────────
static void cmdWrite(char* args) {
    auto& dm = displayManager;
    unsigned int addr = 0, reg = 0, val = 0;
    if (sscanf(args, "%x %x %x", &addr, &reg, &val) < 3) {
        dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Usage: isc w <addr_hex> <reg_hex> <val_hex>");
        return;
    }
    Wire.beginTransmission((uint8_t)addr);
    Wire.write((uint8_t)reg); Wire.write((uint8_t)val);
    uint8_t err = Wire.endTransmission();
    dm.setCursor(4, outputY);
    if (err == 0) {
        char msg[56];
        snprintf(msg, sizeof(msg), "OK  0x%02X  reg:0x%02X = 0x%02X  (%s)",
                 addr, reg, val, chipName(addr));
        dm.setTextColor(TFT_GREEN); dm.println(msg);
    } else {
        char msg[32];
        snprintf(msg, sizeof(msg), "FAIL  Wire err:%u", err);
        dm.setTextColor(TFT_RED); dm.println(msg);
    }
}

// ── interactive main loop ─────────────────────────────────────────────────────
static void runInteractive() {
    doScan();
    if (s_count > 0) refreshDetail(0);

    int  page     = 0;
    bool needDraw = true;

    while (true) {
        if (needDraw) {
            int totalPages = max(1, (s_count + ISC_ROWS - 1) / ISC_ROWS);
            if (page >= totalPages) page = totalPages - 1;
            drawScan(page);
            needDraw = false;
        }

        char           k  = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();

        if (k == 'q' || k == 'Q') break;

        // Trackpad navigation — auto-flip page, refresh detail
        if (tb == TBALL_DOWN && s_count > 0 && s_sel < s_count - 1) {
            s_sel++;
            s_status[0] = '\0';
            int np = s_sel / ISC_ROWS; if (np != page) page = np;
            refreshDetail(s_sel);
            needDraw = true;
        }
        if (tb == TBALL_UP && s_count > 0 && s_sel > 0) {
            s_sel--;
            s_status[0] = '\0';
            int np = s_sel / ISC_ROWS; if (np != page) page = np;
            refreshDetail(s_sel);
            needDraw = true;
        }

        // Trackpad click / [r] = register browser
        if ((tb == TBALL_CLICK || k == 'r' || k == 'R') && s_count > 0) {
            s_detailFor = -1;
            doRegBrowser(s_found[s_sel]);
            refreshDetail(s_sel);
            needDraw = true;
        }

        // [d] = full hex dump
        if ((k == 'd' || k == 'D') && s_count > 0) {
            doHexDump(s_found[s_sel]);
            needDraw = true;
        }

        // [p] = re-probe selected
        if ((k == 'p' || k == 'P') && s_count > 0) {
            s_alive[s_sel] = probe(s_found[s_sel]);
            s_detailFor = -1;
            refreshDetail(s_sel);
            if (s_detailOk) {
                snprintf(s_status, sizeof(s_status),
                         "0x%02X ACK:%s  r0: %02X %02X %02X %02X",
                         s_found[s_sel],
                         s_alive[s_sel] ? "OK" : "DEAD",
                         s_detailBytes[0], s_detailBytes[1],
                         s_detailBytes[2], s_detailBytes[3]);
            } else {
                snprintf(s_status, sizeof(s_status),
                         "0x%02X ACK:%s (no reg read)",
                         s_found[s_sel],
                         s_alive[s_sel] ? "OK" : "DEAD");
            }
            s_statusClr = s_alive[s_sel] ? TFT_GREEN : TFT_RED;
            needDraw = true;
        }

        // [v] = verify all devices, update s_alive[]
        if (k == 'v' || k == 'V') {
            verifyAll();
            int alive = 0;
            for (int i = 0; i < s_count; i++) if (s_alive[i]) alive++;
            snprintf(s_status, sizeof(s_status),
                     "Verify: %d/%d alive", alive, s_count);
            s_statusClr = (alive == s_count) ? TFT_GREEN : TFT_YELLOW;
            needDraw = true;
        }

        // Page navigation with [a]/[l]
        if ((k == 'l' || k == 'L') && s_count > 0) {
            int tp = max(1, (s_count + ISC_ROWS - 1) / ISC_ROWS);
            if (page < tp - 1) { page++; s_sel = page * ISC_ROWS; refreshDetail(s_sel); needDraw = true; }
        }
        if ((k == 'a' || k == 'A') && s_count > 0) {
            if (page > 0) { page--; s_sel = page * ISC_ROWS; refreshDetail(s_sel); needDraw = true; }
        }

        // [s] = save
        if (k == 's' || k == 'S') { saveResults(); needDraw = true; }

        // [f] = rescan
        if (k == 'f' || k == 'F') {
            s_status[0] = '\0';
            doScan();
            if (s_count > 0) refreshDetail(0);
            page = 0; needDraw = true;
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) needDraw = true;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── command entry point ───────────────────────────────────────────────────────
void runI2cScan(char* args) {
    if (!args || !args[0]) {
        runInteractive();
        displayManager.printCommandScreen();
        return;
    }

    char  cmd  = args[0];
    char* rest = (args[1] == ' ' && args[2]) ? args + 2 : args + 1;

    if (cmd == 'r' || cmd == 'R') {
        cmdRead(rest);
        displayManager.printCommandScreen();
    } else if (cmd == 'w' || cmd == 'W') {
        cmdWrite(rest);
        displayManager.printCommandScreen();
    } else if (cmd == 'd' || cmd == 'D') {
        unsigned int addr = 0;
        if (sscanf(rest, "%x", &addr) == 1) {
            displayManager.clearScreen();
            doHexDump((uint8_t)addr);
            displayManager.printCommandScreen();
        } else {
            displayManager.setCursor(4, outputY);
            displayManager.setTextColor(TFT_RED);
            displayManager.println("Usage: isc d <addr_hex>");
        }
    } else if (strncmp(args, "raw", 3) == 0) {
        char* r = (args[3] == ' ' && args[4]) ? args + 4 : args + 3;
        cmdRaw(r);
        displayManager.printCommandScreen();
    } else {
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Usage: isc [r|w|d|raw] <args>  or: isc");
    }
}
