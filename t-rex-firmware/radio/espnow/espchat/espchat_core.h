// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#pragma once
#include <Arduino.h>

// ── Wire format ───────────────────────────────────────────────────────────────
// type byte shared with espsniff/esptest  (0x01 = chat)
struct EcMsg {
    uint8_t type;      // EC_TYPE_CHAT = 0x01
    uint8_t seq;
    char    name[12];  // sender short name — espsniff detail view shows this
    char    text[100]; // message payload (max 99 chars + null)
};
// sizeof(EcMsg) = 114  (well under ESP-NOW 250-byte limit)

#define EC_TYPE_CHAT      0x01
#define EC_TYPE_PAIR_REQ  0x10   // pairing beacon (broadcast, dst MAC in payload)

// ── Ring + log sizes ──────────────────────────────────────────────────────────
#define EC_RX_MAX       8   // ISR-safe two-pointer ring (WiFi task → main loop)
#define EC_LOG_MAX     16   // display + scroll history (main loop only)
#define EC_VIS          7   // visible message rows on screen
#define EC_CONTACT_MAX 16   // max contacts in /apps/espchat/contacts.csv

// ── Contact ───────────────────────────────────────────────────────────────────
// Stored in /apps/espchat/contacts.csv:  MAC,name,channel,lmk_hex
// lmk_hex = 32 hex chars (16-byte AES-128 key derived at pairing time)
struct EcContact {
    uint8_t mac[6];
    char    name[13];
    uint8_t channel;
    uint8_t lmk[16];   // zero → not encrypted (public/unknown contact)
};
extern EcContact g_ecContacts[EC_CONTACT_MAX];
extern uint8_t   g_ecContactCount;

// ── Structs ───────────────────────────────────────────────────────────────────
// Pair request beacon (sent broadcast, dst_mac in payload so others ignore it)
struct EcPairReq {
    uint8_t type;     // EC_TYPE_PAIR_REQ = 0x10
    uint8_t seq;
    uint8_t dst[6];   // target MAC
    char    name[12]; // initiator display name
};
// sizeof = 20 bytes

// Pair request ring (WiFi task → main loop, same two-pointer pattern)
#define EC_PAIR_MAX  2
struct EcPairEntry {
    uint8_t mac[6];   // requester MAC
    char    name[13]; // requester name
};
extern EcPairEntry       g_ecPairRing[EC_PAIR_MAX];
extern volatile uint8_t  g_ecPairWr;
extern uint8_t           g_ecPairRd;
#define EC_PAIR_PENDING() ((uint8_t)(g_ecPairWr - g_ecPairRd))

struct EcRxEntry {
    uint8_t mac[6];
    char    name[13];   // null-terminated sender name (max 12)
    char    text[101];  // message text (max 100 chars + null)
};

struct EcLogEntry {
    bool    isTx;
    uint8_t mac[6];
    char    name[13];
    char    text[101];
    char    ts[6];     // "HH:MM\0" from ClockManager, "--:--\0" if no clock
};

// ── RX ring — two-pointer lock-free ──────────────────────────────────────────
extern EcRxEntry         g_ecRxRing[EC_RX_MAX];
extern volatile uint8_t  g_ecRxWr;   // written by WiFi task
extern uint8_t           g_ecRxRd;   // read by main loop only
#define EC_RX_PENDING()  ((uint8_t)(g_ecRxWr - g_ecRxRd))

// ── Display log (main loop only) ──────────────────────────────────────────────
extern EcLogEntry g_ecLog[EC_LOG_MAX];
extern uint8_t    g_ecLogWr;
extern uint8_t    g_ecLogFill;

// ── Session state ─────────────────────────────────────────────────────────────
extern bool    g_ecBgActive;
extern uint8_t g_ecChannel;
extern bool    g_ecPrivate;
extern uint8_t g_ecPeerMac[6];
extern uint8_t g_ecOwnMac[6];
extern char    g_ecName[13];
extern uint8_t g_ecSeq;

// ── Core API ──────────────────────────────────────────────────────────────────
bool ecCoreInit(uint8_t ch, bool isPrivate,
                const uint8_t* peerMac = nullptr,
                const char*    pin     = nullptr);
// Variant for when LMK is already derived (pairing flow)
bool ecCoreInitWithLmk(uint8_t ch, const uint8_t* peerMac, const uint8_t* lmk16);
void ecCoreDeinit();

bool ecDrainRx();
void ecLogAppend(bool isTx, const uint8_t* mac, const char* name, const char* text);
bool ecSendMessage(const char* text);
void ecSetChannel(uint8_t ch);

// Pairing
void    ecSendPairReq(const uint8_t* dstMac);  // broadcast pair beacon to dstMac
bool    ecDrainPairReq(uint8_t* mac, char* name); // drain one entry; returns true if got one

// Contact management
bool ecLoadContacts();    // load /apps/espchat/contacts.csv → g_ecContacts[]
bool ecSaveContact(const uint8_t* mac, const char* name, uint8_t ch,
                   const uint8_t* lmk);
void ecAddContactPeers();
const EcContact* ecFindContact(const uint8_t* mac);

// Channel helpers
// g_ecPublicChannel: default channel for public/no-contacts bg mode (from config)
extern uint8_t g_ecPublicChannel;
bool    ecLoadConfig();              // load /apps/espchat/config.conf
bool    ecSaveConfig();              // write /apps/espchat/config.conf
uint8_t ecAutoChannel();             // most common contact channel, else g_ecPublicChannel

// Crypto
void ecDeriveLmk(const char* pin, const uint8_t* mac1, const uint8_t* mac2,
                 uint8_t* lmk16);

// Pairing helpers — used by espchat_ui during pairing flow
void ecAddEncryptedPeer(const uint8_t* mac, const uint8_t* lmk);
bool ecSendToMac(const uint8_t* dstMac, const char* text);
bool ecRemoveContact(const uint8_t* mac);  // remove a contact by MAC; returns true if found+removed

// Load last EC_LOG_MAX lines from the SD log into g_ecLog — call after ecCoreInit
// (same path logic as ecSdLogOpen: peerMac=null → pub/chN.log, else prv/MAC.log)
void ecSdLogLoad(uint8_t ch, const uint8_t* peerMac = nullptr);

// SD log helpers
// Foreground: open a session file and keep it open until ecSdLogClose()
//   public  → /apps/espchat/pub/ch<N>.log
//   private → /apps/espchat/prv/<AABBCCDDEEFF>.log
void ecSdLogOpen(uint8_t ch, const uint8_t* peerMac = nullptr); // peerMac=null → public
void ecSdLogAppend(bool isTx, const char* macStr, const char* name, const char* text);
void ecSdLogClose();

// Background: write a single line directly (open → append → close per call)
// Routes automatically: contact MAC → prv file, unknown → pub/ch<N>.log
void ecSdLogDirect(bool isTx, const uint8_t* mac, const char* name, const char* text);

// Path builders (public, for use by UI if needed)
void ecPubLogPath(uint8_t ch,          char* buf, int n); // /apps/espchat/pub/chN.log
void ecPrvLogPath(const uint8_t* mac,  char* buf, int n); // /apps/espchat/prv/AABBCC.log
