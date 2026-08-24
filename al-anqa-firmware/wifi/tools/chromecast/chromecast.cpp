// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// cast / ca — Google Cast control (Network).
//
// Discovers Cast receivers (Chromecast, Google TV, Nest displays, cast-enabled
// TVs) on the LAN and drives them two ways:
//
//   DIAL   (HTTP  :8008, no auth)  — launch an app / YouTube video, stop it.
//                                    Light, no TLS. The "rickroll" path.
//   Cast v2(TLS   :8009, protobuf) — real playback: load a media URL, play,
//                                    pause, stop, volume, now-playing status.
//
// The Cast v2 CastMessage envelope is hand-rolled (6 protobuf fields, JSON in
// payload_utf8) — no nanopb, matching dpwo/isoscan's hand-built frames. JSON is
// built/parsed with ArduinoJson (already a dep). Everything is a plain STA
// socket (no promiscuous / no soft-AP) → NO GDMA concern, SD writes safe anytime.
// Requires cw (WL_CONNECTED); target via the shared resolveNetTarget (ip/nd#/ns#)
// or the interactive mDNS picker.
//
// HONEST: own devices only (it drives someone's TV — destructive actions confirm
// first). DIAL YouTube launching works today but Google has narrowed DIAL over
// the years; if a device refuses, use the Cast v2 path. The Default Media
// Receiver plays direct media URLs (mp4/HLS), not YouTube page URLs.
//
// Sources / method (NOTICES — references, no code copied): thibauts/node-castv2
// (Cast v2 protocol), andrasbiro/ArduCastControl + amitn/ESPCaster (ESP32 prior
// art), ivan-krukov/chromecast_dial (DIAL rickroll), Wikipedia "Discovery and
// Launch" (DIAL/SSDP). Full plan: docs/plans/chromecast-cast.md.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <SD.h>

#include "chromecast.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "network_scanner.h"   // resolveNetTarget()
#include "sdcard_manager.h"    // SD_DIR_CHROMECAST

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── protocol constants ─────────────────────────────────────────────────────────────
static const uint16_t DIAL_PORT = 8008;
static const uint16_t CAST_PORT = 8009;

static const char* NS_CONN  = "urn:x-cast:com.google.cast.tp.connection";
static const char* NS_BEAT  = "urn:x-cast:com.google.cast.tp.heartbeat";
static const char* NS_RECV  = "urn:x-cast:com.google.cast.receiver";
static const char* NS_MEDIA = "urn:x-cast:com.google.cast.media";
static const char* APP_DMR  = "CC1AD845";                 // Default Media Receiver
static const char* SENDER   = "sender-0";
static const char* RECEIVER = "receiver-0";

// Presets (overridable via CLI).
static const char* RICKROLL_VID = "dQw4w9WgXcQ";          // DIAL YouTube video id
static const char* SAMPLE_MP4   =                          // Cast v2 "play test video"
    "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";

// ── discovered devices ─────────────────────────────────────────────────────────────
#define CAST_MAX 16
struct CastDev { IPAddress ip; String name; String model; };
static CastDev  s_dev[CAST_MAX];
static int      s_devCount = 0;
static int      s_reqId    = 1;          // Cast v2 requestId counter

// Saved content loaded from SD (/apps/cast/media.csv: "name,url-or-youtube-id").
// target starting with http(s) → Cast v2 LOAD; otherwise treated as a YouTube
// video id launched via DIAL.
#define CAST_MEDIA_MAX 32
struct MediaItem { String name; String target; };
static MediaItem s_media[CAST_MEDIA_MAX];
static int       s_mediaCount = 0;

// ── small UI helpers ────────────────────────────────────────────────────────────────
static void castMsg(const char* line, uint16_t col, int row) {
    displayManager.setCursor(6, outputY + LINE_HEIGHT * row);
    displayManager.setTextColor(col);
    displayManager.println(line);
    displayManager.setTextColor(TFT_WHITE);
}

// ════════════════════════════════════════════════════════════════════════════════════
// DIAL (HTTP :8008)
// ════════════════════════════════════════════════════════════════════════════════════

// Read the numeric HTTP status code from a fresh response ("HTTP/1.1 NNN ...").
static int httpStatus(WiFiClient& c, uint32_t toMs) {
    uint32_t t0 = millis();
    String line;
    while (millis() - t0 < toMs) {
        if (c.available()) { line = c.readStringUntil('\n'); break; }
        delay(2);
    }
    int sp = line.indexOf(' ');
    if (sp < 0) return -1;
    return line.substring(sp + 1, sp + 4).toInt();
}

static inline bool dialOk(int st) { return st >= 200 && st < 400; }

// POST /apps/<app> with an optional form body → launch. Returns HTTP status.
static int dialLaunch(const IPAddress& ip, const char* app, const String& body) {
    WiFiClient c;
    if (!c.connect(ip, DIAL_PORT, 4000)) return -1;
    String req = String("POST /apps/") + app + " HTTP/1.1\r\n"
               + "Host: " + ip.toString() + "\r\n"
               + "Content-Type: application/x-www-form-urlencoded\r\n"
               + "Content-Length: " + body.length() + "\r\n"
               + "Connection: close\r\n\r\n" + body;
    c.print(req);
    int st = httpStatus(c, 5000);
    c.stop();
    return st;
}

// DELETE /apps/<app>/run → stop.
static int dialStop(const IPAddress& ip, const char* app) {
    WiFiClient c;
    if (!c.connect(ip, DIAL_PORT, 4000)) return -1;
    c.print(String("DELETE /apps/") + app + "/run HTTP/1.1\r\nHost: "
            + ip.toString() + "\r\nConnection: close\r\n\r\n");
    int st = httpStatus(c, 5000);
    c.stop();
    return st;
}

