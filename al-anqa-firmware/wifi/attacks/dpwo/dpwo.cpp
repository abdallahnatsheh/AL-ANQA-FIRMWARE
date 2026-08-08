// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// dpwo / dw — DEFAULT-PASSWORD checker (Network).
//
// Audits a host you've already discovered for services still running FACTORY /
// default credentials — the "admin:admin" class of finding that dominates real
// LAN audits (IP cameras, routers, printers, NAS, IoT). It is a *default-cred*
// checker, NOT a brute-forcer: a small curated per-service list, rate-limited,
// own-networks-only.
//
// Everything is a short scripted exchange over a raw WiFiClient (TCP) / WiFiUDP
// (UDP) socket — no per-protocol libraries, so RAM stays tiny (rule 5c). All
// plain STA sockets (no promiscuous / no soft-AP) → NO GDMA concern, SD writes
// are safe anytime. Only HTTP/RTSP Digest needs crypto (mbedTLS MD5, already
// linked). Target resolution reuses network_scanner's shared resolveNetTarget
// (ip / nd# / ns#), exactly like ping/portscan/arpspoof.
//
// Services checked (Phase 1):
//   FTP(21) Telnet(23) HTTP(80/81/8000/8080, Basic+Digest) RTSP(554) Redis(6379)
//   SNMP(161/udp community strings).
//
// HONEST LIMITS: default-cred list only (not a wordlist grinder); no modern web
// FORM logins (CSRF/JS/custom POST) — HTTP is Basic/Digest only; HTTPS panels not
// probed (TLS DRAM cost). Own networks only.
//
// Sources / method (see NOTICES #22 — technique/spec/prior-art references, no code
// copied): HackTricks network-service pentesting; RFC 2617 (HTTP Basic/Digest),
// RFC 2326 (RTSP), RFC 1157 (SNMPv1). RouterSploit creds/ modules informed the
// per-service default-cred flow ("success = past the 401"); Cameradar's RTSP route
// dictionary informed the brand stream-path probing (cameras 404 on "/"); Bruce
// ESP32 firmware is prior art for on-device router default-cred search. Camera
// default-cred prevalence: Hikvision/Dahua public defaults.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SD.h>
#include <mbedtls/md5.h>
#include <mbedtls/base64.h>
#include "libssh_esp32.h"          // SSH default-cred check reuses the sc/ssh infra
#include <libssh/libssh.h>
#include "dpwo.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "network_scanner.h"   // resolveNetTarget()
#include "sdcard_manager.h"    // SD_DIR_DPWO

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── credential lists ──────────────────────────────────────────────────────────────
struct DpCred { const char* user; const char* pass; };

// Curated user:pass defaults (routers / cameras / NAS / IoT). Kept small so a full
// pass over an open service is seconds, not minutes.
static const DpCred DP_BUILTIN[] = {
    {"admin", "admin"},   {"admin", "password"}, {"admin", ""},        {"admin", "1234"},
    {"admin", "12345"},   {"admin", "admin123"}, {"admin", "9999"},    {"admin", "pass"},
    {"root",  "root"},    {"root",  ""},         {"root",  "admin"},   {"root", "toor"},
    {"root",  "password"},{"root",  "12345"},    {"user",  "user"},    {"guest", "guest"},
    {"support","support"},{"service","service"},
};
static const int DP_BUILTIN_N = (int)(sizeof(DP_BUILTIN) / sizeof(DP_BUILTIN[0]));

// SNMP community strings (read-access probe).
static const char* DP_SNMP_COMM[] = { "public", "private", "community", "manager", "admin", "cisco" };
static const int DP_SNMP_COMM_N = (int)(sizeof(DP_SNMP_COMM) / sizeof(DP_SNMP_COMM[0]));

// SSH gets its OWN short list: each attempt is a full key-exchange (slow + a crash
// risk on the shared HW-SHA engine, see ssh_client), so keep it tight.
static const DpCred DP_SSH_CREDS[] = {
    {"root", "root"}, {"root", "admin"},  {"root", "toor"},      {"root", "password"},
    {"root", ""},     {"admin", "admin"}, {"admin", "password"}, {"admin", ""},
    {"pi", "raspberry"}, {"user", "user"},
};
static const int DP_SSH_N = (int)(sizeof(DP_SSH_CREDS) / sizeof(DP_SSH_CREDS[0]));

// Runtime cred tables = built-ins + optional SD extras. Two separate SD files:
//   /apps/dpwo/creds.csv       → the shared "user,pass" list (FTP/Telnet/HTTP/RTSP/Redis/MQTT)
//   /apps/dpwo/ssh_creds.csv   → SSH-only (kept separate: each SSH try is a slow key exchange)
// SD-row backing stores are heap-allocated on demand + freed on exit (rule 5c); built-in
// rows point at string literals and need no backing.
#define DP_MAX_CREDS 48
#define DP_SSH_MAX   24
static DpCred s_creds[DP_MAX_CREDS];               // shared list (pointer pairs, always resident)
static DpCred s_sshCr[DP_SSH_MAX];                 // SSH-only list
static char (*s_credBuf)[2][24] = nullptr;         // heap SD backing for creds.csv
static char (*s_sshBuf)[2][24]  = nullptr;         // heap SD backing for ssh_creds.csv
static int  s_credN  = 0;
static int  s_sshCrN = 0;

// Shared parser: append "user,pass" rows from an open SD file into cr[] (backed by the
// pre-allocated buf), from *n up to max. Blank lines and '#' comments are skipped.
static void dpReadCredFile(File& f, DpCred* cr, char (*buf)[2][24], int* n, int max) {
    int row = 0;
    while (f.available() && *n < max && row < max) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int c = line.indexOf(',');
        if (c < 0) continue;
        String u = line.substring(0, c);   u.trim();
        String p = line.substring(c + 1);  p.trim();
        strncpy(buf[row][0], u.c_str(), 23); buf[row][0][23] = 0;
        strncpy(buf[row][1], p.c_str(), 23); buf[row][1][23] = 0;
        cr[*n].user = buf[row][0];
        cr[*n].pass = buf[row][1];
        (*n)++; row++;
    }
}

static void dpLoadCreds() {
    s_credN = 0;
    for (int i = 0; i < DP_BUILTIN_N && s_credN < DP_MAX_CREDS; i++) s_creds[s_credN++] = DP_BUILTIN[i];
    s_sshCrN = 0;
    for (int i = 0; i < DP_SSH_N && s_sshCrN < DP_SSH_MAX; i++) s_sshCr[s_sshCrN++] = DP_SSH_CREDS[i];
    if (!sdCardManager.canAccessSD()) return;
    File f = SD.open(SD_DIR_DPWO "/creds.csv");
    if (f) {
        s_credBuf = (char (*)[2][24])malloc(sizeof(char) * DP_MAX_CREDS * 2 * 24);
        if (s_credBuf) dpReadCredFile(f, s_creds, s_credBuf, &s_credN, DP_MAX_CREDS);
        f.close();
    }
    File sf = SD.open(SD_DIR_DPWO "/ssh_creds.csv");
    if (sf) {
        s_sshBuf = (char (*)[2][24])malloc(sizeof(char) * DP_SSH_MAX * 2 * 24);
        if (s_sshBuf) dpReadCredFile(sf, s_sshCr, s_sshBuf, &s_sshCrN, DP_SSH_MAX);
        sf.close();
    }
}

