// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// Interactive SSH client over the existing WiFi STA connection, built on the
// LibSSH-ESP32 port of libssh. Password auth + PTY shell.
//
// Terminal: a scrollback buffer (SB lines) with per-cell ANSI 16-colour
// attributes. The visible window shows ROWS lines; trackpad UP/DOWN scrolls
// through history, typing snaps back to live, trackpad CLICK disconnects.
//
// libssh needs a big stack, so the whole session runs in a dedicated FreeRTOS
// task (~50 KB) while the CLI command waits for it to finish.
//
// KNOWN: the docs recommend disabling CONFIG_MBEDTLS_HARDWARE_SHA for stability
// under concurrency — not possible with the precompiled Arduino core, so if a
// crash happens during the connect/key-exchange phase, the shared hardware SHA
// engine is the prime suspect.

#include "ssh_client.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "utilities.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <strings.h>   // strcasecmp

#include "libssh_esp32.h"
#include <libssh/libssh.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;
extern LGFX           tft;

// ── Terminal geometry ─────────────────────────────────────────────────────────
#define TERM_Y    (outputY + LINE_HEIGHT)            // one header line above grid
#define ROWS      13                                 // visible rows
#define COLS      52                                 // visible columns
#define SB        120                                // scrollback lines
#define CHAR_W    6
#define CELL_X(c) (2 + (c) * CHAR_W)
#define CELL_Y(r) (TERM_Y + (r) * LINE_HEIGHT)
#define TERM_W    (COLS * CHAR_W)
#define TERM_H    (SCREEN_HEIGHT - TERM_Y)
#define SBAR_X    314                                // scrollbar

// ANSI 16-colour palette → RGB565.
static const uint16_t PAL[16] = {
    0x0000, 0xA800, 0x0540, 0xAAA0, 0x001F, 0xA815, 0x0555, 0xAD55,
    0x52AA, 0xF800, 0x07E0, 0xFFE0, 0x349F, 0xF81F, 0x07FF, 0xFFFF
};
#define DEF_FG 7
#define DEF_ATTR ((uint8_t)((DEF_FG << 4) | 0))      // fg=7 (light grey), bg=0

// Scrollback ring: logical line L (0..count-1) → s_buf[(head+L)%SB].
static char    s_buf[SB][COLS];
static uint8_t s_col[SB][COLS];
static int     s_head, s_count;       // ring base + valid line count
static int     s_cr, s_cc;            // cursor within visible region (0..ROWS-1)
static int     s_view;                // scroll-up offset (0 = live/bottom)
static uint8_t s_attr;                // current SGR attribute
static int     s_fg, s_bg;            // current fg/bg index
static bool    s_bold;
static bool    s_rowDirty[ROWS], s_allDirty;

static inline int ringIdx(int logical) { return (s_head + logical) % SB; }
static inline int liveTop()  { return s_count - ROWS; }            // top live line
static inline int maxScroll(){ return s_count - ROWS; }

static void termInit() {
    s_head = 0; s_count = ROWS; s_cr = s_cc = 0; s_view = 0;
    s_fg = DEF_FG; s_bg = 0; s_bold = false; s_attr = DEF_ATTR;
    for (int i = 0; i < SB; i++) { memset(s_buf[i], ' ', COLS); memset(s_col[i], DEF_ATTR, COLS); }
    s_allDirty = true;
    for (int r = 0; r < ROWS; r++) s_rowDirty[r] = false;
    tft.fillRect(0, TERM_Y, SCREEN_WIDTH, TERM_H, TFT_BLACK);
}

static void appendLine() {
    if (s_count < SB) s_count++;
    else              s_head = (s_head + 1) % SB;     // drop oldest
    int idx = ringIdx(s_count - 1);
    memset(s_buf[idx], ' ', COLS);
    memset(s_col[idx], s_attr, COLS);
    if (s_view > 0 && s_view < maxScroll()) s_view++; // keep scrolled view stable
}

static void termNewline() {
    s_cc = 0;
    if (++s_cr >= ROWS) { appendLine(); s_cr = ROWS - 1; s_allDirty = true; }
}

static void putPrintable(char ch) {
    if (s_cc >= COLS) termNewline();
    int idx = ringIdx(liveTop() + s_cr);
    s_buf[idx][s_cc] = ch;
    s_col[idx][s_cc] = s_attr;
    s_rowDirty[s_cr] = true;
    s_cc++;
}

