// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "espsniff.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include <WiFi.h>
#include <SD.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <time.h>

extern DisplayManager    displayManager;
extern InputHandling     inputHandler;
extern SDCardManager     sdCardManager;

// ─── constants ────────────────────────────────────────────────────────────────
#define ES_MAX_ENTRIES   50
#define ES_RAW_MAX       300   // max raw 802.11 frame bytes (ESP-NOW action < 280)
#define ES_ROWS_PER_PAGE 5
#define ES_HOP_MS        300
#define ES_HEX_ROWS      6
#define ES_HEX_BPR       8    // bytes per hex row in detail view
#define ES_AUTO_SAVE_AT  40   // auto-save when this many unsaved frames accumulate

static const uint8_t HOP_SEQ[] = { 1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 5, 10 };

// ─── view states ──────────────────────────────────────────────────────────────
enum EsState { STATE_LIST, STATE_DETAIL };

// ─── captured frame — stores full raw 802.11 frame for PCAP ──────────────────
struct EsEntry {
    uint8_t  srcMAC[6];
    uint8_t  dstMAC[6];
    uint8_t  channel;
    uint8_t  payloadType;   // 0=raw  1=espchat  2=espvoice
    uint8_t  seq;
    uint16_t rawLen;
    uint8_t  raw[ES_RAW_MAX];   // complete 802.11 frame; payload starts at raw+30
    uint32_t timestamp;         // millis() at capture time
    int8_t   rssi;
};

// payload helpers (ESP-NOW body starts 30 bytes into the 802.11 action frame)
static inline const uint8_t* esPayload(const EsEntry& e)    { return e.raw + 30; }
static inline int             esPayloadLen(const EsEntry& e) { return e.rawLen > 30 ? e.rawLen - 30 : 0; }

// ─── ISR-volatile sniffer state ───────────────────────────────────────────────
static EsEntry           s_buf[ES_MAX_ENTRIES];
static volatile uint8_t  s_head   = 0;
static volatile uint8_t  s_count  = 0;
static volatile bool     s_active = false;
static volatile uint8_t  s_channel= 1;
static volatile uint32_t s_total  = 0;   // total frames ever captured

// ─── main-loop state ──────────────────────────────────────────────────────────
static bool     s_hopping = true;
static uint8_t  s_hopIdx  = 0;
static uint32_t s_lastHop = 0;

// ─── session / SD state (set before promiscuous starts) ───────────────────────
static char     s_csvPath[48]  = {};
static char     s_pcapPath[48] = {};
static uint16_t s_sessionNum   = 0;
static bool     s_fileOk       = false;
static uint8_t  s_savedCount   = 0;   // frames written to SD so far this session
static uint32_t s_sessionStartMs = 0;
static time_t   s_sessionEpoch   = 0; // 0 if ClockManager not valid at session start

