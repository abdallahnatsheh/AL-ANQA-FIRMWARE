#include <vector>
#include <algorithm>
#include "network_scanner.h"
#include "input_handling.h"
#include "lockscreen_manager.h"
#include "task_manager.h"
#include "utils.h"
#include <ESP32Ping.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip4.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

extern InputHandling inputHandler;
extern Utils utils;

// ARP result cache — populated by networkDiscovery(), indexed by portscan/topscan
struct ArpEntry {
    IPAddress ip;
    uint8_t   mac[6];
};
static std::vector<ArpEntry> g_arpResults;

// Top ports — nmap-style common services
static const uint16_t TOP_PORTS[] = {
    21, 22, 23, 25, 53, 80, 110, 111, 135, 139,
    143, 161, 389, 443, 445, 587, 993, 995, 1433, 1521,
    1723, 3306, 3389, 5432, 5900, 6379, 8080, 8443, 8888, 9200,
    27017
};
static const int TOP_PORTS_COUNT = (int)(sizeof(TOP_PORTS) / sizeof(TOP_PORTS[0]));

// Resolve first arg — bare number = ARP index, otherwise IP string
static bool resolveTarget(const String& tok, IPAddress& out) {
    bool allDigits = tok.length() > 0;
    for (char c : tok) { if (!isdigit((unsigned char)c)) { allDigits = false; break; } }
    if (allDigits) {
        int idx = tok.toInt();
        if (idx < 0 || idx >= (int)g_arpResults.size()) return false;
        out = g_arpResults[idx].ip;
        return true;
    }
    return out.fromString(tok);
}

static const char* portService(int port) {
    switch (port) {
        case 20:    return "FTP-data";
        case 21:    return "FTP";
        case 22:    return "SSH";
        case 23:    return "Telnet";
        case 25:    return "SMTP";
        case 53:    return "DNS";
        case 69:    return "TFTP";
        case 80:    return "HTTP";
        case 88:    return "Kerberos";
        case 110:   return "POP3";
        case 111:   return "RPC";
        case 119:   return "NNTP";
        case 123:   return "NTP";
        case 135:   return "MSRPC";
        case 137:   return "NetBIOS";
        case 138:   return "NetBIOS";
        case 139:   return "NetBIOS";
        case 143:   return "IMAP";
        case 161:   return "SNMP";
        case 162:   return "SNMP-trap";
        case 179:   return "BGP";
        case 389:   return "LDAP";
        case 443:   return "HTTPS";
        case 445:   return "SMB";
        case 500:   return "IKE";
        case 514:   return "Syslog";
        case 587:   return "SMTP-sub";
        case 631:   return "IPP";
        case 636:   return "LDAPS";
        case 873:   return "rsync";
        case 993:   return "IMAPS";
        case 995:   return "POP3S";
        case 1080:  return "SOCKS";
        case 1194:  return "OpenVPN";
        case 1433:  return "MSSQL";
        case 1521:  return "Oracle";
        case 1723:  return "PPTP";
        case 2049:  return "NFS";
        case 2082:  return "cPanel";
        case 2083:  return "cPanel-SSL";
        case 3128:  return "Squid";
        case 3306:  return "MySQL";
        case 3389:  return "RDP";
        case 4444:  return "Msfp";
        case 5000:  return "UPnP";
        case 5432:  return "PostgreSQL";
        case 5900:  return "VNC";
        case 6379:  return "Redis";
        case 6667:  return "IRC";
        case 8080:  return "HTTP-alt";
        case 8443:  return "HTTPS-alt";
        case 8888:  return "HTTP-alt";
        case 9000:  return "PHP-FPM";
        case 9200:  return "Elastic";
        case 10000: return "Webmin";
        case 11211: return "Memcached";
        case 27017: return "MongoDB";
        default:    return "";
    }
}

static String grabBanner(const IPAddress& ip, int port) {
    WiFiClient c;
    if (!c.connect(ip, port, 300)) return "";

    // Active probes for protocols that don't send first
    if (port == 80 || port == 8080)
        c.print("HEAD / HTTP/1.0\r\nHost: x\r\n\r\n");
    else if (port == 9200)
        c.print("GET / HTTP/1.0\r\nHost: x\r\n\r\n");
    else if (port == 6379)
        c.print("PING\r\n");

    char buf[128];
    int len = 0;
    unsigned long t0 = millis();
    while (millis() - t0 < 1000 && len < 127) {
        if (c.available()) buf[len++] = (char)c.read();
        else delay(5);
    }
    c.stop();
    buf[len] = '\0';
    if (len == 0) return "";

    // TLS: first byte is 0x16 (handshake record)
    if ((port == 443 || port == 8443) && (uint8_t)buf[0] == 0x16)
        return "TLS";

    // MySQL: 4-byte packet header + 1-byte protocol, then null-terminated version string
    if (port == 3306 && len > 5) {
        char ver[32] = {0}; int vi = 0;
        for (int i = 5; i < len && vi < 31 && buf[i] != '\0'; i++)
            if ((uint8_t)buf[i] >= 0x20 && (uint8_t)buf[i] < 0x7F) ver[vi++] = buf[i];
        return vi > 0 ? String(ver) : String("");
    }

    // Truncate at first non-printable / newline / null
    for (int i = 0; i < len; i++) {
        if ((uint8_t)buf[i] < 0x20 || (uint8_t)buf[i] == 0x7F) { buf[i] = '\0'; break; }
    }
    return String(buf);
}

