#include "wifimon_functions.h"
#include "input_handling.h"

extern InputHandling inputHandler;

// ── ring buffer (written from WiFi task, read from main loop) ─────────────────
volatile PktSummary WiFiMonitor::ringBuf[PKT_RING_SIZE];
volatile uint8_t    WiFiMonitor::ringHead = 0;
volatile uint8_t    WiFiMonitor::ringTail = 0;

// ── 802.11 frame type constants ───────────────────────────────────────────────
#define FRAME_TYPE_MGMT  0
#define FRAME_TYPE_CTRL  1
#define FRAME_TYPE_DATA  2
#define SUBTYPE_ASSOC_REQ  0
#define SUBTYPE_PROBE_REQ  4
#define SUBTYPE_PROBE_RESP 5
#define SUBTYPE_BEACON     8
#define SUBTYPE_DEAUTH     12

// ── promiscuous callback (runs in WiFi driver task) ───────────────────────────
void IRAM_ATTR WiFiMonitor::rxCallback(void* buf, wifi_promiscuous_pkt_type_t pktType) {
    if (pktType == WIFI_PKT_MISC) return;

    const wifi_promiscuous_pkt_t* ppkt = (wifi_promiscuous_pkt_t*)buf;
    uint16_t len = ppkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t nextHead = (ringHead + 1) % PKT_RING_SIZE;
    if (nextHead == ringTail) return;  // ring full — drop packet

    PktSummary& slot = (PktSummary&)ringBuf[ringHead];
    parseFrame(ppkt->payload, len, ppkt->rx_ctrl.channel, ppkt->rx_ctrl.rssi, slot);
    ringHead = nextHead;
}

// ── static frame parser (safe to call from callback context) ─────────────────
void WiFiMonitor::parseFrame(const uint8_t* d, uint16_t len, uint8_t ch, int8_t rssi, PktSummary& out) {
    out.type    = (d[0] >> 2) & 0x03;
    out.subtype = (d[0] >> 4) & 0x0F;
    out.rssi    = rssi;
    out.channel = ch;
    out.ssid[0] = '\0';

    // addr2 = source, addr3 = BSSID (management frames)
    memcpy(out.src,   d + 10, 6);
    memcpy(out.bssid, d + 16, 6);

    if (out.type == FRAME_TYPE_MGMT &&
        (out.subtype == SUBTYPE_BEACON || out.subtype == SUBTYPE_PROBE_RESP || out.subtype == SUBTYPE_PROBE_REQ)) {
        extractSSID(d, len, out.subtype, out.ssid);
    }
}

// ── SSID extraction from tagged parameters ────────────────────────────────────
void WiFiMonitor::extractSSID(const uint8_t* d, uint16_t len, uint8_t subtype, char* ssidOut) {
    // Beacon/probe-response have 12 bytes of fixed params after the 24-byte MAC header.
    // Probe-request has IEs starting immediately at offset 24.
    uint16_t offset = 24;
    if (subtype == SUBTYPE_BEACON || subtype == SUBTYPE_PROBE_RESP) offset += 12;

    while (offset + 2 <= len) {
        uint8_t id  = d[offset];
        uint8_t iel = d[offset + 1];
        if (offset + 2 + iel > len) break;

        if (id == 0) {  // SSID IE
            uint8_t slen = (iel > 32) ? 32 : iel;
            memcpy(ssidOut, d + offset + 2, slen);
            ssidOut[slen] = '\0';
            return;
        }
        offset += 2 + iel;
    }
}

// ── constructor ───────────────────────────────────────────────────────────────
WiFiMonitor::WiFiMonitor(DisplayManager& displayManager, SDCardManager& sdCardManager)
    : displayManager(displayManager), sdCardManager(sdCardManager),
      totalPkts(0), beaconPkts(0), probePkts(0), dataPkts(0),
      mgmtPkts(0), ctrlPkts(0), netCount(0),
      logging(false), currentChannel(1), displayPage(0) {}

void WiFiMonitor::resetStats() {
    totalPkts  = beaconPkts = probePkts = 0;
    dataPkts   = mgmtPkts  = ctrlPkts  = 0;
    netCount   = 0;
    displayPage = 0;
    ringHead   = 0;
    ringTail   = 0;
}

