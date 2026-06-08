// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "espchat_core.h"
#include "display_manager.h"
#include "sdcard_manager.h"
#include "clock_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SD.h>
#include <mbedtls/sha256.h>

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;

// ── Globals ───────────────────────────────────────────────────────────────────
EcContact  g_ecContacts[EC_CONTACT_MAX];
uint8_t    g_ecContactCount  = 0;
uint8_t    g_ecPublicChannel = 1;   // default; overridden by ecLoadConfig()

EcPairEntry      g_ecPairRing[EC_PAIR_MAX];
volatile uint8_t g_ecPairWr = 0;
uint8_t          g_ecPairRd = 0;

EcRxEntry         g_ecRxRing[EC_RX_MAX];
volatile uint8_t  g_ecRxWr    = 0;
uint8_t           g_ecRxRd    = 0;

EcLogEntry g_ecLog[EC_LOG_MAX];
uint8_t    g_ecLogWr   = 0;
uint8_t    g_ecLogFill = 0;

bool    g_ecBgActive   = false;
uint8_t g_ecChannel    = 1;
bool    g_ecPrivate    = false;
uint8_t g_ecPeerMac[6] = {};
uint8_t g_ecOwnMac[6]  = {};
char    g_ecName[13]   = "ANON";
uint8_t g_ecSeq        = 0;

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ── SD log ────────────────────────────────────────────────────────────────────
static File s_sdLog;
static bool s_sdOpen = false;

// ── LMK derivation ────────────────────────────────────────────────────────────
// Public wrapper used by both core and UI (for save-contact flow)
void ecDeriveLmk(const char* pin, const uint8_t* mac1, const uint8_t* mac2,
                  uint8_t* lmk16) {
    const uint8_t* lo = (memcmp(mac1, mac2, 6) <= 0) ? mac1 : mac2;
    const uint8_t* hi = (lo == mac1) ? mac2 : mac1;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)pin, strlen(pin));
    mbedtls_sha256_update(&ctx, lo, 6);
    mbedtls_sha256_update(&ctx, hi, 6);
    uint8_t hash[32];
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    memcpy(lmk16, hash, 16);
}

// Forward declarations for static helpers defined later in this file
static bool hexToBytes(const char* hexStr, uint8_t* out, int len);
static void bytesToHex(const uint8_t* in, int len, char* out);

// ── Pairing helpers ───────────────────────────────────────────────────────────

void ecAddEncryptedPeer(const uint8_t* mac, const uint8_t* lmk) {
    esp_now_del_peer(mac);
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0; p.encrypt = true;
    memcpy(p.lmk, lmk, 16);
    esp_now_add_peer(&p);
}