static void grabAllBanners(DisplayManager& dm, const IPAddress& ip,
                            const std::vector<int>& ports, std::vector<String>& banners) {
    const char spinner[] = "|/-\\";
    int   total   = (int)ports.size();
    const int maxShow = 7;

    for (int i = 0; i < total; i++) {
        if (!banners[i].isEmpty()) continue;

        // ── header + completed results ────────────────────────────────────────
        dm.clearScreen();
        dm.setCursor(10, outputY);
        dm.setTextColor(0x7BEF);     dm.printText("[");
        dm.setTextColor(TFT_CYAN);   dm.printText("BANNER");
        dm.setTextColor(0x7BEF);     dm.printText("::");
        dm.setTextColor(TFT_YELLOW); dm.printText("GRAB");
        dm.setTextColor(0x7BEF);     dm.println("]");
        dm.printSeparator();

        int showFrom = max(0, i - maxShow);
        for (int j = showFrom; j < i; j++) {
            dm.setCursor(10, dm.getCursorY());
            if (banners[j].length() > 0) {
                dm.setTextColor(TFT_GREEN);
                char ln[42]; snprintf(ln, sizeof(ln), "%d: %.32s", ports[j], banners[j].c_str());
                dm.println(ln);
            } else {
                dm.setTextColor(0x7BEF);
                char ln[20]; snprintf(ln, sizeof(ln), "%d: -", ports[j]);
                dm.println(ln);
            }
        }

        int32_t spinY    = dm.getCursorY();
        uint32_t spinFrm = (uint32_t)(i * 3);   // stagger initial frame per port

        // helper to redraw the spinner line
        auto drawSpin = [&](char ch) {
            dm.fillRect(10, spinY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
            dm.setCursor(10, spinY);
            dm.setTextColor(TFT_YELLOW);
            char cur[40];
            snprintf(cur, sizeof(cur), "%d: %c  (%d/%d)", ports[i], ch, i + 1, total);
            dm.println(cur);
            dm.setTextColor(TFT_WHITE);
        };

        drawSpin(spinner[spinFrm % 4]);

        // ── connect ───────────────────────────────────────────────────────────
        WiFiClient c;
        bool connected = c.connect(ip, ports[i], 300);

        if (!connected) {
            banners[i] = "";
        } else {
            // active probes for protocols that don't send first
            if (ports[i] == 80 || ports[i] == 8080)
                c.print("HEAD / HTTP/1.0\r\nHost: x\r\n\r\n");
            else if (ports[i] == 9200)
                c.print("GET / HTTP/1.0\r\nHost: x\r\n\r\n");
            else if (ports[i] == 6379)
                c.print("PING\r\n");

            // ── animated read loop ────────────────────────────────────────────
            char buf[384]; int len = 0;
            uint32_t t0 = millis(), lastSpin = 0;

            while (millis() - t0 < 1000 && len < 383) {
                if (c.available()) {
                    buf[len++] = (char)c.read();
                } else {
                    uint32_t now = millis();
                    if (now - lastSpin >= 120) {
                        lastSpin = now;
                        drawSpin(spinner[++spinFrm % 4]);
                    }
                    delay(5);
                }
            }
            c.stop();
            buf[len] = '\0';

            // ── parse ─────────────────────────────────────────────────────────
            bool isHTTP = (ports[i] == 80 || ports[i] == 8080 ||
                           ports[i] == 8888 || ports[i] == 9200) && len > 4 &&
                          memcmp(buf, "HTTP", 4) == 0;

            if (len == 0) {
                banners[i] = "";
            } else if ((ports[i] == 443 || ports[i] == 8443) && (uint8_t)buf[0] == 0x16) {
                banners[i] = "TLS";
            } else if (ports[i] == 3306 && len > 5) {
                char ver[32] = {0}; int vi = 0;
                for (int k = 5; k < len && vi < 31 && buf[k] != '\0'; k++)
                    if ((uint8_t)buf[k] >= 0x20 && (uint8_t)buf[k] < 0x7F) ver[vi++] = buf[k];
                banners[i] = vi > 0 ? String(ver) : String("");
            } else if (isHTTP) {
                // Search for "Server:" header — it's on a later line than the status line
                const char* sv = nullptr;
                for (int k = 0; k < len - 8 && !sv; k++) {
                    if (buf[k] == '\n') {
                        const char* cand = buf + k + 1;
                        if (strncasecmp(cand, "Server:", 7) == 0) sv = cand + 7;
                    }
                }
                if (sv) {
                    while (*sv == ' ' || *sv == '\t') sv++;
                    char sval[48] = {0}; int vi = 0;
                    while (*sv && *sv != '\r' && *sv != '\n' && vi < 47) sval[vi++] = *sv++;
                    banners[i] = String(sval);
                } else {
                    // Fallback: status line
                    for (int k = 0; k < len; k++)
                        if ((uint8_t)buf[k] < 0x20) { buf[k] = '\0'; break; }
                    banners[i] = String(buf);
                }
            } else {
                for (int k = 0; k < len; k++)
                    if ((uint8_t)buf[k] < 0x20 || (uint8_t)buf[k] == 0x7F) { buf[k] = '\0'; break; }
                banners[i] = String(buf);
            }
        }

        // ── show result on spinner line ───────────────────────────────────────
        dm.fillRect(10, spinY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(10, spinY);
        if (banners[i].length() > 0) {
            dm.setTextColor(TFT_GREEN);
            char ln[42]; snprintf(ln, sizeof(ln), "%d: %.32s", ports[i], banners[i].c_str());
            dm.println(ln);
        } else {
            dm.setTextColor(0x7BEF);
            char ln[20]; snprintf(ln, sizeof(ln), "%d: -", ports[i]);
            dm.println(ln);
        }
        dm.setTextColor(TFT_WHITE);
        delay(200);   // brief pause so user can read the result before next port
    }

    delay(800);
}

// ── OS detection — TTL fingerprint + banner analysis ─────────────────────────

// TTL detection via lwip raw ICMP — callback fires from the tcpip task
struct _PingCtx { volatile bool done; volatile uint8_t ttl; };

static uint8_t _rawPingRecv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    if (p->len < sizeof(struct icmp_echo_hdr)) return 0;
    struct icmp_echo_hdr* hdr = (struct icmp_echo_hdr*)p->payload;
    if (hdr->type == ICMP_ER && hdr->code == 0 && hdr->id == PP_HTONS(0xABCD)) {
        _PingCtx* ctx = (_PingCtx*)arg;
        const struct ip_hdr* ip = ip4_current_header();
        if (ip) ctx->ttl = IPH_TTL(ip);
        ctx->done = true;
        pbuf_free(p);
        return 1;   // consumed
    }
    return 0;
}

static uint8_t getIPTTL(const IPAddress& ip) {
    ip_addr_t target = {};
    IP4_ADDR(&target.u_addr.ip4, ip[0], ip[1], ip[2], ip[3]);
    target.type = IPADDR_TYPE_V4;

    _PingCtx ctx = {false, 0};

    LOCK_TCPIP_CORE();
    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) { UNLOCK_TCPIP_CORE(); return 0; }
    raw_recv(pcb, _rawPingRecv, &ctx);
    raw_bind(pcb, IP_ADDR_ANY);

    struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
    if (!p) { raw_remove(pcb); UNLOCK_TCPIP_CORE(); return 0; }
    struct icmp_echo_hdr* echo = (struct icmp_echo_hdr*)p->payload;
    ICMPH_TYPE_SET(echo, ICMP_ECHO);
    ICMPH_CODE_SET(echo, 0);
    echo->chksum = 0;
    echo->id     = PP_HTONS(0xABCD);
    echo->seqno  = PP_HTONS(1);
    echo->chksum = inet_chksum(echo, sizeof(*echo));
    raw_sendto(pcb, p, &target);
    pbuf_free(p);
    UNLOCK_TCPIP_CORE();

    for (uint32_t t0 = millis(); !ctx.done && millis() - t0 < 2000;) delay(10);

    LOCK_TCPIP_CORE();
    raw_remove(pcb);
    UNLOCK_TCPIP_CORE();

    return ctx.done ? (uint8_t)ctx.ttl : 0;
}

