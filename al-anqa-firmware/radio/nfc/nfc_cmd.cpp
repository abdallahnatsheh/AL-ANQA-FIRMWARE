/**
 * @file   nfc_cmd.cpp
 * @brief  `nfc` / `nm` command — Phase 0 Slice 2 (RFAL init).
 *
 * Adopts LilyGoLib's proven bring-up for the T-Lora Pager's ST25R3916
 * (see LilyGo_LoRa_Pager.cpp:38 / :874). Raw-SPI probing from S1 was
 * dropped after HW testing: the ST25R3916 IS present but the ESP32 SPI
 * peripheral inserts framing quirks between the command byte and the
 * response byte that a hand-rolled reader has to work around. The RFAL
 * fork handles all of that under the hood via ST's own driver — same
 * pattern that already works on LilyGo's own firmware.
 *
 * The idiom is literally 3 lines: one RfalRfST25R3916Class over the
 * shared &SPI, wrapped in an RfalNfcClass, initialised once via
 * rfalNfcInitialize(). Later slices add scan/dump/emu on top.
 */
#include "nfc_cmd.h"
#include "display_manager.h"
#include "input_handling.h"
#include "board.h"

#include <cstring>
#include <cstdio>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;

#if defined(BOARD_HAS_NFC)

#include <SPI.h>
#include <rfal_rfst25r3916.h>
#include <rfal_nfc.h>
#include <st_errno.h>

// LilyGoLib's exact instantiation — reuse the shared global SPI bus (already
// begun by sdCardManager.begin()), fed the T-Pager's NFC CS + IRQ pins.
static RfalRfST25R3916Class nfc_hw(&SPI, BOARD_NFC_CS, BOARD_NFC_IRQ);
static RfalNfcClass         NFCReader(&nfc_hw);
static bool                 s_inited = false;

// Bring RFAL up once and cache the result. Deassert the peer CS lines on the
// shared SPI bus first so nothing else can pull data during RFAL's first
// register writes — belt-and-braces; display/SD/LoRa also set their own CS
// OUTPUT+HIGH at their own init.
static bool ensureInit() {
    if (s_inited) return true;
    pinMode(BOARD_TFT_CS,    OUTPUT); digitalWrite(BOARD_TFT_CS,    HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT); digitalWrite(BOARD_SDCARD_CS, HIGH);
    pinMode(RADIO_CS_PIN,    OUTPUT); digitalWrite(RADIO_CS_PIN,    HIGH);
    s_inited = (NFCReader.rfalNfcInitialize() == ST_ERR_NONE);
    return s_inited;
}

static void doInfo() {
    displayManager.println("NFC  ST25R3916 (HF 13.56 MHz)");
    displayManager.println("SPI CS 39  IRQ 5  power XL9555");
    if (ensureInit()) {
        displayManager.println("RFAL init OK — chip online");
    } else {
        displayManager.println("RFAL init FAILED");
        displayManager.println("check EXPANDS_NFC_EN + SPI bus");
    }
}

// Latched by the RFAL notify callback the moment discovery activates a tag.
// Volatile because the RFAL worker may set it from within a callback context.
static volatile bool s_activated = false;
static void nfcNotifyCb(rfalNfcState st) {
    if (st == RFAL_NFC_STATE_ACTIVATED) s_activated = true;
}

// Best-effort classifier — SAK is the definitive ISO14443A type byte; falls
// back to RFAL's NFC-A subtype if SAK is ambiguous. Not exhaustive; extended
// per-vendor detection lives in the Phase-1 `nfc vuln` module (plan §4.2).
static const char* classifyNfcA(uint8_t sak) {
    if (sak & 0x08) return "MFC 1K/4K";
    if (sak == 0x00) return "NTAG / Ultralight";
    if (sak & 0x20) return "T4T (DESFire?)";
    return "NFC-A unknown";
}