bool ecRemoveContact(const uint8_t* mac) {
    if (!sdCardManager.canAccessSD()) return false;
    EcContact tmp[EC_CONTACT_MAX];
    uint8_t   tmpCount = 0;
    File f = SD.open("/espchat/contacts.csv", FILE_READ);
    if (f) {
        char line[96];
        while (f.available() && tmpCount < EC_CONTACT_MAX) {
            int n = 0;
            while (f.available() && n < (int)sizeof(line) - 1) {
                char c2 = f.read();
                if (c2 == '\n') break;
                if (c2 != '\r') line[n++] = c2;
            }
            line[n] = '\0';
            if (n == 0 || line[0] == '#') continue;
            char* mac_s  = strtok(line, ",");
            char* name_s = strtok(nullptr, ",");
            char* ch_s   = strtok(nullptr, ",");
            char* lmk_s  = strtok(nullptr, ",");
            if (!mac_s || !name_s || !ch_s || !lmk_s) continue;
            EcContact& ct = tmp[tmpCount];
            unsigned int b[6] = {};
            if (sscanf(mac_s, "%x:%x:%x:%x:%x:%x",
                       &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) continue;
            for (int i = 0; i < 6; i++) ct.mac[i] = (uint8_t)b[i];
            strncpy(ct.name, name_s, 12); ct.name[12] = '\0';
            ct.channel = (uint8_t)atoi(ch_s);
            if (strlen(lmk_s) == 32) hexToBytes(lmk_s, ct.lmk, 16);
            else memset(ct.lmk, 0, 16);
            tmpCount++;
        }
        f.close();
    }
    // Filter out the target MAC
    EcContact filtered[EC_CONTACT_MAX];
    uint8_t   filteredCount = 0;
    for (int i = 0; i < tmpCount; i++) {
        if (memcmp(tmp[i].mac, mac, 6) != 0)
            filtered[filteredCount++] = tmp[i];
    }
    if (filteredCount == tmpCount) return false;  // not found
    // Rewrite
    File wf = SD.open("/espchat/contacts.csv", FILE_WRITE);
    if (!wf) return false;
    wf.println("# ESPChat contacts — MAC,name,channel,lmk_hex");
    char lmk_hex[33];
    for (int i = 0; i < filteredCount; i++) {
        char mac_s[18];
        snprintf(mac_s, sizeof(mac_s), "%02X:%02X:%02X:%02X:%02X:%02X",
                 filtered[i].mac[0], filtered[i].mac[1], filtered[i].mac[2],
                 filtered[i].mac[3], filtered[i].mac[4], filtered[i].mac[5]);
        bytesToHex(filtered[i].lmk, 16, lmk_hex);
        wf.printf("%s,%s,%d,%s\n", mac_s, filtered[i].name, filtered[i].channel, lmk_hex);
    }
    wf.close();
    memcpy(g_ecContacts, filtered, sizeof(EcContact) * filteredCount);
    g_ecContactCount = filteredCount;
    return true;
}

bool ecSendToMac(const uint8_t* dstMac, const char* text) {
    EcMsg msg = {};
    msg.type = EC_TYPE_CHAT;
    msg.seq  = g_ecSeq++;
    strncpy(msg.name, g_ecName, sizeof(msg.name) - 1);  msg.name[sizeof(msg.name)-1] = '\0';
    strncpy(msg.text, text,     sizeof(msg.text) - 1);  msg.text[sizeof(msg.text)-1] = '\0';
    return esp_now_send(dstMac, (uint8_t*)&msg, sizeof(msg)) == ESP_OK;
}

// ── Recv callback ─────────────────────────────────────────────────────────────
static void ecOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 2) return;

    // ── Pair request beacon ───────────────────────────────────────────────────
    if (data[0] == EC_TYPE_PAIR_REQ && len >= (int)sizeof(EcPairReq)) {
        const EcPairReq* req = (const EcPairReq*)data;
        // Only handle if addressed to us
        if (memcmp(req->dst, g_ecOwnMac, 6) == 0) {
            uint8_t slot = g_ecPairWr % EC_PAIR_MAX;
            memcpy(g_ecPairRing[slot].mac, mac, 6);
            strncpy(g_ecPairRing[slot].name, req->name, 12);
            g_ecPairRing[slot].name[12] = '\0';
            g_ecPairWr++;
        }
        return;
    }

    if (data[0] != EC_TYPE_CHAT) return;
    uint8_t slot = g_ecRxWr % EC_RX_MAX;
    EcRxEntry& e = g_ecRxRing[slot];
    memcpy(e.mac, mac, 6);
    if (len >= (int)sizeof(EcMsg)) {
        const EcMsg* m = (const EcMsg*)data;
        strncpy(e.name, m->name,  12);  e.name[12]  = '\0';
        strncpy(e.text, m->text, 100);  e.text[100] = '\0';
    } else {
        strncpy(e.name, "?", sizeof(e.name));
        snprintf(e.text, sizeof(e.text), "(raw %dB)", len);
    }
    g_ecRxWr++;
}

static void ecOnSent(const uint8_t* mac, esp_now_send_status_t st) {
    (void)mac; (void)st;
}

// ── Peer helpers ──────────────────────────────────────────────────────────────
static void addBcastPeer() {
    esp_now_del_peer(BCAST);
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, BCAST, 6);
    p.channel = 0; p.encrypt = false;
    esp_now_add_peer(&p);
}

static void addUnicastPeer(const uint8_t* peerMac, const uint8_t* lmk) {
    esp_now_del_peer(peerMac);
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, peerMac, 6);
    p.channel = 0; p.encrypt = (lmk != nullptr);
    if (lmk) memcpy(p.lmk, lmk, 16);
    esp_now_add_peer(&p);
}

// ── Contact management ────────────────────────────────────────────────────────