static String buildOSHint(uint8_t ttl) {
    if (ttl == 0) return "";
    const char* os;
    if      (ttl <= 32)  os = "Embedded";
    else if (ttl <= 64)  os = "Linux/macOS";
    else if (ttl <= 128) os = "Windows";
    else                 os = "Network";
    char buf[28];
    snprintf(buf, sizeof(buf), "%s  TTL %d", os, ttl);
    return String(buf);
}

static String checkBannerOS(const std::vector<int>& ports, const std::vector<String>& banners) {
    // Banner analysis (most accurate)
    for (int i = 0; i < (int)ports.size(); i++) {
        const String& b = banners[i]; if (b.isEmpty()) continue;
        if (ports[i] == 22) {
            if (b.indexOf("Ubuntu")  >= 0) return "Ubuntu Linux";
            if (b.indexOf("Debian")  >= 0) return "Debian Linux";
            if (b.indexOf("CentOS")  >= 0) return "CentOS/RHEL";
            if (b.indexOf("Fedora")  >= 0) return "Fedora";
            if (b.indexOf("Alpine")  >= 0) return "Alpine Linux";
            if (b.indexOf("Windows") >= 0) return "Windows";
            if (!b.isEmpty())              return "Unix/Linux";
        }
        if (ports[i] == 80 || ports[i] == 8080 || ports[i] == 8888 || ports[i] == 9200) {
            if (b.indexOf("IIS")        >= 0) return "Windows/IIS";
            if (b.indexOf("Microsoft")  >= 0) return "Windows";
            if (b.indexOf("ASP.NET")    >= 0) return "Windows";
            if (b.indexOf("Ubuntu")     >= 0) return "Ubuntu Linux";
            if (b.indexOf("Debian")     >= 0) return "Debian Linux";
            if (b.indexOf("CentOS")     >= 0) return "CentOS/RHEL";
            if (b.indexOf("Red Hat")    >= 0) return "RHEL";
            if (b.indexOf("Fedora")     >= 0) return "Fedora";
        }
    }
    // Port-presence fallback — useful before banner grab or when no banners reveal OS
    bool has3389 = false, has135 = false, has445 = false;
    for (int p : ports) {
        if (p == 3389) has3389 = true;
        if (p == 135)  has135  = true;
        if (p == 445)  has445  = true;
    }
    if (has3389)        return "Windows (RDP)";
    if (has135 && has445) return "Windows (SMB)";
    if (has135)         return "Windows (MSRPC)";
    return "";
}