// GET /setup/eureka_info → friendly device name (best-effort; blank on failure).
static String dialDeviceName(const IPAddress& ip) {
    WiFiClient c;
    if (!c.connect(ip, DIAL_PORT, 3000)) return "";
    c.print(String("GET /setup/eureka_info HTTP/1.1\r\nHost: ") + ip.toString()
            + "\r\nConnection: close\r\n\r\n");
    uint32_t t0 = millis(); String body; bool inBody = false;
    while (millis() - t0 < 4000 && c.connected()) {
        while (c.available()) {
            String l = c.readStringUntil('\n');
            if (inBody) body += l;
            else if (l == "\r" || l.length() == 0) inBody = true;
        }
        delay(2);
    }
    c.stop();
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
        const char* n = doc["name"] | (const char*)nullptr;
        if (n) return String(n);
    }
    return "";
}

// ════════════════════════════════════════════════════════════════════════════════════
// Cast v2 (protobuf CastMessage over TLS :8009) — hand-rolled envelope
// ════════════════════════════════════════════════════════════════════════════════════

// Send one CastMessage: 4-byte big-endian length prefix + serialized protobuf.
static bool castSend(WiFiClientSecure& c, const char* ns, const char* dest,
                     const String& payload) {
    static uint8_t buf[1400];
    size_t n = 0;
    auto put = [&](uint8_t b) { if (n < sizeof(buf)) buf[n++] = b; };
    auto putVar = [&](uint32_t v) { while (v >= 0x80) { put((v & 0x7F) | 0x80); v >>= 7; } put(v & 0x7F); };
    auto putLen = [&](uint8_t tag, const char* s, size_t l) {
        put(tag); putVar(l); for (size_t i = 0; i < l; i++) put((uint8_t)s[i]);
    };
    put(0x08); put(0x00);                                  // 1 protocol_version = 0
    putLen(0x12, SENDER, strlen(SENDER));                  // 2 source_id
    putLen(0x1A, dest,   strlen(dest));                    // 3 destination_id
    putLen(0x22, ns,     strlen(ns));                      // 4 namespace
    put(0x28); put(0x00);                                  // 5 payload_type = STRING
    putLen(0x32, payload.c_str(), payload.length());       // 6 payload_utf8
    uint8_t hdr[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
    if (c.write(hdr, 4) != 4) return false;
    return c.write(buf, n) == n;
}

// Receive one CastMessage → namespace + payload_utf8. false on timeout/parse-fail.
static bool castRecv(WiFiClientSecure& c, String& ns, String& payload, uint32_t toMs) {
    uint32_t t0 = millis();
    uint8_t lb[4]; int got = 0;
    while (got < 4) {
        if (millis() - t0 > toMs) return false;
        if (c.available()) lb[got++] = c.read(); else delay(2);
    }
    uint32_t len = ((uint32_t)lb[0] << 24) | ((uint32_t)lb[1] << 16)
                 | ((uint32_t)lb[2] << 8)  |  (uint32_t)lb[3];
    if (len == 0 || len > 8192) return false;
    uint8_t* body = (uint8_t*)malloc(len);
    if (!body) return false;
    got = 0;
    while ((uint32_t)got < len) {
        if (millis() - t0 > toMs) { free(body); return false; }
        if (c.available()) body[got++] = c.read(); else delay(2);
    }
    ns = ""; payload = "";
    size_t i = 0;
    while (i < len) {
        uint8_t tag = body[i++]; uint8_t field = tag >> 3; uint8_t wt = tag & 7;
        if (wt == 0) {                                    // varint — skip
            while (i < len && (body[i++] & 0x80)) {}
        } else if (wt == 2) {                             // length-delimited
            uint32_t l = 0; int sh = 0;
            while (i < len) { uint8_t b = body[i++]; l |= (uint32_t)(b & 0x7F) << sh; if (!(b & 0x80)) break; sh += 7; }
            if (i + l > len) break;
            if (field == 4) { for (uint32_t k = 0; k < l; k++) ns += (char)body[i + k]; }
            else if (field == 6) { for (uint32_t k = 0; k < l; k++) payload += (char)body[i + k]; }
            i += l;
        } else break;                                     // unsupported wire type
    }
    free(body);
    return true;
}

// Read messages until one whose payload contains `want`; auto-PONG heartbeats.
// Returns the matching payload in `out`. false on timeout.
static bool castPumpUntil(WiFiClientSecure& c, const char* want, String& out, uint32_t toMs) {
    uint32_t t0 = millis();
    String ns, pl;
    while (millis() - t0 < toMs) {
        if (!castRecv(c, ns, pl, toMs - (millis() - t0))) return false;
        if (ns == NS_BEAT && pl.indexOf("PING") >= 0) { castSend(c, NS_BEAT, RECEIVER, "{\"type\":\"PONG\"}"); continue; }
        if (pl.indexOf(want) >= 0) { out = pl; return true; }
    }
    return false;
}

// Open a TLS Cast channel + virtual-connect to receiver-0. false = connect failed.
static bool castOpen(WiFiClientSecure& c, const IPAddress& ip) {
    c.setInsecure();                                       // Cast uses a self-signed cert
    if (!c.connect(ip, CAST_PORT, 8000)) return false;
    castSend(c, NS_CONN, RECEIVER, "{\"type\":\"CONNECT\"}");
    return true;
}

// Set device master volume 0..100 (%). Own connection, no session needed.
static bool castVolume(const IPAddress& ip, int pct, bool mute, bool doMute) {
    WiFiClientSecure c;
    if (!castOpen(c, ip)) return false;
    JsonDocument d; d["type"] = "SET_VOLUME"; d["requestId"] = s_reqId++;
    JsonObject v = d["volume"].to<JsonObject>();
    if (doMute) v["muted"] = mute; else v["level"] = pct / 100.0f;
    String out; serializeJson(d, out);
    bool ok = castSend(c, NS_RECV, RECEIVER, out);
    String tmp; castPumpUntil(c, "RECEIVER_STATUS", tmp, 3000);
    c.stop();
    return ok;
}

// LAUNCH the Default Media Receiver and LOAD a URL with explicit content/stream
// type (images use contentType image/*, streamType NONE; video uses BUFFERED).
static bool castLoadMediaEx(const IPAddress& ip, const String& url,
                            const char* contentType, const char* streamType, String& err) {
    WiFiClientSecure c;
    if (!castOpen(c, ip)) { err = "TLS connect failed (:8009)"; return false; }

    // LAUNCH Default Media Receiver, wait for RECEIVER_STATUS → transportId.
    { JsonDocument d; d["type"] = "LAUNCH"; d["appId"] = APP_DMR; d["requestId"] = s_reqId++;
      String o; serializeJson(d, o); castSend(c, NS_RECV, RECEIVER, o); }
    String status;
    if (!castPumpUntil(c, "\"transportId\"", status, 9000)) { err = "no receiver status"; c.stop(); return false; }
    JsonDocument sd;
    if (deserializeJson(sd, status)) { err = "status parse"; c.stop(); return false; }
    const char* transport = sd["status"]["applications"][0]["transportId"] | (const char*)nullptr;
    if (!transport) { err = "no transportId"; c.stop(); return false; }
    String tId = transport;

    // Virtual-connect to the app, then LOAD.
    castSend(c, NS_CONN, tId.c_str(), "{\"type\":\"CONNECT\"}");
    { JsonDocument d; d["type"] = "LOAD"; d["requestId"] = s_reqId++; d["autoplay"] = true;
      JsonObject m = d["media"].to<JsonObject>();
      m["contentId"] = url; m["streamType"] = streamType; m["contentType"] = contentType;
      String o; serializeJson(d, o); castSend(c, NS_MEDIA, tId.c_str(), o); }

    String mstat;
    bool ok = castPumpUntil(c, "MEDIA_STATUS", mstat, 8000);
    c.stop();
    if (!ok) err = "loaded, no media status";     // often still plays; treat as soft-ok
    return true;
}

// Auto content-type from URL extension (video path). Wrapper over castLoadMediaEx.
static bool castLoadMedia(const IPAddress& ip, const String& url, String& err) {
    const char* ctype = url.endsWith(".m3u8") ? "application/x-mpegurl"
                      : url.endsWith(".mpd")  ? "application/dash+xml" : "video/mp4";
    return castLoadMediaEx(ip, url, ctype, "BUFFERED", err);
}

// Find the running app's transportId + media mediaSessionId (for play/pause/stop).
static bool castFindSession(WiFiClientSecure& c, String& tId, int& mediaSess) {
    { JsonDocument d; d["type"] = "GET_STATUS"; d["requestId"] = s_reqId++;
      String o; serializeJson(d, o); castSend(c, NS_RECV, RECEIVER, o); }
    String status;
    if (!castPumpUntil(c, "\"transportId\"", status, 6000)) return false;
    JsonDocument sd;
    if (deserializeJson(sd, status)) return false;
    const char* t = sd["status"]["applications"][0]["transportId"] | (const char*)nullptr;
    if (!t) return false;
    tId = t;
    castSend(c, NS_CONN, tId.c_str(), "{\"type\":\"CONNECT\"}");
    { JsonDocument d; d["type"] = "GET_STATUS"; d["requestId"] = s_reqId++;
      String o; serializeJson(d, o); castSend(c, NS_MEDIA, tId.c_str(), o); }
    String mstat;
    if (!castPumpUntil(c, "MEDIA_STATUS", mstat, 5000)) return false;
    JsonDocument md;
    if (deserializeJson(md, mstat)) return false;
    mediaSess = md["status"][0]["mediaSessionId"] | -1;
    return mediaSess >= 0;
}

// PLAY / PAUSE / STOP the current media session.
static bool castMediaCtl(const IPAddress& ip, const char* type) {
    WiFiClientSecure c;
    if (!castOpen(c, ip)) return false;
    String tId; int sess = -1;
    if (!castFindSession(c, tId, sess)) { c.stop(); return false; }
    JsonDocument d; d["type"] = type; d["mediaSessionId"] = sess; d["requestId"] = s_reqId++;
    String o; serializeJson(d, o);
    bool ok = castSend(c, NS_MEDIA, tId.c_str(), o);
    String tmp; castPumpUntil(c, "MEDIA_STATUS", tmp, 3000);
    c.stop();
    return ok;
}

// Now-playing title from a media status (best-effort, blank if idle).
static String castNowPlaying(const IPAddress& ip) {
    WiFiClientSecure c;
    if (!castOpen(c, ip)) return "";
    String tId; int sess = -1;
    if (!castFindSession(c, tId, sess)) { c.stop(); return ""; }
    { JsonDocument d; d["type"] = "GET_STATUS"; d["requestId"] = s_reqId++;
      String o; serializeJson(d, o); castSend(c, NS_MEDIA, tId.c_str(), o); }
    String mstat; String title = "";
    if (castPumpUntil(c, "MEDIA_STATUS", mstat, 4000)) {
        JsonDocument md;
        if (!deserializeJson(md, mstat)) {
            const char* t = md["status"][0]["media"]["metadata"]["title"] | (const char*)nullptr;
            if (t) title = t;
        }
    }
    c.stop();
    return title;
}

// ════════════════════════════════════════════════════════════════════════════════════
// Saved content (SD /apps/cast/media.csv)
// ════════════════════════════════════════════════════════════════════════════════════
static void seedMediaFile() {
    if (!sdCardManager.isReady()) return;
    if (SD.exists(SD_DIR_CHROMECAST "/media.csv")) return;
    sdCardManager.ensureDir(SD_DIR_CHROMECAST);
    sdCardManager.appendLine(SD_DIR_CHROMECAST "/media.csv", "# cast saved content — name,url-or-youtube-id");
    sdCardManager.appendLine(SD_DIR_CHROMECAST "/media.csv", "# http(s) URL = Cast v2 media; bare id = YouTube via DIAL");
    sdCardManager.appendLine(SD_DIR_CHROMECAST "/media.csv", String("Rickroll,") + RICKROLL_VID);
    sdCardManager.appendLine(SD_DIR_CHROMECAST "/media.csv", String("Big Buck Bunny,") + SAMPLE_MP4);
}

// (Re)load the saved-content list. Seeds the file on first use; falls back to
// built-in presets in RAM when there's no SD.
static void loadMedia() {
    s_mediaCount = 0;
    seedMediaFile();
    if (sdCardManager.isReady() && SD.exists(SD_DIR_CHROMECAST "/media.csv")) {
        File f = SD.open(SD_DIR_CHROMECAST "/media.csv", FILE_READ);
        while (f && f.available() && s_mediaCount < CAST_MEDIA_MAX) {
            String l = f.readStringUntil('\n'); l.trim();
            if (l.length() == 0 || l.startsWith("#")) continue;
            int comma = l.indexOf(',');
            if (comma < 0) continue;
            MediaItem m; m.name = l.substring(0, comma); m.target = l.substring(comma + 1);
            m.name.trim(); m.target.trim();
            if (m.target.length()) s_media[s_mediaCount++] = m;
        }
        if (f) f.close();
    }
    if (s_mediaCount == 0) {                                // no SD → built-in presets
        s_media[s_mediaCount].name = "Rickroll";        s_media[s_mediaCount++].target = RICKROLL_VID;
        s_media[s_mediaCount].name = "Big Buck Bunny";  s_media[s_mediaCount++].target = SAMPLE_MP4;
    }
}

// Resolve a saved item by (case-insensitive) name → its target. false if none.
static bool savedTarget(const String& name, String& out) {
    loadMedia();
    for (int i = 0; i < s_mediaCount; i++)
        if (s_media[i].name.equalsIgnoreCase(name)) { out = s_media[i].target; return true; }
    return false;
}

// Play any target string: http(s) → Cast v2 LOAD; else YouTube video id → DIAL.
static bool playTarget(const IPAddress& ip, const String& target, String& err) {
    if (target.startsWith("http")) return castLoadMedia(ip, target, err);
    bool ok = dialOk(dialLaunch(ip, "YouTube", String("v=") + target));
    if (!ok) err = "DIAL refused";
    return ok;
}

// ════════════════════════════════════════════════════════════════════════════════════
// Share a LOCAL SD file — the T-Deck serves it over HTTP and the Cast device pulls it
// ════════════════════════════════════════════════════════════════════════════════════
#define CAST_HTTP_PORT 8123
#define CAST_SHARE_DIR SD_DIR_CHROMECAST "/share"

static const char* mimeFor(const String& name) {
    String n = name; n.toLowerCase();
    if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
    if (n.endsWith(".png"))  return "image/png";
    if (n.endsWith(".gif"))  return "image/gif";
    if (n.endsWith(".webp")) return "image/webp";
    if (n.endsWith(".bmp"))  return "image/bmp";
    if (n.endsWith(".mp4") || n.endsWith(".m4v")) return "video/mp4";
    if (n.endsWith(".webm")) return "video/webm";
    if (n.endsWith(".mkv"))  return "video/x-matroska";
    return "application/octet-stream";
}
static bool isImageFile(const String& n) { return String(mimeFor(n)).startsWith("image/"); }
static bool isMediaFile(const String& n) { String m = mimeFor(n); return m.startsWith("image/") || m.startsWith("video/"); }

// Serve one HTTP request for the single shared file (GET/HEAD, honours Range so
// the Cast device can seek video). SD READS only (no writes) → GDMA-safe on STA.
static void handleShareClient(WiFiClient& c, const String& path, const char* ctype,
                              unsigned long& servedBytes) {
    String reqLine = c.readStringUntil('\n');
    long rStart = -1, rEnd = -1;
    while (c.connected()) {
        String h = c.readStringUntil('\n');
        if (h.length() == 0 || h == "\r") break;
        if (h.startsWith("Range:") || h.startsWith("range:")) {
            int eq = h.indexOf('='), dash = h.indexOf('-', eq);
            if (eq >= 0 && dash > eq) {
                rStart = h.substring(eq + 1, dash).toInt();
                String e = h.substring(dash + 1); e.trim();
                if (e.length()) rEnd = e.toInt();
            }
        }
    }
    bool head = reqLine.startsWith("HEAD");
    File f = SD.open(path, FILE_READ);
    if (!f) { c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"); c.stop(); return; }
    long size = f.size();
    long start = (rStart >= 0) ? rStart : 0;
    long end   = (rEnd   >= 0) ? rEnd   : size - 1;
    if (start < 0) start = 0;
    if (end >= size || end < start) end = size - 1;
    long len = end - start + 1;

    String hdr = String(rStart >= 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n")
               + "Content-Type: " + ctype + "\r\n"
               + "Accept-Ranges: bytes\r\n"
               + "Content-Length: " + len + "\r\n";
    if (rStart >= 0)
        hdr += "Content-Range: bytes " + String(start) + "-" + String(end) + "/" + String(size) + "\r\n";
    hdr += "Connection: close\r\n\r\n";
    c.print(hdr);

    if (!head) {
        f.seek(start);
        static uint8_t buf[1460];
        long remaining = len;
        while (remaining > 0 && c.connected()) {
            int chunk = (remaining > (long)sizeof(buf)) ? (int)sizeof(buf) : (int)remaining;
            int rd = f.read(buf, chunk);
            if (rd <= 0) break;
            c.write(buf, rd);
            remaining -= rd; servedBytes += rd;
        }
    }
    f.close();
    c.stop();
}

// ════════════════════════════════════════════════════════════════════════════════════
// SD logging
// ════════════════════════════════════════════════════════════════════════════════════
static void logDevice(const CastDev& d) {
    if (!sdCardManager.isReady()) return;
    sdCardManager.ensureDir(SD_DIR_CHROMECAST);
    String line = d.ip.toString() + "," + d.name + "," + d.model;
    sdCardManager.appendLine(SD_DIR_CHROMECAST "/devices.csv", line);
}

// ════════════════════════════════════════════════════════════════════════════════════
// Discovery (mDNS _googlecast._tcp)
// ════════════════════════════════════════════════════════════════════════════════════
static void castScan() {
    s_devCount = 0;
    displayManager.clearScreen();
    castMsg("[CAST] scanning mDNS _googlecast._tcp ...", TFT_CYAN, 0);

    MDNS.begin("alanqa-ca");                                // idempotent enough for querying
    int n = MDNS.queryService("googlecast", "tcp");         // ~ blocks a couple seconds
    for (int i = 0; i < n && s_devCount < CAST_MAX; i++) {
        CastDev d;
        d.ip    = MDNS.IP(i);
        d.name  = MDNS.txt(i, "fn");                        // friendly name
        d.model = MDNS.txt(i, "md");                        // model
        if (d.name.length() == 0) d.name = dialDeviceName(d.ip);
        if (d.name.length() == 0) d.name = MDNS.hostname(i);
        if (d.name.length() == 0) d.name = d.ip.toString();
        s_dev[s_devCount++] = d;
        logDevice(d);
    }
}

// ════════════════════════════════════════════════════════════════════════════════════
// Interactive UI
// ════════════════════════════════════════════════════════════════════════════════════
static void drawHeader(const char* sub, const String& who) {
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("CAST");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText(sub);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("]  ");
    displayManager.setTextColor(TFT_WHITE);  displayManager.println(who.c_str());
}

static void footer(const char* txt) {
    displayManager.fillRect(0, SCREEN_HEIGHT - LINE_HEIGHT, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(4, SCREEN_HEIGHT - LINE_HEIGHT + 2);
    displayManager.setTextColor(0x7BEF);
    displayManager.println(txt);
    displayManager.setTextColor(TFT_WHITE);
}

// A transient status line at row 12 (above the footer).
static void toast(const char* txt, uint16_t col) {
    int y = SCREEN_HEIGHT - LINE_HEIGHT * 2;
    displayManager.fillRect(0, y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(4, y);
    displayManager.setTextColor(col);
    displayManager.println(txt);
    displayManager.setTextColor(TFT_WHITE);
}

// Device picker → returns chosen index, or -1 on quit.
static int pickDevice() {
    int sel = 0, top = 0;
    const int rows = (SCREEN_HEIGHT - outputY - LINE_HEIGHT * 3) / LINE_HEIGHT;
    bool redraw = true;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            drawHeader("DEVICES", String(s_devCount) + " found");
            if (s_devCount == 0) {
                castMsg("No casts found. Same Wi-Fi as the TV?", TFT_ORANGE, 2);
                castMsg("[u] rescan   [q] back", 0x7BEF, 4);
            } else {
                for (int i = 0; i < rows && top + i < s_devCount; i++) {
                    int idx = top + i, y = outputY + LINE_HEIGHT * (2 + i);
                    displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, idx == sel ? 0x0010 : TFT_BLACK);
                    displayManager.setCursor(4, y);
                    displayManager.setTextColor(idx == sel ? TFT_YELLOW : TFT_WHITE);
                    displayManager.printText(idx == sel ? "> " : "  ");
                    displayManager.printText(s_dev[idx].name.c_str());
                    displayManager.setTextColor(0x7BEF);
                    displayManager.printText("  ");
                    displayManager.println(s_dev[idx].ip.toString().c_str());
                }
                footer("[trackball/ws] move  [enter] open  [1-9] pick  [u] rescan  [q] back");
            }
            redraw = false;
        }
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (e == TBALL_UP   && sel > 0)              { sel--; if (sel < top) top = sel; redraw = true; }
        if (e == TBALL_DOWN && sel < s_devCount - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        if (e == TBALL_CLICK && s_devCount > 0) return sel;
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return -1;
        if (k == 'u' || k == 'U') { castScan(); sel = 0; top = 0; redraw = true; }
        if ((k == 'w' || k == 'W') && sel > 0)              { sel--; if (sel < top) top = sel; redraw = true; }
        if ((k == 's' || k == 'S') && sel < s_devCount - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        if (k == '\r' || k == '\n') { if (s_devCount > 0) return sel; }
        if (k >= '1' && k <= '9' && (k - '1') < s_devCount) return k - '1';
        delay(15);
    }
}

// Blocking yes/no confirm. true = confirmed.
static bool confirm(const char* what, const String& who) {
    displayManager.clearScreen();
    drawHeader("CONFIRM", who);
    castMsg(what, TFT_YELLOW, 2);
    castMsg("This controls a real device.", 0x7BEF, 3);
    castMsg("[y] do it    [q] cancel", TFT_WHITE, 5);
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'y' || k == 'Y') return true;
        if (k == 'q' || k == 'Q' || k == 'n' || k == 'N') return false;
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (e == TBALL_CLICK) return true;
        delay(15);
    }
}

// Saved-content sub-picker → play the chosen entry on `ip`.
static void pickMedia(const IPAddress& ip, const String& who) {
    loadMedia();
    int sel = 0, top = 0;
    const int rows = (SCREEN_HEIGHT - outputY - LINE_HEIGHT * 3) / LINE_HEIGHT;
    bool redraw = true;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            drawHeader("CONTENT", who);
            if (s_mediaCount == 0) castMsg("No saved content. Add /apps/cast/media.csv", TFT_ORANGE, 2);
            for (int i = 0; i < rows && top + i < s_mediaCount; i++) {
                int idx = top + i, y = outputY + LINE_HEIGHT * (2 + i);
                displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, idx == sel ? 0x0010 : TFT_BLACK);
                displayManager.setCursor(4, y);
                displayManager.setTextColor(idx == sel ? TFT_YELLOW : TFT_WHITE);
                displayManager.printText(idx == sel ? "> " : "  ");
                displayManager.printText(s_media[idx].name.c_str());
                displayManager.setTextColor(0x7BEF);
                displayManager.println(s_media[idx].target.startsWith("http") ? "  [url]" : "  [yt]");
            }
            footer("[trackball/ws] move  [enter] play  [1-9] pick  [q] back");
            redraw = false;
        }
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (e == TBALL_UP   && sel > 0)                { sel--; if (sel < top) top = sel; redraw = true; }
        if (e == TBALL_DOWN && sel < s_mediaCount - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if ((k == 'w' || k == 'W') && sel > 0)                { sel--; if (sel < top) top = sel; redraw = true; }
        if ((k == 's' || k == 'S') && sel < s_mediaCount - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        int quick = (k >= '1' && k <= '9' && (k - '1') < s_mediaCount) ? (k - '1') : -1;
        if (quick >= 0) sel = quick;
        bool fire = (e == TBALL_CLICK) || (k == '\r') || (k == '\n') || (quick >= 0);
        if (fire && s_mediaCount > 0) {
            toast((String("playing ") + s_media[sel].name).c_str(), TFT_CYAN);
            String err; bool ok = playTarget(ip, s_media[sel].target, err);
            toast(ok ? "Playing." : (err.length() ? ("Failed: " + err).c_str() : "Failed"), ok ? TFT_GREEN : TFT_ORANGE);
            delay(1300); redraw = true;
        }
        delay(15);
    }
}

// Pick a media file from /apps/cast/share → (path,name). false if none / cancelled.
static bool pickShareFile(String& outPath, String& outName) {
    static String names[64];
    int n = 0;
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(CAST_SHARE_DIR);
        File dir = SD.open(CAST_SHARE_DIR);
        if (dir && dir.isDirectory()) {
            for (File e = dir.openNextFile(); e && n < 64; e = dir.openNextFile()) {
                if (!e.isDirectory()) {
                    String nm = String(e.name());
                    int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
                    if (isMediaFile(nm)) names[n++] = nm;
                }
                e.close();
            }
        }
        if (dir) dir.close();
    }
    if (n == 0) {
        displayManager.clearScreen();
        drawHeader("SHARE", "");
        castMsg("No media in /apps/cast/share", TFT_ORANGE, 2);
        castMsg("Drop .jpg/.png/.mp4 files there on the card.", 0x7BEF, 3);
        castMsg("[q] back", TFT_WHITE, 5);
        while (true) {
            char k = inputHandler.getKeyboardInput(); if (k == 'q' || k == 'Q') return false;
            TrackballEvent e = inputHandler.getTrackballEvent(); if (e == TBALL_CLICK) return false; delay(15);
        }
    }
    int sel = 0, top = 0;
    const int rows = (SCREEN_HEIGHT - outputY - LINE_HEIGHT * 3) / LINE_HEIGHT;
    bool redraw = true;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            drawHeader("SHARE", String(n) + " files");
            for (int i = 0; i < rows && top + i < n; i++) {
                int idx = top + i, y = outputY + LINE_HEIGHT * (2 + i);
                displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, idx == sel ? 0x0010 : TFT_BLACK);
                displayManager.setCursor(4, y);
                displayManager.setTextColor(idx == sel ? TFT_YELLOW : TFT_WHITE);
                displayManager.printText(idx == sel ? "> " : "  ");
                displayManager.printText(names[idx].c_str());
                displayManager.setTextColor(0x7BEF);
                displayManager.println(isImageFile(names[idx]) ? "  [img]" : "  [vid]");
            }
            footer("[trackball/ws] move  [enter] share  [1-9] pick  [q] back");
            redraw = false;
        }
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (e == TBALL_UP   && sel > 0)     { sel--; if (sel < top) top = sel; redraw = true; }
        if (e == TBALL_DOWN && sel < n - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return false;
        if ((k == 'w' || k == 'W') && sel > 0)     { sel--; if (sel < top) top = sel; redraw = true; }
        if ((k == 's' || k == 'S') && sel < n - 1) { sel++; if (sel >= top + rows) top = sel - rows + 1; redraw = true; }
        int quick = (k >= '1' && k <= '9' && (k - '1') < n) ? (k - '1') : -1;
        if (quick >= 0) sel = quick;
        if ((e == TBALL_CLICK) || (k == '\r') || (k == '\n') || quick >= 0) {
            outName = names[sel]; outPath = String(CAST_SHARE_DIR) + "/" + names[sel]; return true;
        }
        delay(15);
    }
}