static void eraseLineToEnd() {
    int idx = ringIdx(liveTop() + s_cr);
    for (int c = s_cc; c < COLS; c++) { s_buf[idx][c] = ' '; s_col[idx][c] = s_attr; }
    s_rowDirty[s_cr] = true;
}

static void eraseScreen() {                            // ESC[2J — keep history
    for (int r = 0; r < ROWS; r++) {
        int idx = ringIdx(liveTop() + r);
        memset(s_buf[idx], ' ', COLS);
        memset(s_col[idx], s_attr, COLS);
    }
    s_cr = s_cc = 0; s_allDirty = true;
}

// ── ANSI/VT100 parser ─────────────────────────────────────────────────────────
static int  s_esc = 0;       // 0 none · 1 ESC · 2 CSI · 3 OSC
static char s_csi[24];
static int  s_csiLen;

static void applySgr(int p) {
    if (p == 0)                   { s_fg = DEF_FG; s_bg = 0; s_bold = false; }
    else if (p == 1)              s_bold = true;
    else if (p == 22)             s_bold = false;
    else if (p == 39)             s_fg = DEF_FG;
    else if (p == 49)             s_bg = 0;
    else if (p >= 30 && p <= 37)  s_fg = p - 30;
    else if (p >= 90 && p <= 97)  s_fg = p - 90 + 8;
    else if (p >= 40 && p <= 47)  s_bg = p - 40;
    else if (p >= 100 && p <= 107) s_bg = p - 100 + 8;
    int fg = s_fg; if (s_bold && fg < 8) fg += 8;
    s_attr = (uint8_t)((fg << 4) | (s_bg & 0x0F));
}

static void csiFinal(char f) {
    int params[8], np = 0, v = 0; bool any = false;
    for (int i = 0; i < s_csiLen && np < 8; i++) {
        char c = s_csi[i];
        if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = true; }
        else if (c == ';')        { params[np++] = v; v = 0; any = false; }
    }
    if (any || np > 0) { if (np < 8) params[np++] = v; }
    int p0 = np > 0 ? params[0] : 0;

    switch (f) {
        case 'm':
            if (np == 0) { applySgr(0); break; }
            for (int i = 0; i < np; i++) {
                int p = params[i];
                if ((p == 38 || p == 48) && i + 1 < np) {       // extended colour
                    if (params[i + 1] == 5) {                   // 38;5;n  (256-colour)
                        int n = (i + 2 < np) ? params[i + 2] : 0;
                        int ci = (n < 16) ? n : (n < 244 ? 7 : 15);
                        if (p == 38) s_fg = ci; else s_bg = (ci & 0x0F);
                        i += 2;
                    } else if (params[i + 1] == 2) {            // 38;2;r;g;b (truecolor)
                        i += 4;                                 // skip, keep current
                    }
                    int fg = s_fg; if (s_bold && fg < 8) fg += 8;
                    s_attr = (uint8_t)((fg << 4) | (s_bg & 0x0F));
                } else applySgr(p);
            }
            break;
        case 'H': case 'f':
            s_cr = (p0 > 0 ? p0 - 1 : 0); if (s_cr >= ROWS) s_cr = ROWS - 1;
            s_cc = (np > 1 && params[1] > 0 ? params[1] - 1 : 0); if (s_cc >= COLS) s_cc = COLS - 1;
            break;
        case 'J': eraseScreen(); break;
        case 'K': eraseLineToEnd(); break;
        case 'A': s_cr -= (p0 ? p0 : 1); if (s_cr < 0) s_cr = 0; break;
        case 'B': s_cr += (p0 ? p0 : 1); if (s_cr >= ROWS) s_cr = ROWS - 1; break;
        case 'C': s_cc += (p0 ? p0 : 1); if (s_cc >= COLS) s_cc = COLS - 1; break;
        case 'D': s_cc -= (p0 ? p0 : 1); if (s_cc < 0) s_cc = 0; break;
        default: break;
    }
}