NetworkScanner::NetworkScanner(DisplayManager& displayManager)
    : displayManager(displayManager) {}

// ── ARP network discovery ─────────────────────────────────────────────────────

static void runArpScan(struct netif* nif, uint8_t base[3],
                       DisplayManager& dm, int32_t progressY, bool& stopped) {
    const int BATCH = 8;
    g_arpResults.clear();
    stopped = false;

    for (int batchStart = 1; batchStart <= 254 && !stopped; batchStart += BATCH) {
        int batchEnd = min(batchStart + BATCH - 1, 254);
        int pct      = (batchStart * 100) / 254;

        char prog[40];
        snprintf(prog, sizeof(prog), "Scanning .%d-.%d  (%d%%)", batchStart, batchEnd, pct);
        dm.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
        dm.setCursor(10, progressY);
        dm.setTextColor(TFT_YELLOW);
        dm.printText(prog);
        dm.setTextColor(TFT_WHITE);

        for (int i = batchStart; i <= batchEnd; i++) {
            ip4_addr_t t;
            IP4_ADDR(&t, base[0], base[1], base[2], i);
            LOCK_TCPIP_CORE();
            etharp_request(nif, &t);
            UNLOCK_TCPIP_CORE();
            delayMicroseconds(500);
        }

        delay(250);

        for (int i = batchStart; i <= batchEnd; i++) {
            ip4_addr_t t;
            IP4_ADDR(&t, base[0], base[1], base[2], i);
            struct eth_addr* eth_ret = nullptr;
            const ip4_addr_t* ip_ret = nullptr;

            LOCK_TCPIP_CORE();
            s8_t hit = etharp_find_addr(nif, &t, &eth_ret, &ip_ret);
            UNLOCK_TCPIP_CORE();

            if (hit >= 0 && eth_ret) {
                ArpEntry e;
                e.ip = IPAddress(base[0], base[1], base[2], i);
                memcpy(e.mac, eth_ret->addr, 6);
                g_arpResults.push_back(e);
            }
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') stopped = true;
    }
}

static void renderArpPage(DisplayManager& dm, int page, int perPage,
                          int total, int totalPages) {
    dm.clearScreen();
    dm.setCursor(10, outputY);
    char title[20]; snprintf(title, sizeof(title), "%02d/%02d  %d hosts", page + 1, totalPages, total);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("SCAN");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText("ARP");
    dm.setTextColor(0x7BEF);     dm.printText("]  ");
    dm.setTextColor(0x7BEF);     dm.println(title);
    dm.printSeparator();
    dm.setTextColor(TFT_WHITE);

    int start = page * perPage;
    int end   = min(start + perPage, total);

    if (total == 0) {
        dm.setTextColor(TFT_YELLOW);
        dm.setCursor(10, dm.getCursorY());
        dm.println("No hosts found.");
        dm.setTextColor(TFT_WHITE);
    }

    for (int i = start; i < end; i++) {
        const ArpEntry& e = g_arpResults[i];
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        char lbl[6]; snprintf(lbl, sizeof(lbl), "[%2d]", i);
        dm.printText(lbl);
        dm.setTextColor(TFT_WHITE);
        char ip[17]; snprintf(ip, sizeof(ip), " %-15s", e.ip.toString().c_str());
        dm.printText(ip);
        dm.setTextColor(0x7BEF);
        char mac[20];
        snprintf(mac, sizeof(mac), " %02X:%02X:%02X:%02X:%02X:%02X",
                 e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
        dm.println(mac);
        dm.setTextColor(TFT_WHITE);
    }

    dm.printSeparator();
    dm.setCursor(10, dm.getCursorY());
    dm.printDefaultTableHelpInstructions();
}

void NetworkScanner::showHostResults() {
    if (g_arpResults.empty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("No scan data. Run netdiscover first.");
        displayManager.printCommandScreen();
        return;
    }
    const int perPage  = 10;
    int total          = (int)g_arpResults.size();
    int totalPages     = max(1, (total + perPage - 1) / perPage);
    int currentPage    = 0;
    while (true) {
        renderArpPage(displayManager, currentPage, perPage, total, totalPages);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        }
    }
}

void NetworkScanner::networkDiscovery() {
    if (WiFi.status() != WL_CONNECTED) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Connect to WiFi first.");
        displayManager.printCommandScreen();
        return;
    }

    IPAddress gw  = WiFi.gatewayIP();
    uint8_t base[3] = { (uint8_t)gw[0], (uint8_t)gw[1], (uint8_t)gw[2] };
    struct netif* nif = netif_default;

    const int perPage = 10;

    while (true) {
        // ── scan progress screen ──────────────────────────────────────────────
        displayManager.clearScreen();
        displayManager.setCursor(10, outputY);
        displayManager.setTextColor(TFT_CYAN);
        displayManager.println("-- ARP Scan --");
        displayManager.setTextColor(TFT_WHITE);
        displayManager.setCursor(10, displayManager.getCursorY());
        char subnet[28];
        snprintf(subnet, sizeof(subnet), "Subnet: %d.%d.%d.0/24", base[0], base[1], base[2]);
        displayManager.println(subnet);
        displayManager.setCursor(10, displayManager.getCursorY());

        int32_t progressY = displayManager.getCursorY();
        displayManager.println("Initializing...");
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.println("q=cancel ──────────────────");
        displayManager.setTextColor(TFT_WHITE);

        bool stopped = false;
        runArpScan(nif, base, displayManager, progressY, stopped);

        // Brief "done" flash on progress line
        displayManager.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
        displayManager.setCursor(10, progressY);
        displayManager.setTextColor(stopped ? TFT_YELLOW : TFT_GREEN);
        char doneMsg[32];
        snprintf(doneMsg, sizeof(doneMsg), "%s — %d host(s)",
                 stopped ? "Stopped" : "Done", (int)g_arpResults.size());
        displayManager.printText(doneMsg);
        displayManager.setTextColor(TFT_WHITE);
        delay(700);

        // ── paginated table ───────────────────────────────────────────────────
        int total      = (int)g_arpResults.size();
        int totalPages = max(1, (total + perPage - 1) / perPage);
        int currentPage = 0;
        bool doRescan  = false;

        while (!doRescan) {
            renderArpPage(displayManager, currentPage, perPage, total, totalPages);

            while (true) {
                char k = inputHandler.getKeyboardInput();
                if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
                if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
                if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
                if (k == 'u' || k == 'U') { doRescan = true; break; }
                if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
            }
        }
        // doRescan == true → outer while loops back and re-runs the scan
    }
}

