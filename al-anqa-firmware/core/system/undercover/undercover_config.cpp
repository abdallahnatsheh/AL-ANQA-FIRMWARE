// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "undercover_config.h"
#include "sdcard_manager.h"
#include <SD.h>
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include <Arduino.h>
#include <string.h>

extern SDCardManager sdCardManager;

// ── In-RAM state ─────────────────────────────────────────────────────────────

static char    s_hashHex[65]  = {};   // 64-hex SHA-256 + NUL
static char    s_saltHex[17]  = {};   // 16-hex 8-byte salt + NUL
static int     s_len          = 0;    // plaintext passphrase byte length
static bool    s_bootCover    = false;
static uint8_t s_panicKey     = '@';  // instant-cover trigger byte; 0 = disabled
static bool    s_loaded       = false;

// ── Crypto ────────────────────────────────────────────────────────────────────

static void genSalt(char* out17) {
    uint32_t r[2] = { esp_random(), esp_random() };
    for (int i = 0; i < 8; i++)
        snprintf(out17 + i * 2, 3, "%02x", ((uint8_t*)r)[i]);
    out17[16] = '\0';
}

static void hashPhrase(const char* phrase, const char* saltHex, char* out65) {
    // SHA-256(saltHex || phrase)  — identical construction to lockscreen PIN
    char input[96];
    snprintf(input, sizeof(input), "%s%s", saltHex, phrase);
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);   // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; i++)
        snprintf(out65 + i * 2, 3, "%02x", hash[i]);
    out65[64] = '\0';
}

// ── SD I/O ────────────────────────────────────────────────────────────────────

bool ucLoadConfig() {
    s_hashHex[0] = '\0'; s_saltHex[0] = '\0'; s_len = 0; s_bootCover = false;
    s_panicKey = '@';   // default if the config has no panic_key line
    s_loaded = true;
    if (!sdCardManager.canAccessSD()) return false;
    File f = SD.open("/config/undercover.conf", FILE_READ);
    if (!f) return false;
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (!line.length() || line[0] == '#') continue;
        int eq = line.indexOf('='); if (eq < 0) continue;
        String k = line.substring(0, eq);
        String v = line.substring(eq + 1);
        if      (k == "hash")       { strncpy(s_hashHex, v.c_str(), 64); s_hashHex[64] = '\0'; }
        else if (k == "salt")       { strncpy(s_saltHex, v.c_str(), 16); s_saltHex[16] = '\0'; }
        else if (k == "len")        { s_len = v.toInt(); }
        else if (k == "boot_cover") { s_bootCover = (v.toInt() != 0); }
        else if (k == "panic_key")  { s_panicKey = (uint8_t)v.toInt(); }
    }
    f.close();
    return strlen(s_hashHex) == 64 && s_len > 0;
}

static bool saveConfig() {
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir("/config");
    File f = SD.open("/config/undercover.conf", FILE_WRITE);
    if (!f) return false;
    f.printf("hash=%s\nsalt=%s\nlen=%d\nboot_cover=%d\npanic_key=%d\n",
             s_hashHex, s_saltHex, s_len, s_bootCover ? 1 : 0, (int)s_panicKey);
    f.close();
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ucHasPassphrase() {
    if (!s_loaded) ucLoadConfig();
    return strlen(s_hashHex) == 64 && s_len > 0;
}

int ucPhraseLen() {
    if (!s_loaded) ucLoadConfig();
    return s_len;
}

bool ucSetPassphrase(const char* phrase) {
    if (!s_loaded) ucLoadConfig();
    genSalt(s_saltHex);
    hashPhrase(phrase, s_saltHex, s_hashHex);
    s_len = (int)strlen(phrase);
    return saveConfig();
}

bool ucClearPassphrase() {
    s_hashHex[0] = '\0'; s_saltHex[0] = '\0'; s_len = 0;
    s_loaded = true;
    return saveConfig();
}

bool ucCheckPhrase(const char* candidate) {
    if (!ucHasPassphrase()) return false;
    if ((int)strlen(candidate) != s_len) return false;
    char computed[65];
    hashPhrase(candidate, s_saltHex, computed);
    return strcmp(computed, s_hashHex) == 0;
}

bool ucBootCoverEnabled() {
    if (!s_loaded) ucLoadConfig();
    return s_bootCover;
}

bool ucSetBootCover(bool on) {
    if (!s_loaded) ucLoadConfig();
    s_bootCover = on;
    return saveConfig();
}

uint8_t ucPanicKey() {
    if (!s_loaded) ucLoadConfig();
    return s_panicKey;
}

bool ucSetPanicKey(uint8_t key) {
    if (!s_loaded) ucLoadConfig();
    s_panicKey = key;
    return saveConfig();
}