// ─── helpers ──────────────────────────────────────────────────────────────────
static void macStr(const uint8_t* m, char* out18) {
    snprintf(out18, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static bool macIsBcast(const uint8_t* m) {
    return m[0]==0xFF && m[1]==0xFF && m[2]==0xFF &&
           m[3]==0xFF && m[4]==0xFF && m[5]==0xFF;
}

static void shortMac(const uint8_t* m, char* out9) {
    if (macIsBcast(m)) strncpy(out9, "BCAST   ", 9);
    else snprintf(out9, 9, "%02X:%02X:%02X", m[3], m[4], m[5]);
}

static int physSlot(int idx) {
    if (s_count < ES_MAX_ENTRIES) return idx;
    return ((int)s_head + idx) % ES_MAX_ENTRIES;
}

static const char* typeName(uint8_t t) {
    switch (t) { case 1: return "CHAT"; case 2: return "VOIC"; default: return "RAW "; }
}

// frame absolute datetime string for CSV ("" when clock not available)
static void frameDateTime(const EsEntry& e, char* out24) {
    out24[0] = '\0';
    if (s_sessionEpoch == 0) return;
    time_t ft = s_sessionEpoch + (long)(e.timestamp - s_sessionStartMs) / 1000;
    struct tm* ti = localtime(&ft);
    strftime(out24, 24, "%Y-%m-%d %H:%M:%S", ti);
}

// ─── ESP-NOW recv callback ────────────────────────────────────────────────────
// Promiscuous mode on ESP32-S3 never delivers ESP-NOW action frames (binary blob
// handles them before the promiscuous path). ESP-NOW recv callback is the only
// reliable way to capture them. We reconstruct a synthetic 802.11 action frame
// so the PCAP output stays valid.
static void IRAM_ATTR espNowSniffCb(const uint8_t* mac, const uint8_t* data, int len) {
    if (!s_active) return;

    EsEntry& e = s_buf[s_head];
    memcpy(e.srcMAC, mac, 6);
    memset(e.dstMAC, 0xFF, 6);   // broadcast (ESP-NOW API doesn't give us DA)
    e.channel   = s_channel;
    e.rssi      = -99;            // not available via ESP-NOW API on IDF 4.x
    e.timestamp = millis();

    // build synthetic 802.11 action frame so PCAP stays valid
    uint8_t frame[30];
    frame[0] = 0xD0; frame[1] = 0x00;               // Frame Control: action
    frame[2] = 0x00; frame[3] = 0x00;               // Duration
    memset(frame +  4, 0xFF, 6);                      // DA = BCAST
    memcpy(frame + 10, mac,  6);                      // SA
    memcpy(frame + 16, mac,  6);                      // BSSID = SA (no AP)
    frame[22] = 0x00; frame[23] = 0x00;              // Seq
    frame[24] = 0x7F;                                 // Category: vendor-specific
    frame[25] = 0x18; frame[26] = 0xFE; frame[27] = 0x34; // OUI
    frame[28] = 0x04;                                 // ESP-NOW type
    frame[29] = 0x00;                                 // reserved

    int bodyLen  = (len < (ES_RAW_MAX - 30)) ? len : (ES_RAW_MAX - 30);
    e.rawLen = (uint16_t)(30 + bodyLen);
    memcpy(e.raw,      frame, 30);
    if (bodyLen > 0) memcpy(e.raw + 30, data, bodyLen);

    const uint8_t* pl  = esPayload(e);
    int            plen = esPayloadLen(e);
    e.payloadType = (plen > 0 && pl[0] == 0x01) ? 1 : (plen > 0 && pl[0] == 0x02) ? 2 : 0;
    e.seq = (plen >= 2 && e.payloadType != 0) ? pl[1] : 0;

    s_head = (s_head + 1) % ES_MAX_ENTRIES;
    if (s_count < ES_MAX_ENTRIES) s_count++;
    s_total++;
}

// ─── promiscuous callback (core 0 WiFi task) ─────────────────────────────────
static void IRAM_ATTR snifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    // ESP-NOW action frames arrive as WIFI_PKT_MISC on some ESP32-S3 IDF builds
    if (!s_active || (type != WIFI_PKT_MGMT && type != WIFI_PKT_MISC)) return;
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pkt->payload;
    uint16_t       len = pkt->rx_ctrl.sig_len;

    // ESP-NOW: Action frame 0xD0, vendor-specific 0x7F, OUI 18:FE:34, type 0x04
    if (len < 30) return;
    if (d[0] != 0xD0) return;
    if (d[24]!=0x7F || d[25]!=0x18 || d[26]!=0xFE || d[27]!=0x34 || d[28]!=0x04) return;

    EsEntry& e = s_buf[s_head];

    // store full raw 802.11 frame for PCAP
    e.rawLen = (len < ES_RAW_MAX) ? len : ES_RAW_MAX;
    memcpy(e.raw, d, e.rawLen);

    memcpy(e.srcMAC, d + 10, 6);
    memcpy(e.dstMAC, d + 4,  6);
    e.channel   = s_channel;
    e.rssi      = pkt->rx_ctrl.rssi;
    e.timestamp = millis();

    uint16_t plen = (uint16_t)esPayloadLen(e);
    const uint8_t* pl = esPayload(e);
    e.payloadType = (plen>0 && pl[0]==0x01) ? 1 : (plen>0 && pl[0]==0x02) ? 2 : 0;
    e.seq = (plen>=2 && e.payloadType!=0) ? pl[1] : 0;

    s_head = (s_head + 1) % ES_MAX_ENTRIES;
    if (s_count < ES_MAX_ENTRIES) s_count++;
    s_total++;
}

// ─── session init — called BEFORE WiFi/promiscuous (GDMA rule) ───────────────
static void initSession() {
    s_sessionStartMs = millis();
    s_sessionEpoch   = ClockManager::instance().isValid() ? time(nullptr) : 0;
    s_savedCount     = 0;
    s_fileOk         = false;

    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir(SD_DIR_ESPSNIFF);

    // find next free NNN — never overwrite existing sessions
    uint16_t num = 1;
    while (num < 999) {
        char probe[48];
        snprintf(probe, sizeof(probe), SD_DIR_ESPSNIFF "/%03u.csv", num);
        if (!SD.exists(probe)) break;
        num++;
    }
    s_sessionNum = num;
    snprintf(s_csvPath,  sizeof(s_csvPath),  SD_DIR_ESPSNIFF "/%03u.csv",  num);
    snprintf(s_pcapPath, sizeof(s_pcapPath), SD_DIR_ESPSNIFF "/%03u.pcap", num);

    // create CSV with header
    File csv = SD.open(s_csvPath, FILE_WRITE);
    if (csv) {
        csv.printf("# ESPSNIFF SESSION %03u  uptime=%lus\n",
                   num, (unsigned long)(s_sessionStartMs / 1000));
        char ts[22] = "";
        ClockManager::instance().getTimestamp(ts, sizeof(ts));
        if (ts[0]) csv.printf("# started %s (local)\n", ts);
        csv.print("frame,datetime,session_ms,src_mac,dst_mac,channel,rssi_dbm,len,type,seq,hex_payload,decoded\n");
        csv.close();
    }

    // create PCAP with global header — linktype 127 = LINKTYPE_IEEE802_11_RADIOTAP
    File pcap = SD.open(s_pcapPath, FILE_WRITE);
    if (pcap) {
        struct __attribute__((packed)) {
            uint32_t magic    = 0xa1b2c3d4;
            uint16_t vmaj     = 2;
            uint16_t vmin     = 4;
            int32_t  tz       = 0;
            uint32_t sig      = 0;
            uint32_t snap     = 65535;
            uint32_t linktype = 127;
        } ghdr;
        pcap.write((uint8_t*)&ghdr, sizeof(ghdr));
        pcap.close();
    }

    s_fileOk = true;
}

// ─── write new frames to CSV + PCAP (pauses promiscuous — GDMA rule) ─────────
static int doSave(bool isAuto) {
    uint8_t cnt = s_count;
    if (!s_fileOk || s_savedCount >= cnt) return 0;

    s_active = false;
    esp_wifi_set_promiscuous(false);

    File csv  = SD.open(s_csvPath,  FILE_APPEND);
    File pcap = SD.open(s_pcapPath, FILE_APPEND);

    int written = 0;
    for (int i = s_savedCount; i < (int)cnt; i++) {
        const EsEntry& e = s_buf[physSlot(i)];
        const uint8_t* pl  = esPayload(e);
        int            plen = esPayloadLen(e);

        // ── CSV row ───────────────────────────────────────────────────────────
        if (csv) {
            char src[18], dst[18], dtBuf[24] = {};
            macStr(e.srcMAC, src);
            macStr(e.dstMAC, dst);
            frameDateTime(e, dtBuf);
            uint32_t sesMs = e.timestamp - s_sessionStartMs;

            // full payload hex
            char* hexBuf = (char*)malloc(plen * 2 + 2);
            if (hexBuf) {
                hexBuf[0] = '\0';
                for (int j = 0; j < plen; j++)
                    snprintf(hexBuf + j*2, 3, "%02X", pl[j]);
            }

            // decoded text (CHAT only)
            char decoded[52] = {};
            if (e.payloadType == 1 && plen > 2) {
                int tlen = min(plen - 2, 48);
                memcpy(decoded, pl + 2, tlen);
                decoded[tlen] = '\0';
            }

            csv.printf("%d,%s,%lu,%s,%s,%d,%d,%d,%s,%d,%s,\"%s\"\n",
                       i + 1, dtBuf, (unsigned long)sesMs,
                       src, dst, e.channel, e.rssi, plen,
                       typeName(e.payloadType), e.seq,
                       hexBuf ? hexBuf : "", decoded);
            if (hexBuf) free(hexBuf);
        }

        // ── PCAP packet (radiotap header + raw 802.11 frame) ─────────────────
        if (pcap) {
            // radiotap: present=bit3(Channel)+bit5(Signal) → 13 bytes total
            struct __attribute__((packed)) {
                uint8_t  it_version = 0;
                uint8_t  it_pad     = 0;
                uint16_t it_len     = 13;
                uint32_t it_present = 0x00000028; // bit3=Channel, bit5=Signal
                uint16_t ch_freq;
                uint16_t ch_flags   = 0x00A0;     // 2.4 GHz CCK
                int8_t   signal_dbm;
            } rthdr;
            rthdr.ch_freq    = 2407 + e.channel * 5;
            rthdr.signal_dbm = e.rssi;

            uint32_t ts_sec, ts_usec;
            if (s_sessionEpoch > 0) {
                uint32_t offMs = e.timestamp - s_sessionStartMs;
                ts_sec  = (uint32_t)(s_sessionEpoch + offMs / 1000);
                ts_usec = (offMs % 1000) * 1000;
            } else {
                ts_sec  = 0;
                ts_usec = (e.timestamp - s_sessionStartMs) * 1000;
            }

            uint32_t incl = sizeof(rthdr) + e.rawLen;
            struct __attribute__((packed)) { uint32_t ts_s, ts_us, inc, orig; } phdr;
            phdr.ts_s = ts_sec; phdr.ts_us = ts_usec;
            phdr.inc  = incl;   phdr.orig  = incl;

            pcap.write((uint8_t*)&phdr,  sizeof(phdr));
            pcap.write((uint8_t*)&rthdr, sizeof(rthdr));
            pcap.write(e.raw, e.rawLen);
        }
        written++;
    }

    s_savedCount = cnt;
    if (csv)  csv.close();
    if (pcap) pcap.close();

    esp_wifi_set_promiscuous(true);
    s_active = true;
    return written;
}

// ─── filter + live-tail state ─────────────────────────────────────────────────
enum EsFilter { FILTER_ALL, FILTER_CHAT, FILTER_VOICE, FILTER_RAW };
static EsFilter  s_filter   = FILTER_ALL;
static bool      s_liveMode = true;   // auto-follow newest visible frame

static uint8_t   s_vis[ES_MAX_ENTRIES];  // filtered absolute indices into s_buf
static uint8_t   s_visCount  = 0;
static uint32_t  s_lastTotal = 0;        // trigger rebuild when s_total changes

static const char* filterLabel() {
    switch (s_filter) {
        case FILTER_CHAT:  return "CHAT";
        case FILTER_VOICE: return "VOIC";
        case FILTER_RAW:   return "RAW ";
        default:           return "ALL ";
    }
}

static void rebuildVis() {
    s_visCount = 0;
    uint8_t cnt = s_count;
    for (int i = 0; i < (int)cnt && s_visCount < ES_MAX_ENTRIES; i++) {
        const EsEntry& e = s_buf[physSlot(i)];
        bool show = (s_filter == FILTER_ALL)                          ||
                    (s_filter == FILTER_CHAT  && e.payloadType == 1)  ||
                    (s_filter == FILTER_VOICE && e.payloadType == 2)  ||
                    (s_filter == FILTER_RAW   && e.payloadType == 0);
        if (show) s_vis[s_visCount++] = (uint8_t)i;
    }
}

// ─── LIST view ────────────────────────────────────────────────────────────────
static void drawList(int selectedRow) {
    auto& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();

    // header
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("ESNIFF");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("MONITOR");
    dm.setTextColor(0x7BEF);     dm.printText("]");
    char hdr[40];
    snprintf(hdr, sizeof(hdr), " ch%-2d %lufrm %s %s%s",
             (int)s_channel, (unsigned long)s_total,
             s_hopping ? "HOP" : "LOCK",
             filterLabel(),
             s_liveMode ? " LIVE" : "");
    dm.setTextColor(0x4208); dm.println(hdr);
    dm.printSeparator();

    int y = outputY + LINE_HEIGHT * 2;
    int totalPages = (s_visCount > 0) ? ((s_visCount + ES_ROWS_PER_PAGE - 1) / ES_ROWS_PER_PAGE) : 1;
    int page       = (s_visCount > 0) ? (selectedRow / ES_ROWS_PER_PAGE) : 0;
    int start      = page * ES_ROWS_PER_PAGE;

    // column header
    dm.setCursor(4, y); dm.setTextColor(TFT_CYAN);
    dm.println(" ## SRC      ch  RSSI TYPE content");
    y += LINE_HEIGHT;

    for (int row = 0; row < ES_ROWS_PER_PAGE; row++) {
        dm.setCursor(4, y + LINE_HEIGHT * row);
        int visIdx = start + row;
        if (visIdx >= (int)s_visCount) {
            dm.setTextColor(TFT_BLACK);
            dm.println("                                        ");
            continue;
        }
        const EsEntry& e = s_buf[physSlot(s_vis[visIdx])];
        char src[9]; shortMac(e.srcMAC, src);

        // content: decoded text for CHAT, byte count for others
        char content[24] = {};
        const uint8_t* pl  = esPayload(e);
        int            plen = esPayloadLen(e);
        if (e.payloadType == 1 && plen > 2) {
            int tl = (plen - 2 < 22) ? plen - 2 : 22;
            memcpy(content, pl + 2, tl);
            content[tl] = '\0';
        } else if (e.payloadType == 2) {
            strncpy(content, "[audio]", sizeof(content));
        } else {
            snprintf(content, sizeof(content), "%dB", plen);
        }

        bool selected = (visIdx == selectedRow);
        char rowBuf[56];
        snprintf(rowBuf, sizeof(rowBuf), "%c%2d %-8s %-3d %4d %-4s %s",
                 selected ? '>' : ' ',
                 s_vis[visIdx] + 1, src, e.channel, e.rssi,
                 typeName(e.payloadType), content);

        uint16_t color = selected           ? TFT_CYAN   :
                         e.payloadType == 1 ? TFT_GREEN  :
                         e.payloadType == 2 ? TFT_YELLOW : TFT_WHITE;
        dm.setTextColor(color);
        dm.println(rowBuf);
    }

    // footer — two lines to fit all controls
    int footerY = y + LINE_HEIGHT * ES_ROWS_PER_PAGE;
    dm.setCursor(4, footerY);
    dm.setTextColor(0x4208);
    char foot1[56], foot2[56];
    snprintf(foot1, sizeof(foot1), "[j/k]sel [↵]open [h/l]pg%d/%d [f]flt [q]qt",
             page + 1, totalPages);
    snprintf(foot2, sizeof(foot2), "[c]%s%s [s]save [x]clr",
             s_hopping ? "LOCK" : "HOP",
             s_hopping ? " [+/-]ch" : "");
    dm.println(foot1);
    dm.setCursor(4, footerY + LINE_HEIGHT);
    dm.println(foot2);
}

// ─── DETAIL view ──────────────────────────────────────────────────────────────
static void drawDetail(int visIdx, int hexOffset) {
    if (visIdx >= (int)s_visCount) return;
    const EsEntry& e  = s_buf[physSlot(s_vis[visIdx])];
    const uint8_t* pl = esPayload(e);
    int            plen = esPayloadLen(e);
    auto& dm = displayManager;

    dm.clearScreen();
    dm.setDefaultTextSize();

    // header — show position in filtered list
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("ESNIFF");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("DETAIL");
    dm.setTextColor(0x7BEF);     dm.printText("]");
    char hdr[28];
    snprintf(hdr, sizeof(hdr), " #%d (%d/%d) %s",
             s_vis[visIdx] + 1, visIdx + 1, (int)s_visCount, typeName(e.payloadType));
    dm.setTextColor(TFT_WHITE); dm.println(hdr);
    dm.printSeparator();

    int y = outputY + LINE_HEIGHT * 2;

    char src[18]; macStr(e.srcMAC, src);
    char dst[18]; macStr(e.dstMAC, dst);

    dm.setCursor(4, y); dm.setTextColor(0x4208); dm.printText("SRC: ");
    dm.setTextColor(TFT_WHITE); dm.println(src); y += LINE_HEIGHT;

    dm.setCursor(4, y); dm.setTextColor(0x4208); dm.printText("DST: ");
    dm.setTextColor(macIsBcast(e.dstMAC) ? TFT_YELLOW : TFT_WHITE);
    dm.println(dst); y += LINE_HEIGHT;

    char meta[52];
    snprintf(meta, sizeof(meta), "Ch:%d  RSSI:%ddBm  Len:%d  Seq:%d",
             e.channel, e.rssi, plen, e.seq);
    dm.setCursor(4, y); dm.setTextColor(0x4208); dm.println(meta); y += LINE_HEIGHT;

    char timeLine[32] = {};
    char dtBuf[24]    = {};
    frameDateTime(e, dtBuf);
    if (dtBuf[0]) strncpy(timeLine, dtBuf, sizeof(timeLine) - 1);
    else snprintf(timeLine, sizeof(timeLine), "Age: %lus",
                  (unsigned long)((millis() - e.timestamp) / 1000));
    dm.setCursor(4, y); dm.setTextColor(0x4208); dm.println(timeLine); y += LINE_HEIGHT;

    // decoded line
    dm.setCursor(4, y);
    if (e.payloadType == 1 && plen > 2) {
        char txt[40]; int tl = (plen - 2 < 39) ? plen - 2 : 39;
        memcpy(txt, pl + 2, tl); txt[tl] = '\0';
        char decoded[48]; snprintf(decoded, sizeof(decoded), "MSG: %s", txt);
        dm.setTextColor(TFT_GREEN); dm.println(decoded);
    } else if (e.payloadType == 2) {
        dm.setTextColor(TFT_YELLOW); dm.println("VOICE: audio frame");
    } else {
        dm.setTextColor(0x4208); dm.println("RAW payload:");
    }
    y += LINE_HEIGHT;

    // hex dump
    int alignedOff = (hexOffset / ES_HEX_BPR) * ES_HEX_BPR;
    for (int row = 0; row < ES_HEX_ROWS; row++) {
        int byteOff = alignedOff + row * ES_HEX_BPR;
        dm.setCursor(4, y + LINE_HEIGHT * row);
        if (byteOff >= plen) {
            dm.setTextColor(TFT_BLACK);
            dm.println("                                  ");
            continue;
        }
        char line[52] = {}; int pos = 0;
        pos += snprintf(line + pos, sizeof(line) - pos, "%04X: ", byteOff);
        char ascii[ES_HEX_BPR + 1] = {};
        for (int b = 0; b < ES_HEX_BPR; b++) {
            int ab = byteOff + b;
            if (ab < plen) {
                uint8_t bv = pl[ab];
                pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", bv);
                ascii[b] = (bv >= 32 && bv < 127) ? (char)bv : '.';
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
                ascii[b] = ' ';
            }
        }
        snprintf(line + pos, sizeof(line) - pos, " %s", ascii);
        dm.setTextColor(TFT_WHITE); dm.println(line);
    }

    int footerY = y + LINE_HEIGHT * ES_HEX_ROWS;
    int totalHex = (plen > 0) ? ((plen + ES_HEX_BPR * ES_HEX_ROWS - 1) / (ES_HEX_BPR * ES_HEX_ROWS)) : 1;
    int curHex   = alignedOff / (ES_HEX_BPR * ES_HEX_ROWS) + 1;
    char footer[52];
    snprintf(footer, sizeof(footer), "[j/k]hex %d/%d  [n/p]frame  [b]back  [q]qt",
             curHex, totalHex);
    dm.setCursor(4, footerY); dm.setTextColor(0x4208); dm.println(footer);
}

// ─── status message overlay ───────────────────────────────────────────────────
static void showMsg(const char* msg, uint16_t color) {
    int msgY = outputY + LINE_HEIGHT * (3 + ES_ROWS_PER_PAGE + 2);
    displayManager.setCursor(4, msgY);
    displayManager.setTextColor(color);
    displayManager.println(msg);
}

// ─── main entry point ─────────────────────────────────────────────────────────
void runEspSniff(char* args) {
    int lockCh = (args && *args) ? atoi(args) : 0;

    s_head     = 0;
    s_count    = 0;
    s_total    = 0;
    s_hopping  = (lockCh < 1 || lockCh > 13);
    s_channel  = s_hopping ? HOP_SEQ[0] : (uint8_t)lockCh;
    s_hopIdx   = 0;
    s_lastHop  = millis();
    s_filter   = FILTER_ALL;
    s_liveMode = true;
    s_visCount = 0;
    s_lastTotal = 0;

    initSession();   // open SD files BEFORE WiFi — GDMA rule

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(espNowSniffCb);
    }

    wifi_promiscuous_filter_t flt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                                                      WIFI_PROMIS_FILTER_MASK_MISC };
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(snifferCb);
    s_active = true;
    esp_wifi_set_promiscuous(true);

    EsState  state       = STATE_LIST;
    int      selectedRow = 0;
    int      hexOffset   = 0;
    bool     needRedraw  = true;
    uint32_t lastRedraw  = 0;
    uint32_t msgExpiry   = 0;

    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) needRedraw = true;

        // channel hop
        if (state == STATE_LIST && s_hopping && millis() - s_lastHop >= ES_HOP_MS) {
            s_hopIdx  = (s_hopIdx + 1) % 13;
            s_channel = HOP_SEQ[s_hopIdx];
            esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
            s_lastHop = millis();
            needRedraw = true;
        }

        // new frames — rebuild filter, update live tail
        if (s_total != s_lastTotal) {
            s_lastTotal = s_total;
            rebuildVis();
            if (s_liveMode && s_visCount > 0)
                selectedRow = s_visCount - 1;
            if (state == STATE_LIST && millis() - lastRedraw >= 300)
                needRedraw = true;
        }

        // auto-save
        if (s_fileOk && s_active && (int)s_count - (int)s_savedCount >= ES_AUTO_SAVE_AT) {
            int n = doSave(true);
            if (n > 0) {
                char msg[40]; snprintf(msg, sizeof(msg), "Auto-saved %d frames", n);
                showMsg(msg, TFT_YELLOW);
                msgExpiry = millis() + 1500;
            }
        }

        if (msgExpiry && millis() >= msgExpiry) { msgExpiry = 0; needRedraw = true; }

        char           k  = inputHandler.getKeyboardInput();
        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (k == 'q') break;

        if (state == STATE_LIST) {
            if (k == 'j' || tb == TBALL_DOWN) {
                if (selectedRow < (int)s_visCount - 1) {
                    selectedRow++;
                    if (selectedRow == (int)s_visCount - 1) s_liveMode = true;
                    needRedraw = true;
                }
            } else if (k == 'k' || tb == TBALL_UP) {
                if (selectedRow > 0) { selectedRow--; s_liveMode = false; needRedraw = true; }
            } else if (k == 'l' || tb == TBALL_RIGHT) {  // page down
                int nr = selectedRow + ES_ROWS_PER_PAGE;
                selectedRow = (nr < (int)s_visCount) ? nr : (s_visCount > 0 ? s_visCount - 1 : 0);
                s_liveMode = (selectedRow == (int)s_visCount - 1);
                needRedraw = true;
            } else if (k == 'h' || tb == TBALL_LEFT) {   // page up
                int nr = selectedRow - ES_ROWS_PER_PAGE;
                selectedRow = (nr >= 0) ? nr : 0;
                s_liveMode = false;
                needRedraw = true;
            } else if (k == '\r' || k == '\n' || tb == TBALL_CLICK) {
                if (s_visCount > 0) { state = STATE_DETAIL; hexOffset = 0; needRedraw = true; }
            } else if (k == 'c') {
                s_hopping = !s_hopping; s_lastHop = millis(); needRedraw = true;
            } else if (k == '+' || k == '=') {
                if (s_channel < 13) {
                    s_channel++;
                    s_hopping = false;
                    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
                    needRedraw = true;
                }
            } else if (k == '-') {
                if (s_channel > 1) {
                    s_channel--;
                    s_hopping = false;
                    esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
                    needRedraw = true;
                }
            } else if (k == 'f') {
                s_filter = (EsFilter)(((int)s_filter + 1) % 4);
                rebuildVis();
                if (s_liveMode) selectedRow = (s_visCount > 0) ? s_visCount - 1 : 0;
                else if (selectedRow >= (int)s_visCount) selectedRow = (s_visCount > 0) ? s_visCount - 1 : 0;
                needRedraw = true;
            } else if (k == 'x') {
                s_active = false;
                esp_wifi_set_promiscuous(false);
                s_head = s_count = 0; s_total = 0; s_savedCount = 0;
                esp_wifi_set_promiscuous(true);
                s_active = true;
                rebuildVis();
                selectedRow = 0; s_liveMode = true;
                needRedraw = true;
            } else if (k == 's') {
                int n = doSave(false);
                char msg[44];
                if (n > 0) snprintf(msg, sizeof(msg), "Saved %d frm -> %03u.csv+pcap", n, s_sessionNum);
                else       strncpy(msg, "Nothing new to save", sizeof(msg));
                showMsg(msg, n >= 0 ? TFT_GREEN : TFT_RED);
                msgExpiry = millis() + 2000;
            }

        } else {  // STATE_DETAIL
            int absIdx = (s_visCount > 0) ? s_vis[selectedRow] : 0;
            int maxHex = (s_visCount > 0) ? esPayloadLen(s_buf[physSlot(absIdx)]) : 0;
            if (k == 'j' || tb == TBALL_DOWN) {
                int next = hexOffset + ES_HEX_BPR;
                if (next < maxHex) { hexOffset = next; needRedraw = true; }
            } else if (k == 'k' || tb == TBALL_UP) {
                if (hexOffset >= ES_HEX_BPR) { hexOffset -= ES_HEX_BPR; needRedraw = true; }
            } else if (k == 'n' || tb == TBALL_RIGHT) {
                if (selectedRow < (int)s_visCount - 1) {
                    selectedRow++; hexOffset = 0; needRedraw = true;
                }
            } else if (k == 'p' || tb == TBALL_LEFT) {
                if (selectedRow > 0) {
                    selectedRow--; hexOffset = 0; needRedraw = true;
                }
            } else if (k == 'b' || tb == TBALL_CLICK) {
                state = STATE_LIST; needRedraw = true;
            }
        }

        if (needRedraw && !displayManager.isBlocked()) {
            needRedraw = false;
            lastRedraw = millis();
            if (selectedRow >= (int)s_visCount && s_visCount > 0) selectedRow = s_visCount - 1;
            if (state == STATE_LIST) drawList(selectedRow);
            else                     drawDetail(selectedRow, hexOffset);
        }
    }

    if (s_fileOk && s_savedCount < s_count) doSave(false);

    s_active = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_STA);

    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_WHITE);
    char bye[52];
    snprintf(bye, sizeof(bye), "ESPSniff done. %lu frm, session %03u.",
             (unsigned long)s_total, s_sessionNum);
    displayManager.println(bye);
}