static void termPut(char ch) {
    switch (s_esc) {
        case 1: if (ch == '[') { s_esc = 2; s_csiLen = 0; } else if (ch == ']') s_esc = 3; else s_esc = 0; return;
        case 2: if (ch >= 0x40 && ch <= 0x7E) { csiFinal(ch); s_esc = 0; }
                else if (s_csiLen < (int)sizeof(s_csi) - 1) s_csi[s_csiLen++] = ch;
                return;
        case 3: if (ch == 0x07 || ch == 0x1B) s_esc = 0; return;
    }
    switch (ch) {
        case 0x1B: s_esc = 1; break;
        case '\n': termNewline(); break;
        case '\r': s_cc = 0; break;
        case '\b': if (s_cc > 0) s_cc--; break;
        case '\t': s_cc = ((s_cc / 8) + 1) * 8; if (s_cc >= COLS) s_cc = COLS - 1; break;
        case 0x07: break;
        default: if ((uint8_t)ch >= 32) putPrintable(ch); break;
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────
static void drawRow(int r, int absLine) {
    int idx = ringIdx(absLine);
    int y = CELL_Y(r);
    tft.fillRect(2, y, TERM_W, LINE_HEIGHT, PAL[0]);
    for (int c = 0; c < COLS; c++) {
        uint8_t a = s_col[idx][c];
        int bg = a & 0x0F, fg = (a >> 4) & 0x0F;
        int x = CELL_X(c);
        if (bg) tft.fillRect(x, y, CHAR_W, LINE_HEIGHT, PAL[bg]);
        char ch = s_buf[idx][c];
        if (ch > 32) { tft.setCursor(x, y); tft.setTextColor(PAL[fg], PAL[bg]); tft.print(ch); }
    }
}

static void drawScrollbar(int topLine) {
    if (s_count <= ROWS) { tft.fillRect(SBAR_X, TERM_Y, 4, TERM_H, TFT_BLACK); return; }
    tft.fillRect(SBAR_X, TERM_Y, 4, TERM_H, 0x2104);                 // track
    int thumbH = ROWS * TERM_H / s_count; if (thumbH < 6) thumbH = 6;
    int thumbY = TERM_Y + topLine * TERM_H / s_count;
    if (thumbY + thumbH > TERM_Y + TERM_H) thumbY = TERM_Y + TERM_H - thumbH;
    tft.fillRect(SBAR_X, thumbY, 4, thumbH, s_view > 0 ? TFT_YELLOW : TFT_CYAN);
}

static void termRender() {
    int topLine = liveTop() - s_view;
    if (topLine < 0) topLine = 0;
    for (int r = 0; r < ROWS; r++) {
        if (s_allDirty || s_rowDirty[r]) { drawRow(r, topLine + r); s_rowDirty[r] = false; }
    }
    drawScrollbar(topLine);
    s_allDirty = false;
}

static void drawHeader(const char* msg, uint16_t col) {
    tft.fillRect(0, outputY, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(2, outputY);
    tft.setTextColor(col, TFT_BLACK);
    tft.print(msg);
}

// ── Session params ────────────────────────────────────────────────────────────
struct SshParams { char host[64]; char user[40]; char pass[64]; int port; };
static SshParams     s_p;
static volatile bool s_taskDone = false;
static char          s_status[96];
static char          s_hdr[72];

static void redrawHeader() {
    if (s_view > 0) { char b[80]; snprintf(b, sizeof(b), "%s  [SCROLL -%d]", s_hdr, s_view); drawHeader(b, TFT_YELLOW); }
    else drawHeader(s_hdr, TFT_CYAN);
}

// ── SSH session task ──────────────────────────────────────────────────────────
static void sshTask(void* arg) {
    (void)arg;
    snprintf(s_status, sizeof(s_status), "Connecting to %s ...", s_p.host);
    drawHeader(s_status, TFT_YELLOW);

    libssh_begin();
    ssh_session session = ssh_new();
    if (!session) { snprintf(s_status, sizeof(s_status), "ssh_new failed"); s_taskDone = true; vTaskDelete(NULL); return; }

    int port = s_p.port > 0 ? s_p.port : 22; long tmo = 15; int noCfg = 0;
    ssh_options_set(session, SSH_OPTIONS_HOST, s_p.host);
    ssh_options_set(session, SSH_OPTIONS_USER, s_p.user);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &tmo);
    ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &noCfg);

    if (ssh_connect(session) != SSH_OK) {
        snprintf(s_status, sizeof(s_status), "Connect failed: %s", ssh_get_error(session));
        ssh_free(session); ssh_finalize(); s_taskDone = true; vTaskDelete(NULL); return;
    }
    drawHeader("Authenticating ...", TFT_YELLOW);
    if (ssh_userauth_password(session, NULL, s_p.pass) != SSH_AUTH_SUCCESS) {
        snprintf(s_status, sizeof(s_status), "Auth failed: %s", ssh_get_error(session));
        ssh_disconnect(session); ssh_free(session); ssh_finalize(); s_taskDone = true; vTaskDelete(NULL); return;
    }

    ssh_channel channel = ssh_channel_new(session);
    if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
        snprintf(s_status, sizeof(s_status), "Channel open failed");
        if (channel) ssh_channel_free(channel);
        ssh_disconnect(session); ssh_free(session); ssh_finalize(); s_taskDone = true; vTaskDelete(NULL); return;
    }
    ssh_channel_request_pty_size(channel, "xterm", COLS, ROWS);   // 16-colour range
    if (ssh_channel_request_shell(channel) != SSH_OK) {
        snprintf(s_status, sizeof(s_status), "Shell request failed");
        ssh_channel_close(channel); ssh_channel_free(channel);
        ssh_disconnect(session); ssh_free(session); ssh_finalize(); s_taskDone = true; vTaskDelete(NULL); return;
    }

    termInit();
    snprintf(s_hdr, sizeof(s_hdr), "%s@%s [trk]scroll [click]quit", s_p.user, s_p.host);
    redrawHeader();
    termRender();

    char buf[512];
    uint32_t lastRender = 0;
    bool running = true;
    while (running && !ssh_channel_is_eof(channel)) {
        bool live = (s_view == 0);

        int n = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 0);
        if (n > 0)            { for (int i = 0; i < n; i++) termPut(buf[i]); }
        else if (n == SSH_ERROR) break;
        int e = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 1);
        if (e > 0)            { for (int i = 0; i < e; i++) termPut(buf[i]); }

        char k = inputHandler.getKeyboardInput();
        if (k) {
            if (s_view != 0) { s_view = 0; s_allDirty = true; redrawHeader(); }  // snap to live
            char tx = k;
            if (k == '\n' || k == '\r') tx = '\r';
            else if (k == '\b')         tx = 0x7F;
            ssh_channel_write(channel, &tx, 1);
        }

        TrackballEvent tb = inputHandler.getTrackballEvent();
        if (tb == TBALL_CLICK)      running = false;
        else if (tb == TBALL_UP)    { if (s_view < maxScroll()) { s_view++; s_allDirty = true; redrawHeader(); } }
        else if (tb == TBALL_DOWN)  { if (s_view > 0)           { s_view--; s_allDirty = true; redrawHeader(); } }

        uint32_t now = millis();
        bool dirty = s_allDirty;
        if (!dirty) for (int r = 0; r < ROWS; r++) if (s_rowDirty[r]) { dirty = true; break; }
        // only auto-render the live view; while scrolled, render only on scroll
        if (dirty && (live || s_allDirty) && now - lastRender >= 30) { termRender(); lastRender = now; }

        if (n <= 0 && e <= 0 && !k && tb == TBALL_NONE) vTaskDelay(pdMS_TO_TICKS(8));
    }

    snprintf(s_status, sizeof(s_status), "Disconnected");
    ssh_channel_send_eof(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);
    ssh_finalize();
    s_taskDone = true;
    vTaskDelete(NULL);
}