static void dpFreeCreds() {
    if (s_credBuf) { free(s_credBuf); s_credBuf = nullptr; }
    if (s_sshBuf)  { free(s_sshBuf);  s_sshBuf  = nullptr; }
    s_credN = 0; s_sshCrN = 0;
}

// ── low-level socket helpers ────────────────────────────────────────────────────────
static bool dpAbort() {
    char k = inputHandler.getKeyboardInput();
    return (k == 'q' || k == 'Q');
}

static bool dpWaitAvail(WiFiClient& c, uint32_t toMs) {
    uint32_t t0 = millis();
    while (!c.available()) {
        if (!c.connected() && !c.available()) return false;
        if (millis() - t0 > toMs) return false;
        delay(2);
    }
    return true;
}

// Read into buf until: overall timeout, or a >250ms lull after data arrived, or EOF.
static int dpRead(WiFiClient& c, char* buf, int n, uint32_t toMs) {
    uint32_t t0 = millis(), lastData = millis();
    int got = 0;
    while (got < n - 1) {
        while (c.available() && got < n - 1) { buf[got++] = (char)c.read(); lastData = millis(); }
        if (!c.connected() && !c.available()) break;
        if (millis() - t0 > toMs) break;
        if (got > 0 && millis() - lastData > 250) break;
        delay(4);
    }
    buf[got] = 0;
    return got;
}

// ── crypto helpers (HTTP/RTSP auth) ──────────────────────────────────────────────────
static void dpMd5Hex(const String& s, char* outHex /*33*/) {
    unsigned char d[16];
    mbedtls_md5_context ctx; mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const unsigned char*)s.c_str(), s.length());
    mbedtls_md5_finish_ret(&ctx, d);
    mbedtls_md5_free(&ctx);
    for (int i = 0; i < 16; i++) sprintf(outHex + i * 2, "%02x", d[i]);
    outHex[32] = 0;
}

static String dpBasicHeader(const DpCred& c) {
    String up = String(c.user) + ":" + c.pass;
    unsigned char out[160]; size_t ol = 0;
    if (mbedtls_base64_encode(out, sizeof(out), &ol, (const unsigned char*)up.c_str(), up.length()) != 0)
        return String();
    out[ol] = 0;
    return String("Authorization: Basic ") + (char*)out;
}

// One WWW-Authenticate / auth challenge.
enum { DP_AUTH_NONE = 0, DP_AUTH_BASIC, DP_AUTH_DIGEST };
struct DpAuth { int scheme; char realm[64]; char nonce[160]; char qop[16]; char opaque[80]; };

// Pull key="value" (or key=value) out of a header line, case-insensitive key.
static bool dpAuthTok(const char* hdr, const char* key, char* out, int n) {
    out[0] = 0;
    String h(hdr), k(key);
    h.toLowerCase(); k.toLowerCase();
    int p = h.indexOf(k + "=");
    if (p < 0) return false;
    p += k.length() + 1;
    // work on the ORIGINAL (case-preserved) string from p
    const char* s = hdr + p;
    while (*s == ' ') s++;
    int i = 0;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && i < n - 1) out[i++] = *s++;
    } else {
        while (*s && *s != ',' && *s != '\r' && *s != '\n' && *s != ' ' && i < n - 1) out[i++] = *s++;
    }
    out[i] = 0;
    return true;
}

// Parse the WWW-Authenticate header out of a full HTTP/RTSP response.
static void dpParseAuth(const char* resp, DpAuth& a) {
    memset(&a, 0, sizeof(a));
    // find the header line
    String r(resp); String rl(resp); rl.toLowerCase();
    int p = rl.indexOf("www-authenticate:");
    if (p < 0) { a.scheme = DP_AUTH_NONE; return; }
    int e = r.indexOf('\n', p); if (e < 0) e = r.length();
    String line = r.substring(p, e);
    String ll = line; ll.toLowerCase();
    a.scheme = (ll.indexOf("digest") >= 0) ? DP_AUTH_DIGEST : DP_AUTH_BASIC;
    dpAuthTok(line.c_str(), "realm",  a.realm,  sizeof(a.realm));
    if (a.scheme == DP_AUTH_DIGEST) {
        dpAuthTok(line.c_str(), "nonce",  a.nonce,  sizeof(a.nonce));
        dpAuthTok(line.c_str(), "opaque", a.opaque, sizeof(a.opaque));
        char qop[32]; if (dpAuthTok(line.c_str(), "qop", qop, sizeof(qop))) {
            // may be "auth,auth-int" — we only do "auth"
            if (strstr(qop, "auth")) strcpy(a.qop, "auth");
        }
    }
}

// Build the Authorization header line for a Basic/Digest challenge (HTTP or RTSP).
static String dpAuthHeader(const DpCred& c, const char* method, const char* uri, const DpAuth& a) {
    if (a.scheme == DP_AUTH_BASIC) return dpBasicHeader(c);
    if (a.scheme != DP_AUTH_DIGEST) return String();
    char ha1[33], ha2[33], resp[33];
    dpMd5Hex(String(c.user) + ":" + a.realm + ":" + c.pass, ha1);
    dpMd5Hex(String(method) + ":" + uri, ha2);
    String hdr = String("Authorization: Digest username=\"") + c.user + "\", realm=\"" + a.realm +
                 "\", nonce=\"" + a.nonce + "\", uri=\"" + uri + "\"";
    if (a.qop[0]) {
        char cnonce[9]; snprintf(cnonce, sizeof(cnonce), "%08x", (unsigned)(millis() * 2654435761u));
        const char* nc = "00000001";
        dpMd5Hex(String(ha1) + ":" + a.nonce + ":" + nc + ":" + cnonce + ":" + a.qop + ":" + ha2, resp);
        hdr += String(", qop=") + a.qop + ", nc=" + nc + ", cnonce=\"" + cnonce + "\", response=\"" + resp + "\"";
    } else {
        dpMd5Hex(String(ha1) + ":" + a.nonce + ":" + ha2, resp);
        hdr += String(", response=\"") + resp + "\"";
    }
    if (a.opaque[0]) hdr += String(", opaque=\"") + a.opaque + "\"";
    return hdr;
}

// Status code out of "HTTP/1.1 401 ..." or "RTSP/1.0 200 ..." (first line).
static int dpStatusCode(const char* resp) {
    const char* sp = strchr(resp, ' ');
    if (!sp) return 0;
    return atoi(sp + 1);
}

// Forward decl — live "currently trying" indicator (defined below with the UI helpers,
// after the column layout); every per-service cred loop calls it.
static void dpTrying(const char* user, const char* pass);