// ── parallel port scan — 4 tasks × 150 ms timeout ────────────────────────────

#define PARALLEL_SCAN_TASKS 4

static volatile int  g_parallelDone = 0;
static volatile bool g_parallelStop = false;
static QueueHandle_t g_portQueue    = nullptr;

struct PortSegment {
    uint32_t     targetIPRaw;
    int          startPort, endPort;
    volatile int currentPort;
};

static void portSegTaskFn(void* p) {
    PortSegment* seg = (PortSegment*)p;
    IPAddress ip(seg->targetIPRaw);
    for (int port = seg->startPort; port <= seg->endPort; port++) {
        if (g_parallelStop) break;
        seg->currentPort = port;
        WiFiClient c;
        if (c.connect(ip, port, 150)) {
            c.stop();
            TaskResult r; r.type = TaskResult::PORT_OPEN;
            snprintf(r.data, sizeof(r.data), "%d", port);
            xQueueSend(g_portQueue, &r, pdMS_TO_TICKS(100));
        }
    }
    int finished = __sync_add_and_fetch((int*)&g_parallelDone, 1);
    if (finished >= PARALLEL_SCAN_TASKS) {
        TaskResult fin; fin.type = TaskResult::DONE; fin.data[0] = '\0';
        xQueueSend(g_portQueue, &fin, pdMS_TO_TICKS(1000));
    }
    vTaskDelete(nullptr);
}

void NetworkScanner::networkPortScan(char* args) {
    String tok0 = utils.getValue(args, ' ', 0);
    String tok1 = utils.getValue(args, ' ', 1);
    String tok2 = utils.getValue(args, ' ', 2);

    if (tok0.isEmpty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage: ps <ip|arpIdx> <start> <end>");
        displayManager.printCommandScreen();
        return;
    }

    IPAddress targetIp;
    if (!resolveTarget(tok0, targetIp)) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Bad IP or ARP index out of range.");
        displayManager.setTextColor(TFT_WHITE);
        delay(1500);
        displayManager.printCommandScreen();
        return;
    }

    if (tok1.isEmpty() || tok2.isEmpty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage: ps <ip|arpIdx> <start> <end>");
        displayManager.printCommandScreen();
        return;
    }

    int sp = tok1.toInt(), ep = tok2.toInt();
    if (sp < 1 || ep > 65535 || sp > ep) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Invalid port range (1-65535).");
        displayManager.setTextColor(TFT_WHITE);
        delay(1500);
        displayManager.printCommandScreen();
        return;
    }

    performPortScan(targetIp, sp, ep);
}