// ── Small line input (masked for password) ────────────────────────────────────
static bool readLine(const char* prompt, char* out, int maxLen, bool mask) {
    displayManager.clearScreen();
    displayManager.setDefaultTextSize();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(TFT_CYAN);
    displayManager.println(prompt);

    int len = 0; out[0] = '\0';
    int y = outputY + LINE_HEIGHT * 2;
    while (true) {
        tft.fillRect(4, y, SCREEN_WIDTH - 8, LINE_HEIGHT, TFT_BLACK);
        tft.setCursor(4, y);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        if (mask) for (int i = 0; i < len; i++) tft.print('*');
        else tft.print(out);
        tft.print('_');

        char k = 0;
        while (!(k = inputHandler.getKeyboardInput())) vTaskDelay(pdMS_TO_TICKS(5));
        if (k == '\r' || k == '\n') return len > 0;
        if (k == 0x1B) return false;
        if (k == '\b') { if (len > 0) out[--len] = '\0'; continue; }
        if (k >= 32 && len < maxLen - 1) { out[len++] = k; out[len] = '\0'; }
    }
}

// ── Host profiles  (/apps/ssh/hosts.csv — name,ip,port,user; NO password) ─────
#define SSH_HOSTS_MAX 24
struct HostProfile { char name[24]; char ip[64]; int port; char user[40]; };

