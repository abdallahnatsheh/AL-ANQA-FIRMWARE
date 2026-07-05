// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// iso_ccmp — software AES-CCMP (IEEE 802.11-2016 §12.5.3) via mbedTLS AES-CCM.
// See iso_ccmp.h. The AAD/nonce byte layout follows the canonical construction
// (as reproduced in hostapd's wlantest/ccmp.c); mbedTLS provides the AES-CCM.

#include "iso_ccmp.h"
#include <string.h>
#include <mbedtls/ccm.h>
#include <mbedtls/cipher.h>

// 802.11 non-QoS data header offsets: FC[0..1] Dur[2..3] A1[4..9] A2[10..15]
// A3[16..21] SeqCtrl[22..23].

// CCMP nonce (13 B): flags(1) || A2(6) || PN(6, big-endian MSB-first).
static void ccmpNonce(const uint8_t* a2, const uint8_t pn[6], uint8_t nonce[13]) {
    nonce[0]  = 0x00;                 // priority=0 (non-QoS), mgmt=0
    memcpy(nonce + 1, a2, 6);
    nonce[7]  = pn[5]; nonce[8]  = pn[4]; nonce[9]  = pn[3];
    nonce[10] = pn[2]; nonce[11] = pn[1]; nonce[12] = pn[0];
}

// CCMP AAD (22 B for non-QoS, no A4): masked FC || A1 || A2 || A3 || masked SC.
// FC: clear subtype low bits (b4-6), retry/pwrmgmt/moredata (b11-13); force
// Protected (b14). SC: keep fragment number (low 4 bits), zero the seq number.
static int ccmpAad(const uint8_t hdr[24], uint8_t* aad) {
    aad[0] = hdr[0] & 0x8f;
    aad[1] = (hdr[1] & 0xc7) | 0x40;
    memcpy(aad + 2,  hdr + 4,  6);    // A1
    memcpy(aad + 8,  hdr + 10, 6);    // A2
    memcpy(aad + 14, hdr + 16, 6);    // A3
    aad[20] = hdr[22] & 0x0f;         // SeqCtrl low byte: fragment number only
    aad[21] = 0x00;
    return 22;
}

// CCMP header (8 B): PN0 PN1 Rsvd KeyID(+ExtIV) PN2 PN3 PN4 PN5.
static void ccmpHeader(const uint8_t pn[6], uint8_t keyid, uint8_t* ch) {
    ch[0] = pn[0];
    ch[1] = pn[1];
    ch[2] = 0x00;                     // reserved
    ch[3] = (uint8_t)((keyid << 6) | 0x20);   // ExtIV bit (0x20) required for CCMP
    ch[4] = pn[2];
    ch[5] = pn[3];
    ch[6] = pn[4];
    ch[7] = pn[5];
}

int isoCcmpEncrypt(const uint8_t* gtk, int gtkLen,
                   const uint8_t hdr[24], uint8_t keyid, const uint8_t pn[6],
                   const uint8_t* plain, int plainLen,
                   uint8_t* out, int outCap) {
    if (gtkLen != 16) return 0;                       // AES-128 CCMP only
    if (plainLen <= 0 || outCap < plainLen + 16) return 0;

    uint8_t nonce[13], aad[22];
    ccmpNonce(hdr + 10, pn, nonce);                   // A2 = hdr[10..15]
    int aadLen = ccmpAad(hdr, aad);
    ccmpHeader(pn, keyid, out);                        // out[0..7] = CCMP header

    uint8_t* ct  = out + 8;
    uint8_t* mic = out + 8 + plainLen;

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    int rc = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, gtk, 128);
    if (rc == 0)
        rc = mbedtls_ccm_encrypt_and_tag(&ctx, (size_t)plainLen, nonce, 13,
                                         aad, (size_t)aadLen, plain, ct, mic, 8);
    mbedtls_ccm_free(&ctx);
    return rc == 0 ? plainLen + 16 : 0;
}

bool isoCcmpSelfTest() {
    uint8_t gtk[16];  for (int i = 0; i < 16; i++) gtk[i] = (uint8_t)(0x40 + i);
    // FC = 0x08,0x42 (Data, FromDS, Protected); broadcast DA, BSSID A2, SA A3.
    uint8_t hdr[24] = {
        0x08, 0x42, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,           // A1 = broadcast
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,           // A2 = BSSID
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,           // A3 = source
        0x00, 0x00 };
    uint8_t pn[6]    = { 0x01, 0, 0, 0, 0, 0 };
    uint8_t plain[16]; for (int i = 0; i < 16; i++) plain[i] = (uint8_t)(0xA0 + i);

    uint8_t enc[64];
    int n = isoCcmpEncrypt(gtk, 16, hdr, 1, pn, plain, 16, enc, sizeof(enc));
    if (n != 32) return false;                        // 8 hdr + 16 ct + 8 mic

    // Independently rebuild nonce/AAD and auth-decrypt enc[8..] (ct) + MIC.
    uint8_t nonce[13], aad[22];
    ccmpNonce(hdr + 10, pn, nonce);
    int aadLen = ccmpAad(hdr, aad);
    uint8_t dec[16];
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    int rc = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, gtk, 128);
    if (rc == 0)
        rc = mbedtls_ccm_auth_decrypt(&ctx, 16, nonce, 13, aad, (size_t)aadLen,
                                      enc + 8, dec, enc + 8 + 16, 8);
    mbedtls_ccm_free(&ctx);
    if (rc != 0) return false;                        // MIC failed / decrypt error
    return memcmp(dec, plain, 16) == 0;
}
