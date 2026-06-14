// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Shared WPA/WPA2 dictionary-crack primitives — used by ws (4-way handshake),
// pm (PMKID) and karma (captured M2). All stateless; the caller sets up one
// SHA-1 HMAC context and reuses it across the wordlist for speed.

#ifndef WPA_CRACK_H
#define WPA_CRACK_H

#include <Arduino.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

namespace wpacrack {

// Shared built-in fallback wordlist (top real-world WPA passwords, all >= 8 chars).
extern const char* const kBuiltins[];
extern const int         kBuiltinCount;

// PBKDF2-HMAC-SHA1(pass, ssid, 4096, 32) → pmk[32]. `ctx` must be pre-set-up with
// mbedtls_md_setup(ctx, sha1, 1) and is reused across calls.
void derivePMK(const char* pass, const char* ssid,
               mbedtls_md_context_t* ctx, uint8_t pmk[32]);

// WPA 4-way-handshake MIC verify. `eapolFrame`/`eapolLen` is the M2 EAPOL packet
// with its 16-byte MIC field already zeroed. Returns true if `pass` is correct.
bool verifyHandshake(const char* pass, const char* ssid,
                     const uint8_t apMac[6], const uint8_t staMac[6],
                     const uint8_t aNonce[32], const uint8_t sNonce[32],
                     const uint8_t* eapolFrame, uint16_t eapolLen, const uint8_t mic[16],
                     mbedtls_md_context_t* ctx, const mbedtls_md_info_t* sha1);

// PMKID verify: HMAC-SHA1-128(PMK, "PMK Name" || AP || STA) == pmkid[16].
bool verifyPMKID(const char* pass, const char* ssid,
                 const uint8_t apMac[6], const uint8_t staMac[6], const uint8_t pmkid[16],
                 mbedtls_md_context_t* ctx, const mbedtls_md_info_t* sha1);

} // namespace wpacrack

#endif // WPA_CRACK_H