static int hostLoadAll(HostProfile* arr, int max) {
    if (!sdCardManager.canAccessSD()) return 0;
    File f = SD.open(SD_SSH_HOSTS, FILE_READ);
    if (!f) return 0;
    int n = 0;
    while (f.available() && n < max) {
        String line = f.readStringUntil('\n'); line.trim();
        if (!line.length() || line[0] == '#') continue;
        char buf[180]; line.toCharArray(buf, sizeof(buf));
        char* nm = strtok(buf, ","); char* ip = strtok(nullptr, ",");
        char* pt = strtok(nullptr, ","); char* us = strtok(nullptr, ",");
        if (!nm || !ip) continue;
        HostProfile& h = arr[n];
        strncpy(h.name, nm, sizeof(h.name) - 1); h.name[sizeof(h.name) - 1] = '\0';
        strncpy(h.ip, ip, sizeof(h.ip) - 1);     h.ip[sizeof(h.ip) - 1] = '\0';
        h.port = pt ? atoi(pt) : 22; if (h.port <= 0) h.port = 22;
        if (us) { strncpy(h.user, us, sizeof(h.user) - 1); h.user[sizeof(h.user) - 1] = '\0'; }
        else h.user[0] = '\0';
        n++;
    }
    f.close();
    return n;
}

static bool hostFind(const char* name, HostProfile* out) {
    HostProfile arr[SSH_HOSTS_MAX];
    int n = hostLoadAll(arr, SSH_HOSTS_MAX);
    for (int i = 0; i < n; i++)
        if (strcasecmp(arr[i].name, name) == 0) { *out = arr[i]; return true; }
    return false;
}

static bool hostWriteAll(HostProfile* arr, int n) {
    if (!sdCardManager.canAccessSD()) return false;
    sdCardManager.ensureDir(SD_DIR_SSH);
    File f = SD.open(SD_SSH_HOSTS, FILE_WRITE);   // "w" on ESP32 → truncates
    if (!f) return false;
    f.println("# T-Rex SSH host profiles  —  name,ip,port,user  (no passwords)");
    for (int i = 0; i < n; i++)
        f.printf("%s,%s,%d,%s\n", arr[i].name, arr[i].ip, arr[i].port, arr[i].user);
    f.close();
    return true;
}

static void hostSave(const char* name, const char* ip, const char* user, int port) {
    HostProfile arr[SSH_HOSTS_MAX];
    int n = hostLoadAll(arr, SSH_HOSTS_MAX);
    int slot = -1;
    for (int i = 0; i < n; i++) if (strcasecmp(arr[i].name, name) == 0) { slot = i; break; }
    if (slot < 0) { if (n >= SSH_HOSTS_MAX) { displayManager.println("Host list full"); return; } slot = n++; }
    HostProfile& h = arr[slot];
    strncpy(h.name, name, sizeof(h.name) - 1); h.name[sizeof(h.name) - 1] = '\0';
    strncpy(h.ip, ip, sizeof(h.ip) - 1);       h.ip[sizeof(h.ip) - 1] = '\0';
    strncpy(h.user, user ? user : "", sizeof(h.user) - 1); h.user[sizeof(h.user) - 1] = '\0';
    h.port = port > 0 ? port : 22;
    displayManager.println(hostWriteAll(arr, n) ? "Profile saved" : "Save failed (no SD?)");
}

static void hostRemove(const char* name) {
    HostProfile arr[SSH_HOSTS_MAX];
    int n = hostLoadAll(arr, SSH_HOSTS_MAX), w = 0; bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(arr[i].name, name) == 0) { found = true; continue; }
        if (w != i) arr[w] = arr[i];
        w++;
    }
    if (!found) { displayManager.println("No such profile"); return; }
    displayManager.println(hostWriteAll(arr, w) ? "Profile removed" : "Remove failed");
}