// ── per-service checks ───────────────────────────────────────────────────────────────
// return codes: -1 = port closed · 0 = open, no default found · 1 = HIT (creds set)
//               2 = open with NO auth required · -2 = aborted by [q]

static int dpHttpTry(IPAddress ip, uint16_t port, const DpCred& c, const char* method,
                     const char* uri, const DpAuth& a) {
    WiFiClient s;
    if (!s.connect(ip, port, 800)) return 0;
    String req = String(method) + " " + uri + " HTTP/1.1\r\nHost: " + ip.toString() +
                 "\r\n" + dpAuthHeader(c, method, uri, a) + "\r\nConnection: close\r\nUser-Agent: dpwo\r\n\r\n";
    s.print(req);
    char r[512]; dpRead(s, r, sizeof(r), 1600); s.stop();       // status line is all we need
    int code = dpStatusCode(r);
    return (code >= 200 && code < 400 && code != 0) ? 1 : 0;   // past the 401 = accepted
}

// Common admin/login paths — many devices leave "/" open (200) but Basic/Digest-gate
// the real panel elsewhere. Probe these to find the one that challenges (401), then try
// creds there. Method = Metasploit http_login / changeme (probe URIs → 401 → brute).
// HTTP is Basic/Digest only — form logins need per-device fingerprints (out of scope).
static const char* DP_HTTP_PATHS[] = {
    "/",              "/admin",    "/login",     "/cgi-bin/luci",   // generic + OpenWrt LuCI
    "/setup.cgi",     "/index.asp","/main.html", "/system.html",    // routers
    "/doc/page/login.asp",                                          // Hikvision cameras
};
static const int DP_HTTP_PATHS_N = (int)(sizeof(DP_HTTP_PATHS) / sizeof(DP_HTTP_PATHS[0]));