// Decode hex string to bytes (len = byte count, hexStr must be 2*len chars)
static bool hexToBytes(const char* hexStr, uint8_t* out, int len) {
    for (int i = 0; i < len; i++) {
        char hi = hexStr[i * 2], lo = hexStr[i * 2 + 1];
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = hexVal(hi), l = hexVal(lo);
        if (h < 0 || l < 0) return false;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return true;
}

static void bytesToHex(const uint8_t* in, int len, char* out) {
    for (int i = 0; i < len; i++)
        snprintf(out + i * 2, 3, "%02X", in[i]);
    out[len * 2] = '\0';
}

bool ecLoadContacts() {
    g_ecContactCount = 0;
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir("/espchat");
    File f = SD.open("/espchat/contacts.csv", FILE_READ);
    if (!f) return false;

    char line[96];
    while (f.available() && g_ecContactCount < EC_CONTACT_MAX) {
        int n = 0;
        while (f.available() && n < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[n++] = c;
        }
        line[n] = '\0';
        if (n == 0 || line[0] == '#') continue;

        // Parse: MAC,name,channel,lmk_hex
        char* mac_s  = strtok(line, ",");
        char* name_s = strtok(nullptr, ",");
        char* ch_s   = strtok(nullptr, ",");
        char* lmk_s  = strtok(nullptr, ",");
        if (!mac_s || !name_s || !ch_s || !lmk_s) continue;

        EcContact& c = g_ecContacts[g_ecContactCount];
        unsigned int b[6] = {};
        if (sscanf(mac_s, "%x:%x:%x:%x:%x:%x",
                   &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) continue;
        for (int i = 0; i < 6; i++) c.mac[i] = (uint8_t)b[i];
        strncpy(c.name, name_s, 12); c.name[12] = '\0';
        c.channel = (uint8_t)atoi(ch_s);
        if (strlen(lmk_s) == 32)
            hexToBytes(lmk_s, c.lmk, 16);
        else
            memset(c.lmk, 0, 16);
        g_ecContactCount++;
    }
    f.close();
    return true;
}

bool ecSaveContact(const uint8_t* mac, const char* name, uint8_t ch,
                   const uint8_t* lmk) {
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir("/espchat");

    // Load existing contacts into a fresh array
    EcContact tmp[EC_CONTACT_MAX];
    uint8_t   tmpCount = 0;
    File f = SD.open("/espchat/contacts.csv", FILE_READ);
    if (f) {
        char line[96];
        while (f.available() && tmpCount < EC_CONTACT_MAX) {
            int n = 0;
            while (f.available() && n < (int)sizeof(line) - 1) {
                char c2 = f.read();
                if (c2 == '\n') break;
                if (c2 != '\r') line[n++] = c2;
            }
            line[n] = '\0';
            if (n == 0 || line[0] == '#') continue;
            char* mac_s  = strtok(line, ",");
            char* name_s = strtok(nullptr, ",");
            char* ch_s   = strtok(nullptr, ",");
            char* lmk_s  = strtok(nullptr, ",");
            if (!mac_s || !name_s || !ch_s || !lmk_s) continue;
            EcContact& ct = tmp[tmpCount];
            unsigned int b[6] = {};
            if (sscanf(mac_s, "%x:%x:%x:%x:%x:%x",
                       &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) != 6) continue;
            for (int i = 0; i < 6; i++) ct.mac[i] = (uint8_t)b[i];
            strncpy(ct.name, name_s, 12); ct.name[12] = '\0';
            ct.channel = (uint8_t)atoi(ch_s);
            if (strlen(lmk_s) == 32) hexToBytes(lmk_s, ct.lmk, 16);
            else memset(ct.lmk, 0, 16);
            tmpCount++;
        }
        f.close();
    }

    // Add or update the entry
    bool found = false;
    for (int i = 0; i < tmpCount; i++) {
        if (memcmp(tmp[i].mac, mac, 6) == 0) {
            strncpy(tmp[i].name, name, 12); tmp[i].name[12] = '\0';
            tmp[i].channel = ch;
            if (lmk) memcpy(tmp[i].lmk, lmk, 16);
            found = true; break;
        }
    }
    if (!found && tmpCount < EC_CONTACT_MAX) {
        EcContact& ct = tmp[tmpCount++];
        memcpy(ct.mac, mac, 6);
        strncpy(ct.name, name, 12); ct.name[12] = '\0';
        ct.channel = ch;
        if (lmk) memcpy(ct.lmk, lmk, 16); else memset(ct.lmk, 0, 16);
    }

    // Rewrite the file
    File wf = SD.open("/espchat/contacts.csv", FILE_WRITE);
    if (!wf) return false;
    wf.println("# ESPChat contacts — MAC,name,channel,lmk_hex");
    char lmk_hex[33];
    for (int i = 0; i < tmpCount; i++) {
        char mac_s[18];
        snprintf(mac_s, sizeof(mac_s), "%02X:%02X:%02X:%02X:%02X:%02X",
                 tmp[i].mac[0], tmp[i].mac[1], tmp[i].mac[2],
                 tmp[i].mac[3], tmp[i].mac[4], tmp[i].mac[5]);
        bytesToHex(tmp[i].lmk, 16, lmk_hex);
        wf.printf("%s,%s,%d,%s\n", mac_s, tmp[i].name, tmp[i].channel, lmk_hex);
    }
    wf.close();

    // Refresh in-memory array
    memcpy(g_ecContacts, tmp, sizeof(EcContact) * tmpCount);
    g_ecContactCount = tmpCount;
    return true;
}

// Add every loaded contact as an ESP-NOW peer (call after esp_now_init)
void ecAddContactPeers() {
    for (int i = 0; i < g_ecContactCount; i++) {
        const EcContact& c = g_ecContacts[i];
        bool hasKey = false;
        for (int b = 0; b < 16; b++) if (c.lmk[b]) { hasKey = true; break; }
        addUnicastPeer(c.mac, hasKey ? c.lmk : nullptr);
    }
}

const EcContact* ecFindContact(const uint8_t* mac) {
    for (int i = 0; i < g_ecContactCount; i++)
        if (memcmp(g_ecContacts[i].mac, mac, 6) == 0) return &g_ecContacts[i];
    return nullptr;
}

// ── Config (public_channel) ───────────────────────────────────────────────────
// File: /espchat/config.conf   format: key=value (one per line)
bool ecLoadConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/espchat/config.conf", FILE_READ);
    if (!f) return false;
    char line[48];
    while (f.available()) {
        int n = 0;
        while (f.available() && n < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[n++] = c;
        }
        line[n] = '\0';
        if (n == 0 || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;
        if (strcmp(key, "public_channel") == 0) {
            int ch = atoi(val);
            if (ch >= 1 && ch <= 13) g_ecPublicChannel = (uint8_t)ch;
        }
    }
    f.close();
    return true;
}

bool ecSaveConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir("/espchat");
    File f = SD.open("/espchat/config.conf", FILE_WRITE);
    if (!f) return false;
    f.printf("public_channel=%d\n", g_ecPublicChannel);
    f.close();
    return true;
}

// Returns the best channel for bg mode:
//   - most common channel across all loaded contacts
//   - falls back to g_ecPublicChannel if no contacts (or all channels invalid)
uint8_t ecAutoChannel() {
    if (g_ecContactCount == 0) return g_ecPublicChannel;
    uint8_t freq[14] = {};   // freq[1..13]
    for (int i = 0; i < g_ecContactCount; i++) {
        uint8_t c = g_ecContacts[i].channel;
        if (c >= 1 && c <= 13) freq[c]++;
    }
    uint8_t bestCh = g_ecPublicChannel, bestFreq = 0;
    for (int c = 1; c <= 13; c++) {
        if (freq[c] > bestFreq) { bestFreq = freq[c]; bestCh = (uint8_t)c; }
    }
    return bestCh;
}

// ── Core init / deinit ────────────────────────────────────────────────────────
bool ecCoreInit(uint8_t ch, bool isPrivate,
                const uint8_t* peerMac, const char* pin) {
    g_ecRxWr   = g_ecRxRd   = 0;
    g_ecPairWr = g_ecPairRd = 0;
    g_ecLogWr = g_ecLogFill = 0;
    g_ecChannel = ch;
    g_ecPrivate = isPrivate;
    g_ecSeq     = 0;
    if (peerMac) memcpy(g_ecPeerMac, peerMac, 6);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(ecOnRecv);
    esp_now_register_send_cb(ecOnSent);

    WiFi.macAddress(g_ecOwnMac);
    snprintf(g_ecName, sizeof(g_ecName), "%02X%02X%02X",
             g_ecOwnMac[3], g_ecOwnMac[4], g_ecOwnMac[5]);

    if (isPrivate && peerMac && pin && pin[0]) {
        uint8_t lmk[16];
        ecDeriveLmk(pin, g_ecOwnMac, peerMac, lmk);
        addUnicastPeer(peerMac, lmk);
    } else {
        // Public mode: broadcast + all known contacts as peers
        addBcastPeer();
        ecAddContactPeers();
    }
    return true;
}

bool ecCoreInitWithLmk(uint8_t ch, const uint8_t* peerMac, const uint8_t* lmk16) {
    g_ecRxWr = g_ecRxRd = 0;
    g_ecPairWr = g_ecPairRd = 0;
    g_ecLogWr = g_ecLogFill = 0;
    g_ecChannel = ch;
    g_ecPrivate = true;
    g_ecSeq     = 0;
    memcpy(g_ecPeerMac, peerMac, 6);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(ecOnRecv);
    esp_now_register_send_cb(ecOnSent);

    WiFi.macAddress(g_ecOwnMac);
    snprintf(g_ecName, sizeof(g_ecName), "%02X%02X%02X",
             g_ecOwnMac[3], g_ecOwnMac[4], g_ecOwnMac[5]);

    addUnicastPeer(peerMac, lmk16);
    return true;
}

void ecCoreDeinit() {
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
}

// ── Channel change ────────────────────────────────────────────────────────────
void ecSetChannel(uint8_t ch) {
    g_ecChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    if (!g_ecPrivate) addBcastPeer();
}

// ── RX drain ─────────────────────────────────────────────────────────────────
bool ecDrainRx() {
    bool got = false;
    while (EC_RX_PENDING() > 0) {
        const EcRxEntry& e = g_ecRxRing[g_ecRxRd % EC_RX_MAX];
        // Use stored contact name if available (overrides what sender put in name field)
        const EcContact* ct = ecFindContact(e.mac);
        const char* displayName = (ct && ct->name[0]) ? ct->name : e.name;
        ecLogAppend(false, e.mac, displayName, e.text);
        if (s_sdOpen) {
            // Foreground: write to the already-open session file
            char mac6[18];
            snprintf(mac6, sizeof(mac6), "%02X:%02X:%02X:%02X:%02X:%02X",
                     e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
            ecSdLogAppend(false, mac6, displayName, e.text);
        } else {
            // Background: route to per-channel or per-contact file
            ecSdLogDirect(false, e.mac, displayName, e.text);
        }
        g_ecRxRd++;
        got = true;
    }
    return got;
}

// ── Log append ───────────────────────────────────────────────────────────────
void ecLogAppend(bool isTx, const uint8_t* mac, const char* name, const char* text) {
    uint8_t slot = g_ecLogWr % EC_LOG_MAX;
    EcLogEntry& e = g_ecLog[slot];
    e.isTx = isTx;
    if (mac) memcpy(e.mac, mac, 6); else memset(e.mac, 0, 6);
    strncpy(e.name, name ? name : "",  12);  e.name[12]  = '\0';
    strncpy(e.text, text ? text : "", 100); e.text[100] = '\0';
    strncpy(e.ts, "--:--", 5); e.ts[5] = '\0';
    if (ClockManager::instance().isValid())
        ClockManager::instance().getShortTime(e.ts, sizeof(e.ts));
    g_ecLogWr++;
    if (g_ecLogFill < EC_LOG_MAX) g_ecLogFill++;
}

// ── Send ──────────────────────────────────────────────────────────────────────
bool ecSendMessage(const char* text) {
    EcMsg msg = {};
    msg.type = EC_TYPE_CHAT;
    msg.seq  = g_ecSeq++;
    strncpy(msg.name, g_ecName, sizeof(msg.name) - 1);  msg.name[sizeof(msg.name)-1] = '\0';
    strncpy(msg.text, text,     sizeof(msg.text) - 1);  msg.text[sizeof(msg.text)-1] = '\0';
    const uint8_t* dst = g_ecPrivate ? g_ecPeerMac : BCAST;
    bool ok = (esp_now_send(dst, (uint8_t*)&msg, sizeof(msg)) == ESP_OK);
    ecLogAppend(true, g_ecOwnMac, g_ecName, text);
    // Log TX: route to correct file (own MAC is the "sender" for TX records)
    const uint8_t* logMac = g_ecPrivate ? g_ecPeerMac : g_ecOwnMac;
    if (s_sdOpen) {
        char mac6[18];
        snprintf(mac6, sizeof(mac6), "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_ecOwnMac[0], g_ecOwnMac[1], g_ecOwnMac[2],
                 g_ecOwnMac[3], g_ecOwnMac[4], g_ecOwnMac[5]);
        ecSdLogAppend(true, mac6, g_ecName, text);
    } else {
        ecSdLogDirect(true, logMac, g_ecName, text);
    }
    return ok;
}

// ── Pairing ───────────────────────────────────────────────────────────────────
void ecSendPairReq(const uint8_t* dstMac) {
    EcPairReq req = {};
    req.type = EC_TYPE_PAIR_REQ;
    req.seq  = g_ecSeq++;
    memcpy(req.dst, dstMac, 6);
    strncpy(req.name, g_ecName, sizeof(req.name) - 1);
    // Send as broadcast so we don't need dstMac as a registered peer
    esp_now_send(BCAST, (uint8_t*)&req, sizeof(req));
}

bool ecDrainPairReq(uint8_t* mac, char* name13) {
    if (EC_PAIR_PENDING() == 0) return false;
    const EcPairEntry& e = g_ecPairRing[g_ecPairRd % EC_PAIR_MAX];
    memcpy(mac, e.mac, 6);
    strncpy(name13, e.name, 12); name13[12] = '\0';
    g_ecPairRd++;
    return true;
}

// ── SD log ────────────────────────────────────────────────────────────────────

void ecPubLogPath(uint8_t ch, char* buf, int n) {
    snprintf(buf, n, "/espchat/pub/ch%d.log", ch);
}

void ecPrvLogPath(const uint8_t* mac, char* buf, int n) {
    snprintf(buf, n, "/espchat/prv/%02X%02X%02X%02X%02X%02X.log",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void writeLogLine(File& f, bool isTx, const char* macStr,
                          const char* name, const char* text) {
    char ts[6] = "--:--";
    if (ClockManager::instance().isValid())
        ClockManager::instance().getShortTime(ts, sizeof(ts));
    char line[140];
    snprintf(line, sizeof(line), "[%s] %s | %s(%s) | %s\n",
             ts, isTx ? "TX" : "RX", name, macStr, text);
    f.print(line);
    f.flush();
}

static void openWithHeader(File& f, const char* path) {
    f = SD.open(path, FILE_APPEND);
    if (!f) return;
    // Write session header only when the file is new (size == 0)
    if (f.size() == 0) {
        char ts[22] = "no-clock";
        if (ClockManager::instance().isValid())
            ClockManager::instance().getTimestamp(ts, sizeof(ts));
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "# ESPChat log  %s\n", ts);
        f.print(hdr);
        f.flush();
    }
}

// ── Foreground session log (kept open for duration of chat) ───────────────────
void ecSdLogOpen(uint8_t ch, const uint8_t* peerMac) {
    if (s_sdOpen) return;
    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir("/espchat");          // parent must exist first
    char path[48];
    if (peerMac) {
        sdCardManager.ensureDir("/espchat/prv");
        ecPrvLogPath(peerMac, path, sizeof(path));
    } else {
        sdCardManager.ensureDir("/espchat/pub");
        ecPubLogPath(ch, path, sizeof(path));
    }
    openWithHeader(s_sdLog, path);
    if (s_sdLog) s_sdOpen = true;
}

void ecSdLogAppend(bool isTx, const char* macStr, const char* name, const char* text) {
    if (!s_sdOpen) return;
    writeLogLine(s_sdLog, isTx, macStr, name, text);
}

void ecSdLogClose() {
    if (s_sdOpen) { s_sdLog.close(); s_sdOpen = false; }
}

// ── Load history from SD into g_ecLog ─────────────────────────────────────────
// Reads last EC_LOG_MAX chat lines and pre-populates the display log.
// Call AFTER ecCoreInit (which resets g_ecLogWr/Fill to 0).
// Line format: [HH:MM] TX | name(AA:BB:CC:DD:EE:FF) | text
void ecSdLogLoad(uint8_t ch, const uint8_t* peerMac) {
    if (!sdCardManager.canAccessSD()) return;
    char path[48];
    if (peerMac) {
        ecPrvLogPath(peerMac, path, sizeof(path));
    } else {
        ecPubLogPath(ch, path, sizeof(path));
    }
    File f = SD.open(path, FILE_READ);
    if (!f) return;

    // Rolling line buffer — keep last EC_LOG_MAX non-comment lines
    static char lbuf[EC_LOG_MAX][150];
    int total = 0;

    char line[150];
    while (f.available()) {
        int n = 0;
        while (f.available() && n < 149) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[n++] = c;
        }
        line[n] = '\0';
        if (n < 10 || line[0] == '#') continue;  // skip blanks + headers
        int slot = total % EC_LOG_MAX;
        strncpy(lbuf[slot], line, 149); lbuf[slot][149] = '\0';
        total++;
    }
    f.close();

    // Parse the last min(total,EC_LOG_MAX) lines (oldest first in rolling order)
    int count    = (total < EC_LOG_MAX) ? total : EC_LOG_MAX;
    int startIdx = (total > EC_LOG_MAX) ? (total % EC_LOG_MAX) : 0;

    for (int i = 0; i < count; i++) {
        const char* ln = lbuf[(startIdx + i) % EC_LOG_MAX];

        // Extract original timestamp from "[HH:MM] " prefix
        char parsedTs[6] = "--:--";
        if (ln[0] == '[') {
            const char* tsEnd = strchr(ln + 1, ']');
            if (tsEnd && (tsEnd - ln - 1) == 5) {
                strncpy(parsedTs, ln + 1, 5); parsedTs[5] = '\0';
            }
        }

        // Skip past "[HH:MM] "
        const char* p = strchr(ln, ']');
        if (!p) continue;
        p += 2;  // skip "] "

        bool isTx = (strncmp(p, "TX", 2) == 0);

        // First " | " separates TX/RX from name(MAC)
        const char* sep1 = strstr(p, " | ");
        if (!sep1) continue;
        const char* namePart = sep1 + 3;

        // name is before '('
        const char* parenO = strchr(namePart, '(');
        if (!parenO) continue;
        char name[13] = {};
        int nlen = (int)(parenO - namePart);
        if (nlen > 12) nlen = 12;
        strncpy(name, namePart, nlen); name[nlen] = '\0';

        // MAC between '(' and ')'
        const char* parenC = strchr(parenO, ')');
        if (!parenC) continue;
        char macStr[18] = {};
        int mlen = (int)(parenC - parenO - 1);
        if (mlen > 17) mlen = 17;
        strncpy(macStr, parenO + 1, mlen);
        uint8_t mac[6] = {};
        unsigned int b[6] = {};
        if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
                   &b[0],&b[1],&b[2],&b[3],&b[4],&b[5]) == 6)
            for (int j = 0; j < 6; j++) mac[j] = (uint8_t)b[j];

        // Text after second " | "
        const char* sep2 = strstr(parenC, " | ");
        if (!sep2) continue;
        const char* text = sep2 + 3;

        ecLogAppend(isTx, mac, name[0] ? name : "?", text);
        // ecLogAppend stamps e.ts with current time — override with the
        // original timestamp parsed from the log file so history shows
        // the actual message time, not the load time.
        uint8_t histSlot = (uint8_t)(g_ecLogWr - 1) % EC_LOG_MAX;
        strncpy(g_ecLog[histSlot].ts, parsedTs, 5);
        g_ecLog[histSlot].ts[5] = '\0';
    }
}

// ── Background: route each message to its own file ────────────────────────────
// Contact MAC → /espchat/prv/AABBCCDDEEFF.log
// Unknown MAC → /espchat/pub/chN.log  (N = g_ecChannel)
void ecSdLogDirect(bool isTx, const uint8_t* mac, const char* name, const char* text) {
    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir("/espchat");          // parent must exist first
    char path[48];
    const EcContact* ct = ecFindContact(mac);
    if (ct) {
        sdCardManager.ensureDir("/espchat/prv");
        ecPrvLogPath(mac, path, sizeof(path));
    } else {
        sdCardManager.ensureDir("/espchat/pub");
        ecPubLogPath(g_ecChannel, path, sizeof(path));
    }
    File f;
    openWithHeader(f, path);
    if (!f) return;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    writeLogLine(f, isTx, macStr, name, text);
    f.close();
}