// Serve a local SD file over HTTP and tell the Cast device to fetch + show it.
// Blocks (the device streams from us) until [q]. Images stay on screen after; video
// needs us live, so [q] also sends STOP.
static void shareCast(const IPAddress& ip, const String& path, const String& name, const String& who) {
    const char* ctype = mimeFor(name);
    bool image = isImageFile(name);
    String url = String("http://") + WiFi.localIP().toString() + ":" + CAST_HTTP_PORT + "/" + name;

    WiFiServer server(CAST_HTTP_PORT);
    server.begin();

    displayManager.clearScreen();
    drawHeader("SHARE", who);
    castMsg((String("File: ") + name).c_str(), TFT_WHITE, 1);
    castMsg((String("URL:  ") + url).c_str(), 0x7BEF, 2);
    castMsg("sending to device (Cast v2 / TLS) ...", TFT_CYAN, 4);

    String err;
    bool loaded = castLoadMediaEx(ip, url, ctype, image ? "NONE" : "BUFFERED", err);
    castMsg(loaded ? "device is fetching — serving file" : (String("load failed: ") + err).c_str(),
            loaded ? TFT_GREEN : TFT_ORANGE, 4);
    footer("[q] stop sharing");

    unsigned long served = 0; int reqs = 0; uint32_t lastDraw = 0;
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        WiFiClient client = server.available();
        if (client) { handleShareClient(client, path, ctype, served); reqs++; }
        if (millis() - lastDraw > 400) {
            lastDraw = millis();
            toast((String("requests ") + reqs + "   served " + (served / 1024) + " KB").c_str(), TFT_CYAN);
        }
        delay(3);
    }
    server.stop();
    if (!image) castMediaCtl(ip, "STOP");
}