static void doScan() {
    if (!ensureInit()) {
        displayManager.println("nfc: RFAL init failed");
        return;
    }
    displayManager.println("nfc scan — present tag");
    displayManager.println("[q] to cancel");

    // Start fresh — cancel any previous discovery before configuring a new
    // one, then pump the worker to let the state machine actually return to
    // IDLE. rfalNfcDeactivate() only *requests* deactivation; the transition
    // takes a few worker ticks to complete, and rfalNfcDiscover() rejects the
    // call if the state isn't IDLE — the exact "discover start failed" we hit.
    NFCReader.rfalNfcDeactivate(true);
    for (int i = 0; i < 20; i++) {
        NFCReader.rfalNfcWorker();
        delay(2);
    }
    s_activated = false;

    // Discovery parameters. LilyGoLib's factory + reader examples leave the
    // struct uninitialised and set only these six fields, so we match them
    // verbatim (dropped memset — some optional fields like nfcaBailOut have
    // valid non-zero defaults left by the caller frame, and zeroing them can
    // wedge the discover config on some RFAL fork versions).
    //
    // All four HF bands enabled: A (MFC/NTAG/UL/most credit), B (Ravkav /
    // Calypso transit, some passports, some credit), F (FeliCa/Suica), V
    // (ISO15693 iCLASS/access). ST25TB (proprietary ST) skipped for now.
    rfalNfcDiscoverParam params;
    params.devLimit       = 1;
    params.techs2Find     = RFAL_NFC_POLL_TECH_A
                          | RFAL_NFC_POLL_TECH_B
                          | RFAL_NFC_POLL_TECH_F
                          | RFAL_NFC_POLL_TECH_V;
    params.GBLen          = RFAL_NFCDEP_GB_MAX_LEN;
    params.notifyCb       = nfcNotifyCb;
    params.totalDuration  = 1000U;
    params.wakeupEnabled  = false;

    const ReturnCode rc = NFCReader.rfalNfcDiscover(&params);
    if (rc != ST_ERR_NONE) {
        char buf[48];
        snprintf(buf, sizeof(buf), "nfc: discover start failed rc=%d", (int)rc);
        displayManager.println(buf);
        return;
    }

    // Pump the RFAL worker indefinitely until a tag activates or the user
    // hits q. The internal totalDuration only paces one internal poll cycle;
    // rfalNfcWorker() restarts polling automatically when the cycle expires,
    // so this loop keeps polling forever with no external timer.
    while (!s_activated) {
        NFCReader.rfalNfcWorker();
        const char c = inputHandler.getKeyboardInput();
        if (c == 'q' || c == 'Q') {
            NFCReader.rfalNfcDeactivate(true);
            displayManager.println("cancelled");
            return;
        }
        delay(10);
    }

    // Pull the activated device — nfcid[] / nfcidLen are populated by RFAL
    // for every tech, so this print path is tech-agnostic (though only NFC-A
    // is enabled right now).
    rfalNfcDevice* dev = nullptr;
    NFCReader.rfalNfcGetActiveDevice(&dev);
    if (!dev) {
        displayManager.println("activated but no device?");
        NFCReader.rfalNfcDeactivate(true);
        return;
    }

    char buf[80];
    char* p = buf;
    p += snprintf(p, sizeof(buf), "UID:");
    for (uint32_t i = 0; i < dev->nfcidLen && (p - buf) < (int)sizeof(buf) - 4; i++) {
        p += snprintf(p, sizeof(buf) - (p - buf), " %02X", dev->nfcid[i]);
    }
    displayManager.println(buf);

    // Per-band details. dev->nfcid / nfcidLen are populated for every band;
    // the per-band union carries the tech-specific bytes (SAK/ATQA for A,
    // sensbRes for B, sensfRes for F, InvRes for V).
    switch (dev->type) {
        case RFAL_NFC_LISTEN_TYPE_NFCA: {
            const uint8_t sak    = dev->dev.nfca.selRes.sak;
            const uint8_t atqaHi = dev->dev.nfca.sensRes.platformInfo;
            const uint8_t atqaLo = dev->dev.nfca.sensRes.anticollisionInfo;
            snprintf(buf, sizeof(buf), "NFC-A ATQA:%02X%02X SAK:%02X",
                     atqaHi, atqaLo, sak);
            displayManager.println(buf);
            snprintf(buf, sizeof(buf), "type: %s", classifyNfcA(sak));
            displayManager.println(buf);
            break;
        }
        case RFAL_NFC_LISTEN_TYPE_NFCB:
            // ISO14443B — Ravkav, Calypso, ePassport, some credit cards.
            // UID above is the 4-byte PUPI extracted by RFAL from ATQB.
            displayManager.println("NFC-B (ISO14443B)");
            displayManager.println("type: transit / passport / credit");
            break;
        case RFAL_NFC_LISTEN_TYPE_NFCF:
            // FeliCa — Suica, Pasmo, Octopus, Edy. UID = 8-byte IDm.
            displayManager.println("NFC-F (FeliCa)");
            displayManager.println("type: JP transit / e-money");
            break;
        case RFAL_NFC_LISTEN_TYPE_NFCV:
            // ISO15693 — HID iCLASS, ICODE, some access control tags.
            displayManager.println("NFC-V (ISO15693)");
            displayManager.println("type: access / vicinity tag");
            break;
        default:
            displayManager.println("unknown NFC tech");
            break;
    }

    // Release the tag back to sleep — polite to the card + lets a second
    // `nfc scan` re-detect it without a physical remove/re-place cycle.
    NFCReader.rfalNfcDeactivate(true);
}

static void doHelp() {
    displayManager.println("nfc | nm  NFC HF (ST25R3916)");
    displayManager.println("  info         RFAL init + status");
    displayManager.println("  scan|read    poll for one NFC-A tag");
    displayManager.println("  help         this text");
    displayManager.println("(dump/emu/magic land in later slices)");
}

void runNfc(char* args) {
    if (!args || !*args || strcmp(args, "help") == 0) { doHelp(); return; }
    if (strcmp(args, "info") == 0) { doInfo(); return; }
    if (strcmp(args, "scan") == 0 || strcmp(args, "read") == 0) { doScan(); return; }
    displayManager.println("nfc: unknown subcommand");
    displayManager.println("try 'nfc help'");
}

#else  // ── boards without ST25R3916 ─────────────────────────────────────────

void runNfc(char* /*args*/) {
    displayManager.println("nfc: no NFC silicon on this board");
    displayManager.println("(T-Pager only).");
}

#endif
