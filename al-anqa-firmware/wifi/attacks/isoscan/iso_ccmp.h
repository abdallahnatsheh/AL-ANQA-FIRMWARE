// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// iso_ccmp — software AES-CCMP encryption for isoscan's active inject attacks.
//
// esp_wifi_80211_tx() transmits a raw 802.11 frame verbatim and does NOT
// encrypt it. To inject a GTK-encrypted broadcast/group frame the AP will
// forward to every client (AirSnitch Stage 2), we must CCMP-encrypt the MPDU
// ourselves. This module does exactly that, per IEEE 802.11-2016 §12.5.3,
// using mbedTLS's AES-CCM (the SoC's HW AES).
//
// Passive discovery (netspy) needs none of this — the WiFi HW decrypts inbound
// group frames for free. Encryption is only needed on the TX/inject side.
#pragma once

#include <stdint.h>

// CCMP-encrypt one non-QoS data MPDU.
//   gtk / gtkLen : group temporal key (AES-128 → gtkLen must be 16)
//   hdr          : the 24-byte 802.11 MAC header (FC..SeqCtrl), Protected bit set
//   keyid        : GTK key id (1 or 2) written into the CCMP header
//   pn           : 48-bit packet number, 6 bytes, pn[0] = LSB
//   plain/plainLen: MSDU payload (LLC/SNAP + upper protocol)
//   out/outCap   : receives [CCMP hdr 8B][ciphertext plainLen][MIC 8B]
// Returns bytes written (plainLen + 16), or 0 on error. The caller builds the
// on-air frame as hdr(24) ++ out(plainLen+16).
int isoCcmpEncrypt(const uint8_t* gtk, int gtkLen,
                   const uint8_t hdr[24], uint8_t keyid, const uint8_t pn[6],
                   const uint8_t* plain, int plainLen,
                   uint8_t* out, int outCap);

// Round-trip self-test (encrypt → auth-decrypt → compare, MIC-checked). Proves
// the AES-CCM path + nonce/AAD construction are internally consistent, WITHOUT
// transmitting anything. Returns true on PASS. (Final 802.11 wire-correctness is
// still validated by the hardware inject test — a real receiver accepting it.)
bool isoCcmpSelfTest();