static void renderPortTable(DisplayManager& dm, const IPAddress& target,
                            const std::vector<int>& ports,
                            const std::vector<String>& banners,
                            int page, int perPage,
                            int total, int totalPages,
                            const String& osHint = "") {
    dm.clearScreen();
    dm.setCursor(10, outputY);
    dm.setTextColor(TFT_CYAN);
    char title[36];
    snprintf(title, sizeof(title), "-- Open Ports --  [%d/%d]", page + 1, totalPages);
    dm.println(title);
    dm.setTextColor(TFT_WHITE);
    dm.setCursor(10, dm.getCursorY());
    dm.printText("Target: "); dm.println(target.toString());
    if (osHint.length() > 0) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_WHITE);  dm.printText("OS:     ");
        dm.setTextColor(TFT_CYAN);   dm.println(osHint);
        dm.setTextColor(TFT_WHITE);
    }
    dm.setTextColor(0x7BEF);
    dm.setCursor(10, dm.getCursorY());
    dm.println("──────────────────────────────");
    dm.setTextColor(TFT_WHITE);

    int start = page * perPage;
    int end   = min(start + perPage, total);

    if (total == 0) {
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_YELLOW);
        dm.println("No open ports found.");
        dm.setTextColor(TFT_WHITE);
    }

    for (int i = start; i < end; i++) {
        int port = ports[i];
        const char* svc = portService(port);
        dm.setCursor(10, dm.getCursorY());
        dm.setTextColor(TFT_GREEN);
        char portStr[8]; snprintf(portStr, sizeof(portStr), "%5d", port);
        dm.printText(portStr);
        dm.setTextColor(TFT_WHITE);
        dm.printText("  ");
        dm.setTextColor(svc[0] ? TFT_CYAN : 0x7BEF);
        dm.printText(svc[0] ? svc : "?");
        if (i < (int)banners.size() && banners[i].length() > 0) {
            char bnr[32];
            snprintf(bnr, sizeof(bnr), "  %.28s", banners[i].c_str());
            dm.setTextColor(0x7BEF);
            dm.printText(bnr);
        }
        dm.println("");
        dm.setTextColor(TFT_WHITE);
    }

    dm.setTextColor(0x7BEF);
    dm.setCursor(10, dm.getCursorY());
    dm.println("──────────────────────────────");
    dm.setCursor(10, dm.getCursorY());
    dm.setTextColor(total > 0 ? TFT_GREEN : TFT_YELLOW);
    char summary[16]; snprintf(summary, sizeof(summary), "%d open", total);
    dm.printText(summary);
    dm.setTextColor(0x7BEF);
    dm.println("  a=prev l=next b=grab q=quit");
    dm.setTextColor(TFT_WHITE);
}

void NetworkScanner::performPortScan(const IPAddress& targetIP, int startPort, int endPort) {
    int totalPorts = endPort - startPort + 1;
    int segSize    = max(1, totalPorts / PARALLEL_SCAN_TASKS);

    PortSegment segs[PARALLEL_SCAN_TASKS];
    for (int t = 0; t < PARALLEL_SCAN_TASKS; t++) {
        segs[t].targetIPRaw = (uint32_t)targetIP;
        segs[t].startPort   = startPort + t * segSize;
        segs[t].endPort     = (t == PARALLEL_SCAN_TASKS - 1)
                              ? endPort
                              : segs[t].startPort + segSize - 1;
        segs[t].currentPort = segs[t].startPort;
    }

    g_parallelDone = 0;
    g_parallelStop = false;
    g_portQueue    = xQueueCreate(128, sizeof(TaskResult));

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Port Scan (4x parallel) --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Target: "); displayManager.println(targetIP.toString());
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Range:  ");
    displayManager.printText(startPort); displayManager.printText("-"); displayManager.println(endPort);
    displayManager.setCursor(10, displayManager.getCursorY());

    int32_t progressY = displayManager.getCursorY();
    displayManager.println("Starting tasks...");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("q=stop ──────────────────");
    displayManager.setTextColor(TFT_WHITE);

    for (int t = 0; t < PARALLEL_SCAN_TASKS; t++)
        xTaskCreatePinnedToCore(portSegTaskFn, "pseg", 6144, &segs[t], 2, nullptr, 0);

    // Grab TTL concurrently while tasks spin up — adds no perceived latency
    String osHint = buildOSHint(getIPTTL(targetIP));

    std::vector<int> openPorts;
    unsigned long lastProgUpd = 0;
    bool running = true;

    while (running) {
        unsigned long now = millis();
        if (now - lastProgUpd > 350) {
            int scanned = 0;
            for (int t = 0; t < PARALLEL_SCAN_TASKS; t++)
                scanned += segs[t].currentPort - segs[t].startPort;
            char prog[48];
            snprintf(prog, sizeof(prog), "Scanned %d/%d (%d%%) — %d open",
                     scanned, totalPorts, (scanned * 100) / totalPorts, (int)openPorts.size());
            displayManager.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
            displayManager.setCursor(10, progressY);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.printText(prog);
            displayManager.setTextColor(TFT_WHITE);
            lastProgUpd = now;
        }

        TaskResult r;
        if (xQueueReceive(g_portQueue, &r, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (r.type == TaskResult::PORT_OPEN)
                openPorts.push_back(atoi(r.data));
            else if (r.type == TaskResult::DONE)
                running = false;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') {
            g_parallelStop = true;
            while (g_parallelDone < PARALLEL_SCAN_TASKS) delay(20);
            running = false;
        }
    }

    // Drain any remaining results from the queue
    TaskResult r;
    while (xQueueReceive(g_portQueue, &r, 0) == pdTRUE)
        if (r.type == TaskResult::PORT_OPEN) openPorts.push_back(atoi(r.data));
    vQueueDelete(g_portQueue);
    g_portQueue = nullptr;

    std::sort(openPorts.begin(), openPorts.end());
    std::vector<String> banners(openPorts.size(), "");

    // Port-presence OS hint (no banner needed — RDP/MSRPC/SMB identify Windows immediately)
    { String pos = checkBannerOS(openPorts, banners); if (pos.length() > 0) osHint = pos; }

    // Brief done flash
    displayManager.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
    displayManager.setCursor(10, progressY);
    displayManager.setTextColor(TFT_GREEN);
    char doneMsg[32];
    snprintf(doneMsg, sizeof(doneMsg), "Done — %d open port(s)", (int)openPorts.size());
    displayManager.printText(doneMsg);
    displayManager.setTextColor(TFT_WHITE);
    delay(700);

    // Paginated results table
    const int perPage  = 8;
    int total          = (int)openPorts.size();
    int totalPages     = max(1, (total + perPage - 1) / perPage);
    int currentPage    = 0;

    while (true) {
        renderPortTable(displayManager, targetIP, openPorts, banners, currentPage, perPage, total, totalPages, osHint);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (k == 'b' || k == 'B') {
                grabAllBanners(displayManager, targetIP, openPorts, banners);
                String bos = checkBannerOS(openPorts, banners);
                if (bos.length() > 0) osHint = bos;
                break;
            }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        }
    }
}