// ── channel control ───────────────────────────────────────────────────────────
void WiFiMonitor::setChannel(int ch) {
    if (ch < 1 || ch > 13) return;
    currentChannel = ch;
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
}

// ── track unique networks from beacon/probe frames ───────────────────────────
void WiFiMonitor::trackNet(const PktSummary& pkt) {
    for (int i = 0; i < netCount; i++) {
        if (memcmp(nets[i].bssid, pkt.bssid, 6) == 0) {
            nets[i].rssi = pkt.rssi;
            nets[i].beacons++;
            if (pkt.ssid[0] != '\0') strncpy(nets[i].ssid, pkt.ssid, 32);
            return;
        }
    }
    if (netCount < MAX_TRACKED_NETS) {
        memcpy(nets[netCount].bssid, pkt.bssid, 6);
        strncpy(nets[netCount].ssid, pkt.ssid, 32);
        nets[netCount].ssid[32]  = '\0';
        nets[netCount].rssi      = pkt.rssi;
        nets[netCount].channel   = pkt.channel;
        nets[netCount].beacons   = 1;
        netCount++;
    }
}

// ── process packet and update counters ───────────────────────────────────────
void WiFiMonitor::handlePacket(const PktSummary& pkt) {
    totalPkts++;
    switch (pkt.type) {
        case FRAME_TYPE_MGMT:
            mgmtPkts++;
            if (pkt.subtype == SUBTYPE_BEACON)                         beaconPkts++;
            if (pkt.subtype == SUBTYPE_PROBE_REQ)                      probePkts++;
            if (pkt.subtype == SUBTYPE_BEACON || pkt.subtype == SUBTYPE_PROBE_RESP) trackNet(pkt);
            break;
        case FRAME_TYPE_CTRL: ctrlPkts++; break;
        case FRAME_TYPE_DATA: dataPkts++; break;
    }
    if (logging) logToSD(pkt);
}

// ── read all pending packets from the ring buffer ─────────────────────────────
void WiFiMonitor::processRing() {
    while (ringTail != ringHead) {
        PktSummary pkt;
        memcpy(&pkt, (const void*)&ringBuf[ringTail], sizeof(PktSummary));
        ringTail = (ringTail + 1) % PKT_RING_SIZE;
        handlePacket(pkt);
    }
}

// ── SD logging ───────────────────────────────────────────────────────────────
void WiFiMonitor::logToSD(const PktSummary& pkt) {
    String line = String(pkt.channel) + "," +
                  String(pkt.type)    + "," +
                  String(pkt.subtype) + "," +
                  String(pkt.rssi)    + "," +
                  macStr(pkt.src)     + "," +
                  macStr(pkt.bssid);
    if (pkt.ssid[0] != '\0') { line += ","; line += pkt.ssid; }
    sdCardManager.appendLine(SD_LOG_PACKETS, line);
}