static void hostList() {
    HostProfile arr[SSH_HOSTS_MAX];
    int n = hostLoadAll(arr, SSH_HOSTS_MAX);
    if (n == 0) { displayManager.println("No saved hosts. Add: ssh save <name> <ip> [user] [port]"); return; }
    char line[96];
    for (int i = 0; i < n; i++) {
        snprintf(line, sizeof(line), "%-12s %s@%s:%d",
                 arr[i].name, arr[i].user[0] ? arr[i].user : "?", arr[i].ip, arr[i].port);
        displayManager.println(line);
    }
}

// ── Command entry point ───────────────────────────────────────────────────────
void runSshCon(char* args) {
    // ── subcommands (work without WiFi) ──
    if (args && *args) {
        char sub[160]; strncpy(sub, args, sizeof(sub) - 1); sub[sizeof(sub) - 1] = '\0';
        char* cmd = strtok(sub, " ");
        if (cmd && strcasecmp(cmd, "list") == 0) {
            hostList(); displayManager.printCommandScreen(); return;
        }
        if (cmd && strcasecmp(cmd, "rm") == 0) {
            char* nm = strtok(nullptr, " ");
            if (nm) hostRemove(nm); else displayManager.println("Usage: ssh rm <name>");
            displayManager.printCommandScreen(); return;
        }
        if (cmd && strcasecmp(cmd, "save") == 0) {
            char* nm = strtok(nullptr, " "); char* ip = strtok(nullptr, " ");
            char* us = strtok(nullptr, " "); char* pt = strtok(nullptr, " ");
            if (!nm || !ip) displayManager.println("Usage: ssh save <name> <ip> [user] [port]");
            else hostSave(nm, ip, us, pt ? atoi(pt) : 22);
            displayManager.printCommandScreen(); return;
        }
    }

    // ── connect (needs WiFi) ──
    if (WiFi.status() != WL_CONNECTED) {
        displayManager.println("Not connected. Run 'cw <index>' first.");
        displayManager.printCommandScreen();
        return;
    }
    if (!args || !*args) {
        displayManager.println("Usage: ssh <ip|name> [user]   (also: save/list/rm)");
        displayManager.printCommandScreen();
        return;
    }

    char a[96]; strncpy(a, args, sizeof(a) - 1); a[sizeof(a) - 1] = '\0';
    char* host = strtok(a, " ");
    char* user = strtok(nullptr, " ");
    if (!host) { displayManager.println("Usage: ssh <ip|name> [user]"); displayManager.printCommandScreen(); return; }

    // Resolve: saved profile name takes priority; else treat token as ip/hostname.
    s_p.port = 22;
    HostProfile prof;
    const char* profUser = nullptr;
    if (hostFind(host, &prof)) {
        strncpy(s_p.host, prof.ip, sizeof(s_p.host) - 1); s_p.host[sizeof(s_p.host) - 1] = '\0';
        s_p.port = prof.port;
        if (prof.user[0]) profUser = prof.user;
    } else {
        strncpy(s_p.host, host, sizeof(s_p.host) - 1); s_p.host[sizeof(s_p.host) - 1] = '\0';
    }

    // user: explicit arg > profile > prompt
    if (user)          { strncpy(s_p.user, user, sizeof(s_p.user) - 1); s_p.user[sizeof(s_p.user) - 1] = '\0'; }
    else if (profUser) { strncpy(s_p.user, profUser, sizeof(s_p.user) - 1); s_p.user[sizeof(s_p.user) - 1] = '\0'; }
    else if (!readLine("SSH username:", s_p.user, sizeof(s_p.user), false)) { displayManager.printCommandScreen(); return; }

    if (!readLine("SSH password:", s_p.pass, sizeof(s_p.pass), true)) { displayManager.printCommandScreen(); return; }

    displayManager.clearScreen();
    s_taskDone = false; s_status[0] = '\0';
    if (xTaskCreatePinnedToCore(sshTask, "ssh", 51200, nullptr, tskIDLE_PRIORITY + 3, nullptr, 1) != pdPASS) {
        displayManager.println("SSH task create failed (low memory)");
        displayManager.printCommandScreen();
        return;
    }
    while (!s_taskDone) vTaskDelay(pdMS_TO_TICKS(50));

    memset(s_p.pass, 0, sizeof(s_p.pass));   // wipe password from RAM

    displayManager.clearScreen();
    displayManager.setCursor(4, outputY);
    displayManager.setTextColor(s_status[0] ? TFT_YELLOW : TFT_WHITE);
    displayManager.println(s_status[0] ? s_status : "SSH session ended");
    displayManager.printCommandScreen();
}