// ── top ports scan — single task, 26 common ports ────────────────────────────

struct TopScanParams {
    uint32_t     targetIPRaw;
    volatile int currentIdx;
};

static void topScanTaskFn(void* p) {
    TopScanParams* params = (TopScanParams*)p;
    IPAddress ip(params->targetIPRaw);
    for (int i = 0; i < TOP_PORTS_COUNT; i++) {
        if (TaskManager::stopRequested) break;
        params->currentIdx = i;
        WiFiClient c;
        if (c.connect(ip, TOP_PORTS[i], 150)) {
            c.stop();
            TaskResult r; r.type = TaskResult::PORT_OPEN;
            snprintf(r.data, sizeof(r.data), "%d", TOP_PORTS[i]);
            xQueueSend(TaskManager::resultQueue, &r, pdMS_TO_TICKS(100));
        }
    }
    TaskResult done; done.type = TaskResult::DONE; done.data[0] = '\0';
    xQueueSend(TaskManager::resultQueue, &done, pdMS_TO_TICKS(1000));
    TaskManager::taskRunning = false;
    vTaskDelete(nullptr);
}

void NetworkScanner::topPortScan(char* args) {
    String tok = args ? utils.getValue(args, ' ', 0) : String("");
    if (tok.isEmpty()) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage: ts <ip|arpIdx>");
        displayManager.printCommandScreen();
        return;
    }

    IPAddress targetIp;
    if (!resolveTarget(tok, targetIp)) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(TFT_RED);
        displayManager.println("Bad IP or ARP index out of range.");
        displayManager.setTextColor(TFT_WHITE);
        delay(1500);
        displayManager.printCommandScreen();
        return;
    }

    TopScanParams params;
    params.targetIPRaw = (uint32_t)targetIp;
    params.currentIdx  = 0;

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Top Ports Scan --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Target: "); displayManager.println(targetIp.toString());
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Ports:  ");
    displayManager.setTextColor(0x7BEF);
    displayManager.println(TOP_PORTS_COUNT);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());

    int32_t progressY = displayManager.getCursorY();
    displayManager.println("Scanning...");
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("q=stop ──────────────────");
    displayManager.setTextColor(TFT_WHITE);

    if (!TaskManager::start(topScanTaskFn, "topscan", &params)) {
        displayManager.println("Task start failed.");
        displayManager.printCommandScreen();
        return;
    }

    // Grab TTL while the scan task is running
    String osHint = buildOSHint(getIPTTL(targetIp));

    std::vector<int> openPorts;
    unsigned long lastUpd = 0;

    while (TaskManager::isRunning() || uxQueueMessagesWaiting(TaskManager::resultQueue) > 0) {
        unsigned long now = millis();
        if (now - lastUpd > 300) {
            int idx = min((int)params.currentIdx, TOP_PORTS_COUNT - 1);
            char prog[40];
            snprintf(prog, sizeof(prog), "Checking %d  (%d/%d) — %d open",
                     TOP_PORTS[idx], idx + 1, TOP_PORTS_COUNT, (int)openPorts.size());
            displayManager.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
            displayManager.setCursor(10, progressY);
            displayManager.setTextColor(TFT_YELLOW);
            displayManager.printText(prog);
            displayManager.setTextColor(TFT_WHITE);
            lastUpd = now;
        }

        TaskResult r;
        if (xQueueReceive(TaskManager::resultQueue, &r, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (r.type == TaskResult::PORT_OPEN)
                openPorts.push_back(atoi(r.data));
            else if (r.type == TaskResult::DONE)
                break;
        }

        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') { TaskManager::requestStop(); break; }
    }

    TaskManager::cleanup();
    std::vector<String> banners(openPorts.size(), "");

    // Port-presence OS hint
    { String pos = checkBannerOS(openPorts, banners); if (pos.length() > 0) osHint = pos; }

    // Brief done flash
    displayManager.fillRect(10, progressY, SCREEN_WIDTH - 10, LINE_HEIGHT + 2, TFT_BLACK);
    displayManager.setCursor(10, progressY);
    displayManager.setTextColor(TFT_GREEN);
    char doneMsg[32];
    snprintf(doneMsg, sizeof(doneMsg), "Done — %d open port(s)", (int)openPorts.size());
    displayManager.printText(doneMsg);
    displayManager.setTextColor(TFT_WHITE);
    delay(700);

    // Paginated results table
    const int perPage  = 8;
    int total          = (int)openPorts.size();
    int totalPages     = max(1, (total + perPage - 1) / perPage);
    int currentPage    = 0;

    while (true) {
        renderPortTable(displayManager, targetIp, openPorts, banners, currentPage, perPage, total, totalPages, osHint);
        while (true) {
            char k = inputHandler.getKeyboardInput();
            if (k == 'l' || k == 'L') { if (currentPage < totalPages - 1) currentPage++; break; }
            if (k == 'a' || k == 'A') { if (currentPage > 0)              currentPage--; break; }
            if (k == 'q' || k == 'Q') { displayManager.printCommandScreen(); return; }
            if (k == 'b' || k == 'B') {
                grabAllBanners(displayManager, targetIp, openPorts, banners);
                String bos = checkBannerOS(openPorts, banners);
                if (bos.length() > 0) osHint = bos;
                break;
            }
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;
        }
    }
}