// ── helpers ───────────────────────────────────────────────────────────────────
String WiFiMonitor::macStr(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

// ── display refresh ───────────────────────────────────────────────────────────
void WiFiMonitor::drawDisplay() {
    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setDefaultTextSize();

    // ── row 1: mode + channel + log status
    displayManager.setTextColor(TFT_CYAN);
    displayManager.printText("MONITOR ");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("CH:");
    displayManager.setTextColor(TFT_YELLOW);
    char chBuf[12];
    if (currentChannel == 0)
        snprintf(chBuf, sizeof(chBuf), "HOP");
    else
        snprintf(chBuf, sizeof(chBuf), "%d", currentChannel);
    displayManager.printText(chBuf);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("  LOG:");
    displayManager.setTextColor(logging ? TFT_GREEN : 0x7BEF);
    displayManager.println(logging ? "ON" : "OFF");

    // ── row 2: packet counters
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("Pkts:");
    displayManager.printText((int)totalPkts);
    displayManager.printText("  Bcn:");
    displayManager.printText((int)beaconPkts);
    displayManager.printText("  Prb:");
    displayManager.println((int)probePkts);

    // ── row 3: type breakdown
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Data:");
    displayManager.printText((int)dataPkts);
    displayManager.printText("  Mgmt:");
    displayManager.printText((int)mgmtPkts);
    displayManager.printText("  Ctrl:");
    displayManager.println((int)ctrlPkts);

    // ── row 4: controls
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("h");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("=hop ");
    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("c");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("=ch ");
    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("s");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printText("=log ");
    displayManager.setTextColor(TFT_GREEN);
    displayManager.printText("q");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.println("=quit");

    // ── row 5: network list header
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(TFT_YELLOW);
    char netHdr[40];
    snprintf(netHdr, sizeof(netHdr), "-- Networks (%d) --", netCount);
    displayManager.println(netHdr);
    displayManager.setTextColor(TFT_WHITE);

    // ── network entries (use smaller text to fit more)
    displayManager.setTextSize(1);
    const int netsPerPage = 8;
    int start = displayPage * netsPerPage;
    int end   = min(start + netsPerPage, netCount);

    for (int i = start; i < end; i++) {
        displayManager.setCursor(10, displayManager.getCursorY());

        // BSSID
        displayManager.setTextColor(TFT_CYAN);
        displayManager.printText(macStr(nets[i].bssid).c_str());

        // channel
        displayManager.setTextColor(TFT_WHITE);
        char info[24];
        snprintf(info, sizeof(info), " c%-2d %4ddBm", nets[i].channel, nets[i].rssi);
        displayManager.printText(info);

        // SSID (truncated to fit)
        if (nets[i].ssid[0] != '\0') {
            displayManager.setTextColor(TFT_GREEN);
            char ssidBuf[14];
            snprintf(ssidBuf, sizeof(ssidBuf), " %.13s", nets[i].ssid);
            displayManager.println(ssidBuf);
        } else {
            displayManager.setTextColor(0x7BEF);
            displayManager.println(" <hidden>");
        }
    }

    // page indicator if list overflows
    if (netCount > netsPerPage) {
        int totalPages = (netCount + netsPerPage - 1) / netsPerPage;
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        char pageBuf[24];
        snprintf(pageBuf, sizeof(pageBuf), "a=prev l=next pg%d/%d", displayPage + 1, totalPages);
        displayManager.println(pageBuf);
    }

    displayManager.setTextColor(TFT_WHITE);
    displayManager.setDefaultTextSize();
}

// ── main monitor loop ─────────────────────────────────────────────────────────
void WiFiMonitor::start(int fixedChannel) {
    resetStats();

    // disconnect & switch to station mode to enable promiscuous
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(rxCallback);

    bool hopping = (fixedChannel == 0);
    currentChannel = hopping ? 1 : fixedChannel;
    setChannel(currentChannel);

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_GREEN);
    displayManager.println("Monitor mode active...");
    displayManager.setTextColor(TFT_WHITE);
    delay(600);

    unsigned long lastDraw  = 0;
    unsigned long lastHop   = 0;
    const uint32_t DRAW_MS  = 300;
    const uint32_t HOP_MS   = 200;

    while (true) {
        unsigned long now = millis();

        // channel hopping
        if (hopping && now - lastHop > HOP_MS) {
            currentChannel = (currentChannel % 13) + 1;
            setChannel(currentChannel);
            lastHop = now;
        }

        // drain ring buffer
        processRing();

        // refresh display every 300ms
        if (now - lastDraw > DRAW_MS) {
            drawDisplay();
            lastDraw = now;
        }

        // keyboard input
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        if (k == 'h' || k == 'H') {
            hopping = !hopping;
            if (hopping) { currentChannel = 1; lastHop = 0; }
        }
        if (k == 's' || k == 'S') {
            logging = !logging;
        }
        if (k == 'l' || k == 'L') {
            int maxPage = (netCount - 1) / 8;
            if (displayPage < maxPage) displayPage++;
        }
        if (k == 'a' || k == 'A') {
            if (displayPage > 0) displayPage--;
        }
        // lock to a specific channel: type digit 1-9 or use '0' for 10,11,12,13
        if (k >= '1' && k <= '9') {
            hopping = false;
            setChannel(k - '0');
        }
    }

    // clean up
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_YELLOW);
    displayManager.printText("Monitor off. Pkts: ");
    displayManager.println((int)totalPkts);
    displayManager.printText("Nets: ");
    displayManager.println(netCount);
    if (logging) {
        displayManager.setTextColor(TFT_GREEN);
        displayManager.println("Log saved -> " SD_LOG_PACKETS);
    }
    displayManager.setTextColor(TFT_WHITE);
    delay(2000);
    displayManager.tdeck_begin();
}