static int dpHttp(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient p; if (!p.connect(ip, port, 500)) return -1; p.stop(); }   // closed?
    DpAuth a; a.scheme = DP_AUTH_NONE;
    const char* authPath = nullptr;
    bool sawOpen = false;
    for (int pth = 0; pth < DP_HTTP_PATHS_N && !authPath; pth++) {
        if (dpAbort()) return -2;
        WiFiClient s;
        if (!s.connect(ip, port, 700)) break;
        s.print(String("GET ") + DP_HTTP_PATHS[pth] + " HTTP/1.1\r\nHost: " + ip.toString() +
                "\r\nConnection: close\r\nUser-Agent: dpwo\r\n\r\n");
        char r[1024]; dpRead(s, r, sizeof(r), 1600); s.stop();
        int code = dpStatusCode(r);
        if (code == 401) { dpParseAuth(r, a); if (a.scheme != DP_AUTH_NONE) authPath = DP_HTTP_PATHS[pth]; }
        else if (code >= 200 && code < 400) sawOpen = true;    // reachable but no challenge (yet)
    }
    if (!authPath) {                                           // no Basic/Digest gate found
        if (sawOpen) { strcpy(hu, "(none)"); strcpy(hp, "no auth (form?)"); return 2; }
        return 0;
    }
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        dpTrying(s_creds[i].user, s_creds[i].pass);
        if (dpHttpTry(ip, port, s_creds[i], "GET", authPath, a) == 1) {
            strncpy(hu, s_creds[i].user, 23); hu[23] = 0;
            strncpy(hp, s_creds[i].pass[0] ? s_creds[i].pass : "(blank)", 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

// Common camera stream paths — many cameras 404 on "/" and only challenge auth on a
// valid route (Cameradar's insight, NOTICES). Probe these until one 401s (or 200s).
static const char* DP_RTSP_PATHS[] = {
    "/",                                   // generic / some NVRs
    "/Streaming/Channels/101",             // Hikvision
    "/cam/realmonitor?channel=1&subtype=0",// Dahua
    "/h264Preview_01_main",                // Reolink
    "/live.sdp", "/live", "/11",           // Axis / generic / some Chinese OEM
};
static const int DP_RTSP_PATHS_N = (int)(sizeof(DP_RTSP_PATHS) / sizeof(DP_RTSP_PATHS[0]));

static int dpRtsp(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient p; if (!p.connect(ip, port, 500)) return -1; p.stop(); }   // port probe
    // Find a path that either serves without auth (200) or challenges (401).
    DpAuth a; a.scheme = DP_AUTH_NONE;
    String url; int cseq = 1; bool challenged = false;
    for (int pth = 0; pth < DP_RTSP_PATHS_N; pth++) {
        if (dpAbort()) return -2;
        String u = String("rtsp://") + ip.toString() + ":" + port + DP_RTSP_PATHS[pth];
        WiFiClient c;
        if (!c.connect(ip, port, 700)) return 0;
        c.print(String("DESCRIBE ") + u + " RTSP/1.0\r\nCSeq: " + cseq++ + "\r\nUser-Agent: dpwo\r\n\r\n");
        char r[900]; dpRead(c, r, sizeof(r), 1500); c.stop();   // need the WWW-Authenticate header
        int code = dpStatusCode(r);
        if (code >= 200 && code < 300) { strcpy(hu, "(none)"); strcpy(hp, "no auth"); return 2; }
        if (code == 401) { dpParseAuth(r, a); url = u; challenged = true; break; }
        // 404/400/etc → try the next brand path
    }
    if (!challenged || a.scheme == DP_AUTH_NONE) return 0;
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        dpTrying(s_creds[i].user, s_creds[i].pass);
        WiFiClient c2;
        if (!c2.connect(ip, port, 800)) return 0;
        String req = String("DESCRIBE ") + url + " RTSP/1.0\r\nCSeq: " + cseq++ + "\r\n" +
                     dpAuthHeader(s_creds[i], "DESCRIBE", url.c_str(), a) + "\r\nUser-Agent: dpwo\r\n\r\n";
        c2.print(req);
        char rr[512]; dpRead(c2, rr, sizeof(rr), 1500); c2.stop();   // status line only
        int cc = dpStatusCode(rr);
        if (cc >= 200 && cc < 300) {
            strncpy(hu, s_creds[i].user, 23); hu[23] = 0;
            strncpy(hp, s_creds[i].pass[0] ? s_creds[i].pass : "(blank)", 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

static int dpFtp(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient p; if (!p.connect(ip, port, 500)) return -1; p.stop(); }
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        dpTrying(s_creds[i].user, s_creds[i].pass);
        WiFiClient c;
        if (!c.connect(ip, port, 800)) return 0;
        char b[200];
        dpRead(c, b, sizeof(b), 1500);                            // 220 banner
        c.print(String("USER ") + s_creds[i].user + "\r\n");
        dpRead(c, b, sizeof(b), 1500);                            // 331 (need pass) or 230
        bool ok = (atoi(b) == 230);                               // some servers log in on USER alone
        if (!ok) {
            c.print(String("PASS ") + s_creds[i].pass + "\r\n");
            dpRead(c, b, sizeof(b), 1800);                        // 230 ok / 530 fail
            ok = (atoi(b) == 230);
        }
        c.print("QUIT\r\n"); c.stop();
        if (ok) {
            strncpy(hu, s_creds[i].user, 23); hu[23] = 0;
            strncpy(hp, s_creds[i].pass[0] ? s_creds[i].pass : "(blank)", 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

// Telnet: minimal IAC negotiation (refuse every option), collect printable text.
static int dpTelnetRead(WiFiClient& c, char* buf, int n, uint32_t toMs) {
    uint32_t t0 = millis(), lastData = millis();
    int got = 0;
    while (got < n - 1) {
        while (c.available() && got < n - 1) {
            uint8_t b = c.read(); lastData = millis();
            if (b == 0xFF) {                                      // IAC
                if (!dpWaitAvail(c, 300)) break;
                uint8_t cmd = c.read();
                if (cmd == 0xFF) { buf[got++] = (char)0xFF; continue; }
                if (cmd >= 0xFB && cmd <= 0xFE) {                 // WILL/WONT/DO/DONT
                    if (!dpWaitAvail(c, 300)) break;
                    uint8_t opt = c.read();
                    uint8_t reply = 0;
                    if (cmd == 0xFD) reply = 0xFC;                // DO   -> WONT
                    else if (cmd == 0xFB) reply = 0xFE;           // WILL -> DONT
                    if (reply) { uint8_t o[3] = { 0xFF, reply, opt }; c.write(o, 3); }
                }
                continue;
            }
            if (b >= 32 || b == '\n' || b == '\r') buf[got++] = (char)b;
        }
        if (!c.connected() && !c.available()) break;
        if (millis() - t0 > toMs) break;
        if (got > 0 && millis() - lastData > 300) break;
        delay(5);
    }
    buf[got] = 0;
    return got;
}

static bool dpTelnetSuccess(const char* buf) {
    String s(buf); s.toLowerCase();
    if (s.indexOf("incorrect") >= 0 || s.indexOf("invalid") >= 0 || s.indexOf("fail") >= 0 ||
        s.indexOf("denied") >= 0 || s.indexOf("bad pass") >= 0 || s.indexOf("login:") >= 0 ||
        s.indexOf("password:") >= 0)
        return false;
    // a shell/banner rather than another prompt
    if (s.indexOf("# ") >= 0 || s.indexOf("$ ") >= 0 || s.indexOf("welcome") >= 0 ||
        s.indexOf("last login") >= 0 || s.indexOf("busybox") >= 0 || s.endsWith("# ") ||
        s.endsWith("$ ") || s.endsWith("> "))
        return true;
    return false;   // ambiguous → treat as fail (avoid false positives)
}

static int dpTelnet(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient p; if (!p.connect(ip, port, 500)) return -1; p.stop(); }
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        dpTrying(s_creds[i].user, s_creds[i].pass);
        WiFiClient c;
        if (!c.connect(ip, port, 900)) return 0;
        char b[300];
        dpTelnetRead(c, b, sizeof(b), 1500);                      // banner + "login:"
        c.print(s_creds[i].user); c.print("\r\n");
        dpTelnetRead(c, b, sizeof(b), 1500);                      // "Password:"
        c.print(s_creds[i].pass); c.print("\r\n");
        int n = dpTelnetRead(c, b, sizeof(b), 1900);
        c.stop();
        if (n > 0 && dpTelnetSuccess(b)) {
            strncpy(hu, s_creds[i].user, 23); hu[23] = 0;
            strncpy(hp, s_creds[i].pass[0] ? s_creds[i].pass : "(blank)", 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

static int dpRedis(IPAddress ip, uint16_t port, char* hu, char* hp) {
    WiFiClient c;
    if (!c.connect(ip, port, 500)) return -1;
    c.print("PING\r\n");
    char r[128]; dpRead(c, r, sizeof(r), 1200); c.stop();
    if (strncmp(r, "+PONG", 5) == 0) { strcpy(hu, "(none)"); strcpy(hp, "NO AUTH"); return 2; }
    // AUTH required → try passwords (redis AUTH is password-only on legacy servers)
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        if (!s_creds[i].pass[0]) continue;
        dpTrying("", s_creds[i].pass);
        WiFiClient a;
        if (!a.connect(ip, port, 800)) return 0;
        a.print(String("AUTH ") + s_creds[i].pass + "\r\n");
        char rr[128]; dpRead(a, rr, sizeof(rr), 1000); a.stop();
        if (strncmp(rr, "+OK", 3) == 0) {
            strcpy(hu, "(any)");
            strncpy(hp, s_creds[i].pass, 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

// SNMPv1 GetRequest for sysDescr.0 with `comm` — any valid reply = read access.
static int dpBuildSnmp(const char* comm, uint8_t* p) {
    static const uint8_t oid[] = { 0x06, 0x08, 0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00 };
    int cl = strlen(comm);
    int varbindContent = sizeof(oid) + 2;          // OID + NULL(05 00)
    int varbind        = 2 + varbindContent;       // 30 len ...
    int pduContent     = 3 + 3 + 3 + (2 + varbind); // reqid + err + erridx + varbindlist
    int msgContent     = 3 + (2 + cl) + (2 + pduContent);
    int i = 0;
    p[i++] = 0x30; p[i++] = (uint8_t)msgContent;
    p[i++] = 0x02; p[i++] = 0x01; p[i++] = 0x00;                 // version v1 (0)
    p[i++] = 0x04; p[i++] = (uint8_t)cl; memcpy(p + i, comm, cl); i += cl;
    p[i++] = 0xa0; p[i++] = (uint8_t)pduContent;                 // GetRequest PDU
    p[i++] = 0x02; p[i++] = 0x01; p[i++] = 0x01;                 // request-id = 1
    p[i++] = 0x02; p[i++] = 0x01; p[i++] = 0x00;                 // error-status
    p[i++] = 0x02; p[i++] = 0x01; p[i++] = 0x00;                 // error-index
    p[i++] = 0x30; p[i++] = (uint8_t)varbind;                    // varbind list
    p[i++] = 0x30; p[i++] = (uint8_t)varbindContent;             // varbind
    memcpy(p + i, oid, sizeof(oid)); i += sizeof(oid);
    p[i++] = 0x05; p[i++] = 0x00;                                // value = NULL
    return i;
}

static int dpSnmp(IPAddress ip, uint16_t port, char* hu, char* hp) {
    WiFiUDP udp;
    if (!udp.begin(0)) return 0;
    for (int i = 0; i < DP_SNMP_COMM_N; i++) {
        if (dpAbort()) { udp.stop(); return -2; }
        dpTrying("", DP_SNMP_COMM[i]);
        uint8_t pkt[96];
        int len = dpBuildSnmp(DP_SNMP_COMM[i], pkt);
        udp.beginPacket(ip, port);
        udp.write(pkt, len);
        udp.endPacket();
        uint32_t t0 = millis();
        while (millis() - t0 < 700) {
            if (udp.parsePacket() > 0) {                         // any reply = community valid
                udp.stop();
                strcpy(hu, "community");
                strncpy(hp, DP_SNMP_COMM[i], 23); hp[23] = 0;
                return 1;
            }
            delay(10);
        }
    }
    udp.stop();
    return -1;   // no reply on any community → treat as closed/filtered
}

// MQTT (1883): hand-built MQTT 3.1.1 CONNECT + CONNACK parse. Anonymous first (open
// broker = NO AUTH), else try default creds. Ref: MQTT 3.1.1 spec (OASIS).
static int dpBuildMqtt(uint8_t* buf, const char* clientId, const char* user, const char* pass) {
    uint8_t vh[12]; int vhl = 0;
    vh[vhl++] = 0x00; vh[vhl++] = 0x04;                          // protocol name len
    vh[vhl++] = 'M'; vh[vhl++] = 'Q'; vh[vhl++] = 'T'; vh[vhl++] = 'T';
    vh[vhl++] = 0x04;                                            // protocol level 4 (3.1.1)
    uint8_t flags = 0x02;                                        // clean session
    if (user) { flags |= 0x80; if (pass) flags |= 0x40; }       // username + password flags
    vh[vhl++] = flags;
    vh[vhl++] = 0x00; vh[vhl++] = 0x3C;                          // keep-alive 60s

    uint8_t pl[160]; int pll = 0;
    int idl = strlen(clientId);
    pl[pll++] = (idl >> 8) & 0xFF; pl[pll++] = idl & 0xFF; memcpy(pl + pll, clientId, idl); pll += idl;
    if (user) {
        int ul = strlen(user);
        pl[pll++] = (ul >> 8) & 0xFF; pl[pll++] = ul & 0xFF; memcpy(pl + pll, user, ul); pll += ul;
        if (pass) {
            int ppl = strlen(pass);
            pl[pll++] = (ppl >> 8) & 0xFF; pl[pll++] = ppl & 0xFF; memcpy(pl + pll, pass, ppl); pll += ppl;
        }
    }
    int rem = vhl + pll, n = 0;
    buf[n++] = 0x10;                                             // CONNECT
    do { uint8_t e = rem % 128; rem /= 128; if (rem) e |= 0x80; buf[n++] = e; } while (rem);
    memcpy(buf + n, vh, vhl); n += vhl;
    memcpy(buf + n, pl, pll); n += pll;
    return n;
}

// CONNACK return code (0 = accepted), or -1 if the reply isn't a CONNACK.
static int dpMqttConnack(WiFiClient& c) {
    char r[8]; int n = dpRead(c, r, sizeof(r), 1500);
    if (n < 4 || (uint8_t)r[0] != 0x20) return -1;              // 0x20 = CONNACK
    return (uint8_t)r[3];                                        // return code byte
}

static int dpMqtt(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient c;
      if (!c.connect(ip, port, 600)) return -1;                 // closed
      uint8_t pkt[192]; int len = dpBuildMqtt(pkt, "dpwo", nullptr, nullptr);
      c.write(pkt, len);
      int rc = dpMqttConnack(c); c.stop();
      if (rc == 0x00) { strcpy(hu, "(none)"); strcpy(hp, "NO AUTH"); return 2; }
      if (rc < 0) return 0;                                     // not MQTT / no CONNACK
    }                                                           // rc 0x04/0x05 → needs creds
    for (int i = 0; i < s_credN; i++) {
        if (dpAbort()) return -2;
        dpTrying(s_creds[i].user, s_creds[i].pass);
        WiFiClient c;
        if (!c.connect(ip, port, 800)) return 0;
        uint8_t pkt[192]; int len = dpBuildMqtt(pkt, "dpwo", s_creds[i].user, s_creds[i].pass);
        c.write(pkt, len);
        int rc = dpMqttConnack(c); c.stop();
        if (rc == 0x00) {
            strncpy(hu, s_creds[i].user, 23); hu[23] = 0;
            strncpy(hp, s_creds[i].pass[0] ? s_creds[i].pass : "(blank)", 23); hp[23] = 0;
            return 1;
        }
    }
    return 0;
}

// SSH (22): libssh needs a big stack, so — exactly like the `sc` command — the auth
// loop runs in a dedicated ~50 KB pinned FreeRTOS task; the CLI task blocks on it.
// Fresh session per cred (mirrors ssh_client's proven single-session path); a small
// list bounds runtime + the per-KEX HW-SHA crash window.
static volatile bool s_sshDone, s_sshAbort;
static IPAddress     s_sshIp;
static uint16_t      s_sshPort;                  // supports a custom SSH port
static int           s_sshResult;               // 0 = no default, 1 = hit
static char          s_sshU[24], s_sshP[24];
static volatile int  s_sshTry;                  // current cred # (live SSH progress)
static int           s_curRow = -1;             // row being tested (for the SSH live redraw)

// Row baseline y + fixed columns (6px font) — shared by the drawing code and the SSH
// live-progress redraw so the status text lines up in one clean column.
static inline int dpRowY(int i) { return outputY + LINE_HEIGHT + 4 + i * LINE_HEIGHT; }
#define DP_COL_MARK  4
#define DP_COL_PORT  16
#define DP_COL_SVC   52
#define DP_COL_STAT  96

static int s_sel = -1;   // selected result row in the interactive view; -1 while scanning

// Row background: a dark-blue bar when this is the highlighted result row, else black.
static inline uint16_t dpRowBg(int i) { return (i == s_sel) ? 0x0010 : TFT_BLACK; }

// Live "currently trying" indicator — repaints the status cell of the row under test
// (s_curRow) with the exact credential (or SNMP community) in flight, so a slow service
// shows progress instead of a frozen "testing...". `user`=="" → single value (community).
static void dpTrying(const char* user, const char* pass) {
    if (displayManager.isBlocked() || s_curRow < 0) return;
    int y = dpRowY(s_curRow);
    displayManager.fillRect(DP_COL_STAT, y - 1, SCREEN_WIDTH - DP_COL_STAT, LINE_HEIGHT, dpRowBg(s_curRow));
    displayManager.setCursor(DP_COL_STAT, y);
    displayManager.setTextColor(TFT_YELLOW);
    char b[40];
    const char* pp = (pass && pass[0]) ? pass : "-";
    if (user && user[0]) snprintf(b, sizeof(b), "try %.13s:%.11s", user, pp);
    else                 snprintf(b, sizeof(b), "try %.24s", pp);
    displayManager.printText(b);
    displayManager.setTextColor(TFT_WHITE);
}

static void dpSshTask(void* arg) {
    (void)arg;
    libssh_begin();
    s_sshResult = 0;
    char ips[20]; strncpy(ips, s_sshIp.toString().c_str(), 19); ips[19] = 0;
    for (int i = 0; i < s_sshCrN && !s_sshAbort; i++) {
        s_sshTry = i + 1;                                        // live progress for the CLI task
        ssh_session ses = ssh_new();
        if (!ses) continue;
        int port = s_sshPort; long tmo = 8; int noCfg = 0;
        ssh_options_set(ses, SSH_OPTIONS_HOST, ips);
        ssh_options_set(ses, SSH_OPTIONS_USER, s_sshCr[i].user);
        ssh_options_set(ses, SSH_OPTIONS_PORT, &port);
        ssh_options_set(ses, SSH_OPTIONS_TIMEOUT, &tmo);
        ssh_options_set(ses, SSH_OPTIONS_PROCESS_CONFIG, &noCfg);
        if (ssh_connect(ses) != SSH_OK) { ssh_free(ses); continue; }
        int rc = ssh_userauth_password(ses, NULL, s_sshCr[i].pass);
        ssh_disconnect(ses); ssh_free(ses);
        if (rc == SSH_AUTH_SUCCESS) {
            strncpy(s_sshU, s_sshCr[i].user, 23); s_sshU[23] = 0;
            const char* pp = s_sshCr[i].pass[0] ? s_sshCr[i].pass : "(blank)";
            strncpy(s_sshP, pp, 23); s_sshP[23] = 0;
            s_sshResult = 1;
            break;
        }
    }
    ssh_finalize();
    s_sshDone = true;
    vTaskDelete(NULL);
}

static int dpSsh(IPAddress ip, uint16_t port, char* hu, char* hp) {
    { WiFiClient p; if (!p.connect(ip, port, 600)) return -1; p.stop(); }   // fast port probe
    s_sshIp = ip; s_sshPort = port; s_sshDone = false; s_sshAbort = false;
    s_sshResult = 0; s_sshU[0] = 0; s_sshP[0] = 0; s_sshTry = 0;
    if (xTaskCreatePinnedToCore(dpSshTask, "dpssh", 51200, nullptr, tskIDLE_PRIORITY + 3, nullptr, 1) != pdPASS)
        return 0;                                          // couldn't spawn → treat as no-default
    int lastTry = -1;
    while (!s_sshDone) {
        if (dpAbort()) s_sshAbort = true;                  // stop after the current attempt
        int t = s_sshTry;
        if (t != lastTry && t > 0 && !displayManager.isBlocked() && s_curRow >= 0) {
            // SSH is slow (a key exchange per cred) — show which cred so it isn't "frozen".
            int y = dpRowY(s_curRow);
            displayManager.fillRect(DP_COL_STAT, y - 1, SCREEN_WIDTH - DP_COL_STAT, LINE_HEIGHT, dpRowBg(s_curRow));
            displayManager.setCursor(DP_COL_STAT, y);
            displayManager.setTextColor(TFT_YELLOW);
            char b[36]; int ci = t - 1;
            if (ci >= 0 && ci < s_sshCrN)
                snprintf(b, sizeof(b), "try %d/%d %.9s:%.7s", t, s_sshCrN,
                         s_sshCr[ci].user, s_sshCr[ci].pass[0] ? s_sshCr[ci].pass : "-");
            else snprintf(b, sizeof(b), "testing %d/%d", t, s_sshCrN);
            displayManager.printText(b);
            displayManager.setTextColor(TFT_WHITE);
            lastTry = t;
        }
        delay(50);
    }
    if (s_sshResult == 1) { strcpy(hu, s_sshU); strcpy(hp, s_sshP); return 1; }
    return 0;
}

// ── service table + UI ───────────────────────────────────────────────────────────────
enum { SV_HTTP, SV_TELNET, SV_FTP, SV_RTSP, SV_REDIS, SV_SNMP, SV_SSH, SV_MQTT };
struct DpSvc { uint16_t port; const char* name; uint8_t proto; };
static const DpSvc DP_SVCS[] = {
    { 21,   "FTP  ", SV_FTP },
    { 22,   "SSH  ", SV_SSH },
    { 23,   "TELNET",SV_TELNET },
    { 80,   "HTTP ", SV_HTTP },
    { 81,   "HTTP ", SV_HTTP },
    { 8080, "HTTP ", SV_HTTP },
    { 8000, "HTTP ", SV_HTTP },
    { 554,  "RTSP ", SV_RTSP },
    { 6379, "REDIS", SV_REDIS },
    { 1883, "MQTT ", SV_MQTT },
    { 161,  "SNMP ", SV_SNMP },
};
static const int DP_SVC_N = (int)(sizeof(DP_SVCS) / sizeof(DP_SVCS[0]));

// per-row state
enum { ST_PENDING = 0, ST_TESTING, ST_CLOSED, ST_NODEF, ST_HIT, ST_OPEN };

// The run list — the actual targets to test this run (built-ins the filter selected,
// plus any custom service:port entries). Row state/results index into it 1:1.
struct DpTarget { uint16_t port; uint8_t proto; char name[8]; };
static DpTarget s_run[DP_SVC_N];
static int      s_runN = 0;
static uint8_t  s_state[DP_SVC_N];
static char     s_hu[DP_SVC_N][24];
static char     s_hp[DP_SVC_N][24];

static const char* dpProtoLabel(uint8_t proto) {
    switch (proto) {
        case SV_FTP:  return "FTP  "; case SV_SSH:   return "SSH  "; case SV_TELNET: return "TELNET";
        case SV_HTTP: return "HTTP "; case SV_RTSP:  return "RTSP "; case SV_REDIS:  return "REDIS";
        case SV_MQTT: return "MQTT "; case SV_SNMP:  return "SNMP ";
    }
    return "?";
}
static int dpProtoFromName(const String& n) {
    if (n == "ftp")   return SV_FTP;   if (n == "ssh")  return SV_SSH;   if (n == "telnet") return SV_TELNET;
    if (n == "http")  return SV_HTTP;  if (n == "rtsp") return SV_RTSP;  if (n == "redis")  return SV_REDIS;
    if (n == "mqtt")  return SV_MQTT;  if (n == "snmp") return SV_SNMP;
    return -1;
}
static void dpAddRun(uint16_t port, uint8_t proto, const char* name) {
    if (s_runN >= DP_SVC_N) return;
    for (int i = 0; i < s_runN; i++) if (s_run[i].port == port && s_run[i].proto == proto) return;  // dedup
    s_run[s_runN].port = port; s_run[s_runN].proto = proto;
    strncpy(s_run[s_runN].name, name, 7); s_run[s_runN].name[7] = 0;
    s_runN++;
}

// Build the run list from the filter. Empty filter = all built-in services. Tokens
// (comma-separated): `service` (ssh → all built-in rows of that service) · `port`
// (554 → the built-in row on that port) · `service:port` (ssh:2222 → a CUSTOM target:
// that protocol on a non-standard port). Returns the target count (0 = nothing matched).
static int dpBuildRun(const String& filter) {
    s_runN = 0;
    if (filter.length() == 0) {
        for (int i = 0; i < DP_SVC_N; i++) dpAddRun(DP_SVCS[i].port, DP_SVCS[i].proto, DP_SVCS[i].name);
        return s_runN;
    }
    int start = 0;
    while (start < (int)filter.length()) {
        int comma = filter.indexOf(',', start);
        String t = (comma < 0) ? filter.substring(start) : filter.substring(start, comma);
        t.trim(); t.toLowerCase();
        if (t.length()) {
            int colon = t.indexOf(':');
            if (colon > 0) {                                      // service:port custom target
                int proto = dpProtoFromName(t.substring(0, colon));
                long port = t.substring(colon + 1).toInt();
                if (proto >= 0 && port > 0 && port < 65536)
                    dpAddRun((uint16_t)port, (uint8_t)proto, dpProtoLabel(proto));
            } else {
                bool isNum = t.length() > 0;
                for (unsigned k = 0; k < t.length(); k++) if (!isdigit((unsigned char)t[k])) { isNum = false; break; }
                for (int i = 0; i < DP_SVC_N; i++) {
                    bool m;
                    if (isNum) m = (DP_SVCS[i].port == (uint16_t)t.toInt());
                    else { String nm = DP_SVCS[i].name; nm.trim(); nm.toLowerCase(); m = (nm == t); }
                    if (m) dpAddRun(DP_SVCS[i].port, DP_SVCS[i].proto, DP_SVCS[i].name);
                }
            }
        }
        if (comma < 0) break;
        start = comma + 1;
    }
    return s_runN;
}

static void dpDrawRow(int i) {
    if (displayManager.isBlocked()) return;
    int y = dpRowY(i);
    displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, dpRowBg(i));
    // finding marker — a bright "!" so hits/open rows jump out when scanning
    if (s_state[i] == ST_HIT || s_state[i] == ST_OPEN) {
        displayManager.setCursor(DP_COL_MARK, y);
        displayManager.setTextColor(s_state[i] == ST_HIT ? TFT_GREEN : TFT_ORANGE);
        displayManager.printText("!");
    }
    displayManager.setCursor(DP_COL_PORT, y);
    char pn[8]; snprintf(pn, sizeof(pn), "%-5u", s_run[i].port);
    displayManager.setTextColor(0x7BEF);   displayManager.printText(pn);
    displayManager.setCursor(DP_COL_SVC, y);
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(s_run[i].name);
    displayManager.setCursor(DP_COL_STAT, y);
    char line[40];
    switch (s_state[i]) {
        case ST_PENDING: displayManager.setTextColor(0x39E7);   displayManager.printText("."); break;
        case ST_TESTING: displayManager.setTextColor(TFT_YELLOW);displayManager.printText("testing..."); break;
        case ST_CLOSED:  displayManager.setTextColor(0x39E7);   displayManager.printText("closed"); break;
        case ST_NODEF:   displayManager.setTextColor(0x7BEF);   displayManager.printText("open, no default"); break;
        case ST_OPEN:
            displayManager.setTextColor(TFT_ORANGE);
            snprintf(line, sizeof(line), "%s", s_hp[i]);         // e.g. "NO AUTH" / "no auth on /"
            displayManager.printText(line);
            break;
        case ST_HIT:
            displayManager.setTextColor(TFT_GREEN);
            snprintf(line, sizeof(line), "%s:%s", s_hu[i], s_hp[i]);
            displayManager.printText(line);
            break;
    }
}

static void dpDrawChrome(IPAddress ip) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("DPWO");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("AUDIT");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("]  ");
    displayManager.setTextColor(TFT_WHITE);  displayManager.println(ip.toString().c_str());
    displayManager.setCursor(0, outputY + LINE_HEIGHT + 2);
    displayManager.printSeparator();
    for (int i = 0; i < s_runN; i++) dpDrawRow(i);
    displayManager.setCursor(0, 210);
    displayManager.printSeparator();
}

// Live progress on the right of the header line (so slow rows don't look frozen).
static void dpDrawProgress(int done, int total) {
    if (displayManager.isBlocked()) return;
    displayManager.fillRect(224, outputY - 1, SCREEN_WIDTH - 224, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(230, outputY);
    char b[16]; snprintf(b, sizeof(b), "%d/%d", done, total);
    displayManager.setTextColor(TFT_CYAN); displayManager.printText(b);
    displayManager.setTextColor(TFT_WHITE);
}

// Header-right run tally (hit/open counts, or an "unreachable?" hint) — colours double as
// the legend. Drawn in the same slot the live progress used during the scan. `unreachable`
// = every tested port was closed → the host likely isn't reachable (isolated / down).
static void dpDrawCounts(int nHit, int nOpen, bool unreachable) {
    if (displayManager.isBlocked()) return;
    displayManager.fillRect(224, outputY - 1, SCREEN_WIDTH - 224, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(228, outputY);
    if (unreachable) {
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("unreach?");
        displayManager.setTextColor(TFT_WHITE); return;
    }
    char b[16];
    displayManager.setTextColor(nHit  ? TFT_GREEN  : 0x7BEF);
    snprintf(b, sizeof(b), "hit %d",  nHit);  displayManager.printText(b);
    displayManager.setTextColor(nOpen ? TFT_ORANGE : 0x7BEF);
    snprintf(b, sizeof(b), " open %d", nOpen); displayManager.printText(b);
    displayManager.setTextColor(TFT_WHITE);
}

// Footer shown while a scan is running.
static void dpDrawFooterScan() {
    if (displayManager.isBlocked()) return;
    displayManager.fillRect(0, 212, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF); displayManager.printText("[q] stop");
    displayManager.setTextColor(TFT_WHITE);
}

// Footer for the interactive results view: trackpad picks a row, keys re-scan.
static void dpDrawFooterResults() {
    if (displayManager.isBlocked()) return;
    displayManager.fillRect(0, 212, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(6, 214);
    struct { const char* k; const char* label; } keys[] = {
        { "r", " rescan " }, { "a", " all " }, { "q", " quit  " },
    };
    for (auto& e : keys) {
        displayManager.setTextColor(0x7BEF);    displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);  displayManager.printText(e.k);
        displayManager.setTextColor(0x7BEF);    displayManager.printText("]");
        displayManager.setTextColor(TFT_WHITE); displayManager.printText(e.label);
    }
    displayManager.setTextColor(0x7BEF); displayManager.printText("^v pick");
    displayManager.setTextColor(TFT_WHITE);
}

static int dpRunSvc(int i, IPAddress ip) {
    uint16_t port = s_run[i].port;
    switch (s_run[i].proto) {
        case SV_HTTP:   return dpHttp(ip, port, s_hu[i], s_hp[i]);
        case SV_TELNET: return dpTelnet(ip, port, s_hu[i], s_hp[i]);
        case SV_FTP:    return dpFtp(ip, port, s_hu[i], s_hp[i]);
        case SV_RTSP:   return dpRtsp(ip, port, s_hu[i], s_hp[i]);
        case SV_REDIS:  return dpRedis(ip, port, s_hu[i], s_hp[i]);
        case SV_SNMP:   return dpSnmp(ip, port, s_hu[i], s_hp[i]);
        case SV_SSH:    return dpSsh(ip, port, s_hu[i], s_hp[i]);
        case SV_MQTT:   return dpMqtt(ip, port, s_hu[i], s_hp[i]);
    }
    return 0;
}

// One-shot save of the current HIT/OPEN rows (called once on exit, so per-row rescans
// don't append duplicates). No-op when there's nothing to save or no SD.
static void dpSaveHits(IPAddress ip) {
    if (!sdCardManager.canAccessSD()) return;
    int any = 0;
    for (int i = 0; i < s_runN; i++) if (s_state[i] == ST_HIT || s_state[i] == ST_OPEN) any++;
    if (!any) return;
    sdCardManager.ensureDir(SD_DIR_DPWO);
    File f = SD.open(SD_DIR_DPWO "/results.csv", FILE_APPEND);
    if (!f) return;
    for (int i = 0; i < s_runN; i++)
        if (s_state[i] == ST_HIT || s_state[i] == ST_OPEN)
            f.printf("%s,%u,%s,%s,%s\n", ip.toString().c_str(), s_run[i].port,
                     s_run[i].name, s_hu[i], s_hp[i]);
    f.close();
}

// ── scan orchestration + interactive results ─────────────────────────────────────────
static void dpResetStates() {
    for (int i = 0; i < s_runN; i++) { s_state[i] = ST_PENDING; s_hu[i][0] = 0; s_hp[i][0] = 0; }
}

// Test one service row: mark testing, run it, store the result, redraw. Returns the raw
// dpRunSvc code (-2 = the user pressed q mid-test → the row's previous state is kept, so
// a single-row rescan can be cancelled without losing the earlier result).
static int dpScanRow(int i, IPAddress ip) {
    uint8_t prev = s_state[i];
    s_state[i] = ST_TESTING; dpDrawRow(i);
    s_curRow = i;                       // lets dpTrying / dpSsh repaint live progress here
    int r = dpRunSvc(i, ip);
    s_curRow = -1;
    switch (r) {
        case -2: s_state[i] = prev;      break;
        case -1: s_state[i] = ST_CLOSED; break;
        case  0: s_state[i] = ST_NODEF;  break;
        case  1: s_state[i] = ST_HIT;    break;
        case  2: s_state[i] = ST_OPEN;   break;
    }
    dpDrawRow(i);
    return r;
}

// Run every row in order (states must be pre-reset). Draws live progress. Returns true if
// the user aborted with q — but the caller shows the (partial) results either way.
static bool dpScanAll(IPAddress ip) {
    int done = 0;
    dpDrawProgress(done, s_runN);
    bool aborted = false;
    for (int i = 0; i < s_runN && !aborted; i++) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            dpDrawChrome(ip); dpDrawFooterScan(); dpDrawProgress(done, s_runN);
        }
        int r = dpScanRow(i, ip);
        if (r == -2) aborted = true;
        else { done++; dpDrawProgress(done, s_runN); }
        if (!aborted && dpAbort()) aborted = true;
    }
    return aborted;
}

static void dpTally(int& nHit, int& nOpen, bool& unreachable) {
    int nClosed = 0, nTested = 0; nHit = 0; nOpen = 0;
    for (int i = 0; i < s_runN; i++) {
        if (s_state[i] == ST_PENDING) continue;
        nTested++;
        if      (s_state[i] == ST_HIT)    nHit++;
        else if (s_state[i] == ST_OPEN)   nOpen++;
        else if (s_state[i] == ST_CLOSED) nClosed++;
    }
    unreachable = (nTested > 0 && nClosed == nTested);
}

// Full repaint of the interactive results view (chrome + selection highlight + counts + footer).
static void dpRedrawResults(IPAddress ip) {
    int nHit, nOpen; bool un; dpTally(nHit, nOpen, un);
    dpDrawChrome(ip);
    dpDrawCounts(nHit, nOpen, un);
    dpDrawFooterResults();
}

// ── entry ─────────────────────────────────────────────────────────────────────────────
void runDpwo(char* args) {
    if (WiFi.status() != WL_CONNECTED) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Not on a network. Run cw first.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    String all = args ? String(args) : String();
    all.trim();
    if (all.length() == 0) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Usage: dw <ip|nd#|ns#> [svc|port|svc:port]");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("e.g. dw 192.168.1.10 ssh   dw .. ssh:2222");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    // Split target token from an optional service/port filter (quiet mode).
    int sp = all.indexOf(' ');
    String tok    = (sp < 0) ? all : all.substring(0, sp);
    String filter = (sp < 0) ? String() : all.substring(sp + 1);
    filter.trim();

    IPAddress ip;
    if (!resolveNetTarget(tok, ip)) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Can't resolve target.");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Use an IP, or nd#/ns# after a scan.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    // Build the run list (default = all services; filter selects services/ports/custom).
    if (dpBuildRun(filter) == 0) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No service/port matches that.");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Try: ftp ssh telnet http rtsp redis mqtt snmp");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("or svc:port, e.g. ssh:2222  http:8443");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    dpLoadCreds();
    dpResetStates();
    s_sel = -1;                          // no selection highlight while the first scan runs
    dpDrawChrome(ip);
    dpDrawFooterScan();
    dpScanAll(ip);                       // initial full sweep (q stops early → still show results)

    // Interactive results view — do NOT exit on a keypress. Trackpad picks a row; [r] (or
    // trackball click) re-scans just that service in place, [a] re-scans everything, [q] leaves.
    s_sel = 0;
    dpRedrawResults(ip);
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) dpRedrawResults(ip);

        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP && s_sel > 0)                 { int o = s_sel; s_sel--; dpDrawRow(o); dpDrawRow(s_sel); }
        else if (tb == TBALL_DOWN && s_sel < s_runN - 1) { int o = s_sel; s_sel++; dpDrawRow(o); dpDrawRow(s_sel); }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (k == 'r' || k == 'R' || tb == TBALL_CLICK) {          // re-scan the selected service
            dpScanRow(s_sel, ip);                                 // row stays highlighted (s_sel==row)
            int nHit, nOpen; bool un; dpTally(nHit, nOpen, un);
            dpDrawCounts(nHit, nOpen, un);
        } else if (k == 'a' || k == 'A') {                        // re-scan everything
            int sv = s_sel; s_sel = -1;
            dpResetStates();
            dpDrawChrome(ip); dpDrawFooterScan();
            dpScanAll(ip);
            s_sel = sv;
            dpRedrawResults(ip);
        }
        delay(20);
    }

    dpFreeCreds();                       // rule 5c — creds held through rescans, freed on exit
    dpSaveHits(ip);                      // persist current HIT/OPEN rows once (no per-rescan dupes)
    displayManager.clearScreen();        // wipe the table so the CLI prompt doesn't overprint it
    displayManager.setCursor(10, outputY);
    displayManager.printCommandScreen();
}