// ── ping ──────────────────────────────────────────────────────────────────────

void NetworkScanner::pingHost(char* args) {
    if (!args || !*args) {
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.println("Usage: ping <ip or host>");
        displayManager.printCommandScreen();
        return;
    }

    String host(args);
    host.trim();

    displayManager.clearScreen();
    displayManager.setCursor(10, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println("-- Ping --");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.printText("Host: ");
    displayManager.println(host);
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("q=stop  ─────────────────");
    displayManager.setTextColor(TFT_WHITE);

    IPAddress target;
    if (!target.fromString(host)) {
        int err = WiFi.hostByName(host.c_str(), target);
        if (err != 1) {
            displayManager.setTextColor(TFT_RED);
            displayManager.setCursor(10, displayManager.getCursorY());
            displayManager.println("Could not resolve host.");
            displayManager.setTextColor(TFT_WHITE);
            delay(2000);
            displayManager.printCommandScreen();
            return;
        }
        displayManager.setCursor(10, displayManager.getCursorY());
        displayManager.setTextColor(0x7BEF);
        displayManager.printText("Resolved: ");
        displayManager.println(target.toString());
        displayManager.setTextColor(TFT_WHITE);
    }

    int sent = 0, received = 0;
    const int count = 10;

    for (int i = 1; i <= count; i++) {
        char k = inputHandler.getKeyboardInput();
        if (k == 'q' || k == 'Q') break;

        sent++;
        unsigned long t0 = millis();
        bool ok = Ping.ping(target, 1);
        unsigned long rtt = millis() - t0;

        displayManager.setCursor(10, displayManager.getCursorY());
        if (ok) {
            received++;
            displayManager.setTextColor(TFT_GREEN);
            displayManager.printText("[+] ");
            displayManager.printText(target.toString().c_str());
            displayManager.setTextColor(TFT_WHITE);
            displayManager.printText("  ");
            displayManager.printText((int)rtt);
            displayManager.println(" ms");
        } else {
            displayManager.setTextColor(TFT_RED);
            displayManager.println("[-] Request timed out.");
            displayManager.setTextColor(TFT_WHITE);
        }
        delay(1000);
    }

    int loss = sent > 0 ? ((sent - received) * 100 / sent) : 100;
    displayManager.setCursor(10, displayManager.getCursorY());
    displayManager.setTextColor(0x7BEF);
    displayManager.println("──────────────────────────");
    displayManager.setTextColor(TFT_WHITE);
    displayManager.setCursor(10, displayManager.getCursorY());
    char summary[48];
    snprintf(summary, sizeof(summary), "%d sent  %d recv  %d%% loss", sent, received, loss);
    displayManager.setTextColor(loss == 0 ? TFT_GREEN : loss == 100 ? TFT_RED : TFT_YELLOW);
    displayManager.println(summary);
    displayManager.setTextColor(TFT_WHITE);
    displayManager.printCommandScreen();
}