// The per-device action menu.
enum { A_RICK, A_SAVED, A_SHARE, A_PLAY, A_PAUSE, A_STOP, A_VUP, A_VDN, A_MUTE, A_STATUS, A_BACK, A_N };
static const char* ACTIONS[A_N] = {
    "Rickroll (YouTube via DIAL)",
    "Saved content (SD)...",
    "Share photo/video (SD)...",
    "Play / resume",
    "Pause",
    "Stop",
    "Volume +10%",
    "Volume -10%",
    "Mute toggle",
    "Now playing / status",
    "Back",
};

static void actionMenu(int devIdx) {
    const CastDev& d = s_dev[devIdx];
    String who = d.name + "  " + d.ip.toString();
    int sel = 0; bool redraw = true; bool muted = false; int vol = 50;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            drawHeader("CONTROL", who);
            for (int i = 0; i < A_N; i++) {
                int y = outputY + LINE_HEIGHT * (2 + i);
                displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, i == sel ? 0x0010 : TFT_BLACK);
                displayManager.setCursor(4, y);
                displayManager.setTextColor(i == sel ? TFT_YELLOW : TFT_WHITE);
                displayManager.printText(i == sel ? "> " : "  ");
                displayManager.println(ACTIONS[i]);
            }
            footer("[trackball/ws] move  [enter] do  [1-9] pick  [q] back");
            redraw = false;
        }
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (e == TBALL_UP   && sel > 0)       { sel--; redraw = true; }
        if (e == TBALL_DOWN && sel < A_N - 1) { sel++; redraw = true; }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return;
        if ((k == 'w' || k == 'W') && sel > 0)       { sel--; redraw = true; }
        if ((k == 's' || k == 'S') && sel < A_N - 1) { sel++; redraw = true; }
        int quick = (k >= '1' && k <= '9' && (k - '1') < A_N) ? (k - '1') : -1;
        if (quick >= 0) sel = quick;

        bool fire = (e == TBALL_CLICK) || (k == '\r') || (k == '\n') || (quick >= 0);
        if (!fire) { delay(15); continue; }

        bool ok = false;
        switch (sel) {
            case A_RICK:
                if (!confirm("Rickroll this device?", who)) { redraw = true; break; }
                toast("launching YouTube ...", TFT_CYAN);
                ok = dialOk(dialLaunch(d.ip, "YouTube", String("v=") + RICKROLL_VID));
                toast(ok ? "Rickrolled." : "DIAL refused (try Cast v2 test video)", ok ? TFT_GREEN : TFT_ORANGE);
                delay(1200); redraw = true; break;
            case A_SAVED:
                pickMedia(d.ip, who);
                redraw = true; break;
            case A_SHARE: {
                String p, nm;
                if (pickShareFile(p, nm)) shareCast(d.ip, p, nm, who);
                redraw = true; break; }
            case A_PLAY:
                toast("play ...", TFT_CYAN); ok = castMediaCtl(d.ip, "PLAY");
                toast(ok ? "Playing." : "No active session", ok ? TFT_GREEN : TFT_ORANGE);
                delay(1000); redraw = true; break;
            case A_PAUSE:
                toast("pause ...", TFT_CYAN); ok = castMediaCtl(d.ip, "PAUSE");
                toast(ok ? "Paused." : "No active session", ok ? TFT_GREEN : TFT_ORANGE);
                delay(1000); redraw = true; break;
            case A_STOP:
                if (!confirm("Stop playback on this device?", who)) { redraw = true; break; }
                toast("stop ...", TFT_CYAN);
                ok = castMediaCtl(d.ip, "STOP") || dialOk(dialStop(d.ip, "YouTube"));
                toast(ok ? "Stopped." : "Nothing to stop", ok ? TFT_GREEN : TFT_ORANGE);
                delay(1000); redraw = true; break;
            case A_VUP:
                vol = min(100, vol + 10); ok = castVolume(d.ip, vol, false, false);
                toast(ok ? (String("volume ") + vol + "%").c_str() : "volume failed", ok ? TFT_GREEN : TFT_ORANGE);
                delay(700); redraw = true; break;
            case A_VDN:
                vol = max(0, vol - 10); ok = castVolume(d.ip, vol, false, false);
                toast(ok ? (String("volume ") + vol + "%").c_str() : "volume failed", ok ? TFT_GREEN : TFT_ORANGE);
                delay(700); redraw = true; break;
            case A_MUTE:
                muted = !muted; ok = castVolume(d.ip, vol, muted, true);
                toast(ok ? (muted ? "Muted." : "Unmuted.") : "mute failed", ok ? TFT_GREEN : TFT_ORANGE);
                delay(800); redraw = true; break;
            case A_STATUS: {
                toast("querying ...", TFT_CYAN);
                String np = castNowPlaying(d.ip);
                toast(np.length() ? (String("Now: ") + np).c_str() : "Idle / nothing playing",
                      np.length() ? TFT_GREEN : 0x7BEF);
                delay(1800); redraw = true; break; }
            case A_BACK:
                return;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════════════
// Entry
// ════════════════════════════════════════════════════════════════════════════════════
static void bail(const char* line) {
    displayManager.clearScreen();
    displayManager.setCursor(6, outputY);
    displayManager.setTextColor(TFT_ORANGE);
    displayManager.println(line);
    displayManager.setTextColor(TFT_WHITE);
    delay(1600);
    displayManager.clearScreen();
    displayManager.printCommandScreen();
}

void runCast(char* args) {
    if (WiFi.status() != WL_CONNECTED) {
        bail("cast: connect to Wi-Fi first (cw)");
        return;
    }

    // Tokenise: <verb-or-target> [target] [value]
    String a = args ? String(args) : "";
    a.trim();
    String t0 = a, t1 = "", t2 = "";
    int sp = a.indexOf(' ');
    if (sp >= 0) { t0 = a.substring(0, sp); String r = a.substring(sp + 1); r.trim();
                   int sp2 = r.indexOf(' ');
                   if (sp2 >= 0) { t1 = r.substring(0, sp2); t2 = r.substring(sp2 + 1); t2.trim(); }
                   else t1 = r; }
    t0.toLowerCase();

    // ── quiet CLI forms: ca <verb> <ip|#> [value] ─────────────────────────────────────
    auto isVerb = [](const String& v) {
        return v == "rickroll" || v == "stop" || v == "launch" || v == "vol" ||
               v == "play" || v == "pause" || v == "status" || v == "saved" || v == "share";
    };
    if (isVerb(t0)) {
        IPAddress ip;
        if (t1.length() == 0 || !resolveNetTarget(t1, ip)) { bail("cast: bad target (ip / nd# / ns#)"); return; }
        bool ok = false; String err;
        if      (t0 == "rickroll") ok = dialOk(dialLaunch(ip, "YouTube", String("v=") + (t2.length() ? t2 : RICKROLL_VID)));
        else if (t0 == "stop")     ok = castMediaCtl(ip, "STOP") || dialOk(dialStop(ip, "YouTube"));
        else if (t0 == "launch")   { String tgt = t2, sv; if (savedTarget(t2, sv)) tgt = sv;   // a saved name resolves to its target
                                     ok = playTarget(ip, tgt, err); }
        else if (t0 == "saved")    { if (t2.length()) { String sv; if (savedTarget(t2, sv)) ok = playTarget(ip, sv, err);
                                                        else { bail("cast: no saved item by that name"); return; } }
                                     else { pickMedia(ip, ip.toString()); displayManager.clearScreen(); displayManager.printCommandScreen(); return; } }
        else if (t0 == "share")    { String p, nm;
                                     if (t2.length()) { nm = t2; p = String(CAST_SHARE_DIR) + "/" + t2; }
                                     bool have = t2.length() ? (sdCardManager.isReady() && SD.exists(p.c_str())) : pickShareFile(p, nm);
                                     if (have) { shareCast(ip, p, nm, ip.toString()); displayManager.clearScreen(); displayManager.printCommandScreen(); return; }
                                     bail(t2.length() ? "cast: file not in /apps/cast/share" : "cast: nothing to share"); return; }
        else if (t0 == "vol")      ok = castVolume(ip, t2.toInt(), false, false);
        else if (t0 == "play")     ok = castMediaCtl(ip, "PLAY");
        else if (t0 == "pause")    ok = castMediaCtl(ip, "PAUSE");
        else if (t0 == "status")   { String np = castNowPlaying(ip); bail(np.length() ? (String("Now: ") + np).c_str() : "Idle"); return; }
        bail(ok ? "cast: ok" : (err.length() ? err.c_str() : "cast: failed"));
        return;
    }

    // ── `ca <ip|#>` → jump straight to that device's menu ─────────────────────────────
    if (t0.length() > 0) {
        IPAddress ip;
        if (resolveNetTarget(t0, ip)) {
            s_devCount = 1;
            s_dev[0].ip = ip; s_dev[0].name = dialDeviceName(ip);
            if (s_dev[0].name.length() == 0) s_dev[0].name = ip.toString();
            s_dev[0].model = "";
            actionMenu(0);
            displayManager.clearScreen();
            displayManager.printCommandScreen();
            return;
        }
    }

    // ── no/unknown args → interactive scan + picker ───────────────────────────────────
    castScan();
    while (true) {
        int idx = pickDevice();
        if (idx < 0) break;
        actionMenu(idx);
    }
    displayManager.clearScreen();
    displayManager.printCommandScreen();
}
