// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// edit / ed — on-device nano-style text editor for SD files.
//
// The T-Deck I2C keyboard delivers a single resolved byte per key (Alt/Shift
// handled on the keyboard's own MCU) — no Ctrl/Esc/arrow codes reach us. So the
// control scheme adapts nano to this hardware: the keyboard types text, the
// trackball moves the cursor (U/D/L/R, wrapping across line ends), and a CLICK
// opens the command menu (Save / Save As / Find / Go to line / Cut / Paste /
// Exit). Exit with unsaved changes prompts to save — nano's safety net.
//
// Rendering reuses the 6px-wide LGFX Font0 grid (same as the ssh terminal):
// ~52 columns x 12 visible rows, inverse-colour block cursor, lock-screen aware.

#include "text_editor.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "layout.h"          // layoutCharCols — board-adaptive text-grid width

extern LGFX            tft;
extern DisplayManager  displayManager;
extern InputHandling   inputHandler;
extern SDCardManager   sdCardManager;

// ── Geometry (mirrors the ssh terminal's Font0 grid) ──────────────────────────
#define ED_TITLE_Y   outputY
#define ED_TEXT_Y    (outputY + LINE_HEIGHT)
#define ED_HINT_Y    (SCREEN_HEIGHT - LINE_HEIGHT)
#define ED_ROWS      ((ED_HINT_Y - ED_TEXT_Y) / LINE_HEIGHT)   // visible text rows
// Text-grid width self-adapts to the board's screen.
//   T-Deck  (320): 52   (byte-identical to the old hardcoded 52)
//   T-Pager (480): 78   (+26 chars/line — was ~35% wasted right-side gap)
constexpr int ED_COLS = layoutCharCols();
#define ED_CHAR_W    6
#define ED_CELL_X(c) (2 + (c) * ED_CHAR_W)
#define ED_SBAR_X    315
#define ED_LOAD_CAP  500    // lines loaded from file; longer => read-only
#define ED_MAXLINES  600    // hard cap on the live buffer
#define ED_MAXLINE   1024   // hard cap on a single line's length

// ── Editor state (single-instance; freed on exit per the memory-discipline rule)
static std::vector<String> g_lines;
static int      g_curRow, g_curCol;        // cursor in buffer coords
static int      g_scrollRow, g_scrollCol;  // top-left visible cell
static bool     g_modified, g_readOnly;
static char     g_path[128];
static char     g_fdisp[24];
static String   g_clip;
static char     g_status[40];
static uint32_t g_statusMs;

// Per-row dirty rendering — only redraw rows that changed (kills flicker).
static bool     g_allDirty;
static bool     g_rowDirty[ED_ROWS];
static bool     g_hintDirty;

// Single-level undo: one full-buffer snapshot, coalesced per typing/delete run.
static std::vector<String> g_undo;
static int      g_undoRow, g_undoCol;
static bool     g_undoValid;
enum { ACT_NONE, ACT_TYPE, ACT_DEL };
static int      g_lastAction;

// Trackball acceleration — fast rolls jump multiple cells/lines.
static int      g_accelDir;
static uint32_t g_accelLast;
static int      g_accelStep;

enum { A_SAVE, A_SAVEAS, A_FIND, A_GOTO, A_TOP, A_BOTTOM, A_UNDO, A_CUT, A_PASTE, A_EXIT, A_COUNT };
static const char* MENU[A_COUNT] = {
    "Save", "Save As", "Find", "Go to line", "Top", "Bottom", "Undo", "Cut line", "Paste line", "Exit"
};

static inline bool blocked() { return displayManager.isBlocked(); }
static inline bool unlocked() { return LockScreenManager::getInstance().consumeJustUnlocked(); }

static void setStatus(const char* s) {
    strncpy(g_status, s, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
    g_statusMs = millis();
}

static void updateFdisp() {
    const char* fn = strrchr(g_path, '/');
    fn = fn ? fn + 1 : g_path;
    if (strlen(fn) > 20) { strncpy(g_fdisp, fn, 17); g_fdisp[17] = '\0'; strcat(g_fdisp, "..."); }
    else { strncpy(g_fdisp, fn, sizeof(g_fdisp) - 1); g_fdisp[sizeof(g_fdisp) - 1] = '\0'; }
}

// ── Dirty tracking ────────────────────────────────────────────────────────────
static void markAll() { g_allDirty = true; }
static void markBufRow(int bufRow) {
    int sr = bufRow - g_scrollRow;
    if (sr >= 0 && sr < ED_ROWS) g_rowDirty[sr] = true;
}

// ── Single-level undo ─────────────────────────────────────────────────────────
static void snapshot() {
    g_undo = g_lines;                       // O(n) copy — only at run boundaries
    g_undoRow = g_curRow; g_undoCol = g_curCol;
    g_undoValid = true;
}

// ── Trackball acceleration ────────────────────────────────────────────────────
// Consecutive same-direction moves <90ms apart double the step (cap 16); a pause
// or direction change resets to single-step. Lets a quick roll page big files.
static int accelStep(int dir) {
    uint32_t now = millis();
    if (dir == g_accelDir && now - g_accelLast < 90) g_accelStep = min(16, g_accelStep * 2);
    else                                             g_accelStep = 1;
    g_accelDir = dir; g_accelLast = now;
    return g_accelStep;
}

// ── Drawing ───────────────────────────────────────────────────────────────────
static void drawTitle() {
    tft.setTextSize(1.0, 1.0);
    tft.fillRect(0, ED_TITLE_Y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    tft.setCursor(4, ED_TITLE_Y + 2);
    tft.setTextColor(0x7BEF, TFT_BLACK);   tft.print("[");
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.print("EDIT");
    tft.setTextColor(0x7BEF, TFT_BLACK);   tft.print("] ");
    tft.setTextColor(g_modified ? TFT_YELLOW : TFT_WHITE, TFT_BLACK); tft.print(g_fdisp);
    if (g_modified) { tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.print("*"); }
    if (g_readOnly) { tft.setTextColor(TFT_RED, TFT_BLACK);    tft.print(" RO"); }
    char meta[24];
    snprintf(meta, sizeof(meta), "  %dL %d:%d", (int)g_lines.size(), g_curRow + 1, g_curCol + 1);
    tft.setTextColor(0x7BEF, TFT_BLACK); tft.print(meta);
    tft.fillRect(0, ED_TITLE_Y + LINE_HEIGHT - 1, SCREEN_WIDTH, 1, 0x7BEF);
}

static void drawTextRow(int bufRow, int y) {
    tft.fillRect(0, y, ED_SBAR_X, LINE_HEIGHT, TFT_BLACK);
    const String& s = g_lines[bufRow];
    int len = s.length();
    bool curHere = (bufRow == g_curRow);
    for (int c = 0; c < ED_COLS; c++) {
        int idx = g_scrollCol + c;
        int x   = ED_CELL_X(c);
        char ch = (idx < len) ? s[idx] : ' ';
        if ((uint8_t)ch < 0x20 || (uint8_t)ch >= 0x7f) ch = ' ';  // hide control bytes
        bool cur = curHere && (idx == g_curCol);
        if (cur) {
            tft.fillRect(x, y, ED_CHAR_W, LINE_HEIGHT, TFT_CYAN);
            if (ch != ' ') { tft.setCursor(x, y + 2); tft.setTextColor(TFT_BLACK, TFT_CYAN); tft.print(ch); }
        } else if (ch != ' ') {
            tft.setCursor(x, y + 2); tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.print(ch);
        }
    }
}

static void drawScrollbar() {
    int total = (int)g_lines.size();
    int areaH = ED_ROWS * LINE_HEIGHT;
    if (total <= ED_ROWS) { tft.fillRect(ED_SBAR_X, ED_TEXT_Y, 3, areaH, TFT_BLACK); return; }
    tft.fillRect(ED_SBAR_X, ED_TEXT_Y, 3, areaH, 0x2104);
    int thumbH = max(6, areaH * ED_ROWS / total);
    int maxTop = total - ED_ROWS;
    int thumbY = ED_TEXT_Y + (maxTop > 0 ? (areaH - thumbH) * g_scrollRow / maxTop : 0);
    tft.fillRect(ED_SBAR_X, thumbY, 3, thumbH, TFT_CYAN);
}

static void drawHint() {
    tft.setTextSize(1.0, 1.0);
    tft.fillRect(0, ED_HINT_Y, SCREEN_WIDTH, LINE_HEIGHT, TFT_BLACK);
    tft.fillRect(0, ED_HINT_Y - 1, SCREEN_WIDTH, 1, 0x7BEF);
    tft.setCursor(4, ED_HINT_Y + 2);
    if (g_status[0]) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.print(g_status);
    } else {
        tft.setTextColor(0x7BEF, TFT_BLACK);   tft.print("tpad=move  ");
        tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.print("click");
        tft.setTextColor(0x7BEF, TFT_BLACK);   tft.print("=menu");
    }
}

static bool anyDirty() {
    if (g_allDirty || g_hintDirty) return true;
    for (int r = 0; r < ED_ROWS; r++) if (g_rowDirty[r]) return true;
    return false;
}

// Redraw only what changed. Title always refreshes (it shows live row:col), but
// it's a single row so it's cheap; text rows redraw per the dirty array.
static void flushDraw() {
    tft.setTextSize(1.0, 1.0);
    drawTitle();
    for (int r = 0; r < ED_ROWS; r++) {
        if (!g_allDirty && !g_rowDirty[r]) continue;
        int br = g_scrollRow + r;
        int y  = ED_TEXT_Y + r * LINE_HEIGHT;
        if (br < (int)g_lines.size()) drawTextRow(br, y);
        else                          tft.fillRect(0, y, ED_SBAR_X, LINE_HEIGHT, TFT_BLACK);
        g_rowDirty[r] = false;
    }
    if (g_allDirty)                 drawScrollbar();
    if (g_allDirty || g_hintDirty) { drawHint(); g_hintDirty = false; }
    g_allDirty = false;
}

static void adjustScroll() {
    if (g_curRow < g_scrollRow)               g_scrollRow = g_curRow;
    if (g_curRow >= g_scrollRow + ED_ROWS)    g_scrollRow = g_curRow - ED_ROWS + 1;
    if (g_curCol < g_scrollCol)               g_scrollCol = g_curCol;
    if (g_curCol >= g_scrollCol + ED_COLS)    g_scrollCol = g_curCol - ED_COLS + 1;
    if (g_scrollRow < 0) g_scrollRow = 0;
    if (g_scrollCol < 0) g_scrollCol = 0;
}

// ── File I/O ────────────────────────────────────────────────────────────────
// Create every parent directory of g_path so new files can be saved into new
// folders (ensureDir is single-level, so we walk each prefix). Idempotent.
static void ensureParentDirs(const char* filePath) {
    char dir[128];
    strncpy(dir, filePath, sizeof(dir) - 1); dir[sizeof(dir) - 1] = '\0';
    char* slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;     // file at root — nothing to create
    *slash = '\0';                          // strip filename → parent path
    for (char* p = dir + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; sdCardManager.ensureDir(dir); *p = '/'; }
    }
    sdCardManager.ensureDir(dir);
}

static bool saveBuffer() {
    if (!sdCardManager.canAccessSD()) return false;
    ensureParentDirs(g_path);
    File f = SD.open(g_path, FILE_WRITE);   // "w" — truncates on ESP32
    if (!f) return false;
    for (size_t i = 0; i < g_lines.size(); i++) { f.print(g_lines[i]); f.print('\n'); }
    f.close();
    g_modified = false;
    return true;
}

// Returns false on a hard error (directory). Missing file => new empty buffer.
static bool loadBuffer() {
    g_lines.clear();
    g_curRow = g_curCol = g_scrollRow = g_scrollCol = 0;
    g_modified = g_readOnly = false;

    File f = SD.open(g_path);
    if (f && f.isDirectory()) { f.close(); return false; }
    if (!f) { g_lines.push_back(""); setStatus("New file"); return true; }

    while (f.available() && (int)g_lines.size() < ED_LOAD_CAP) {
        String s = f.readStringUntil('\n');
        if (s.length() > 0 && s[s.length() - 1] == '\r') s.remove(s.length() - 1);
        g_lines.push_back(s);
    }
    if (f.available()) { g_readOnly = true; setStatus("Read-only: file too large"); }
    f.close();
    if (g_lines.empty()) g_lines.push_back("");
    return true;
}

// ── Blocking sub-screens (menu / prompts) — all lock-screen aware ─────────────
static void drawMenu(int sel) {
    tft.setTextSize(1.0, 1.0);
    int bw = 150, bh = (A_COUNT + 2) * LINE_HEIGHT;
    int bx = (SCREEN_WIDTH - bw) / 2, by = (SCREEN_HEIGHT - bh) / 2;
    tft.fillRect(bx, by, bw, bh, TFT_BLACK);
    tft.drawRect(bx, by, bw, bh, TFT_CYAN);
    tft.setCursor(bx + 8, by + 4);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.print("MENU  (any key=cancel)");
    for (int i = 0; i < A_COUNT; i++) {
        int y = by + LINE_HEIGHT * (i + 1) + 4;
        if (i == sel) { tft.fillRect(bx + 2, y - 1, bw - 4, LINE_HEIGHT, TFT_CYAN);
                        tft.setTextColor(TFT_BLACK, TFT_CYAN); }
        else          { tft.setTextColor(TFT_WHITE, TFT_BLACK); }
        tft.setCursor(bx + 10, y); tft.print(MENU[i]);
    }
}

// Returns the chosen action index, or -1 if cancelled.
static int runMenu() {
    int sel = 0; bool redraw = true;
    for (;;) {
        if (unlocked()) redraw = true;
        if (redraw && !blocked()) { drawMenu(sel); redraw = false; }
        char k = inputHandler.getKeyboardInput();
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (blocked()) continue;
        if (e == TBALL_UP)    { sel = (sel - 1 + A_COUNT) % A_COUNT; redraw = true; }
        else if (e == TBALL_DOWN)  { sel = (sel + 1) % A_COUNT; redraw = true; }
        else if (e == TBALL_CLICK) { return sel; }
        else if (k != 0)           { return -1; }   // any key cancels
    }
}

static void drawPrompt(const char* label, const char* buf) {
    tft.setTextSize(1.0, 1.0);
    int y = ED_HINT_Y;
    tft.fillRect(0, y - LINE_HEIGHT, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
    tft.fillRect(0, y - LINE_HEIGHT - 1, SCREEN_WIDTH, 1, TFT_CYAN);
    tft.setCursor(4, y - LINE_HEIGHT + 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.print(label);
    tft.setCursor(4, y + 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    // show the tail if it overflows the row
    int len = strlen(buf);
    const char* show = buf;
    if (len > ED_COLS - 1) show = buf + (len - (ED_COLS - 1));
    tft.print(show);
    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.print('_');
}

// Line input: Enter confirms (true), CLICK cancels (false).
static bool promptLine(const char* label, char* out, size_t cap, const char* initial) {
    strncpy(out, initial ? initial : "", cap - 1);
    out[cap - 1] = '\0';
    int len = strlen(out);
    bool redraw = true;
    for (;;) {
        if (unlocked()) redraw = true;
        if (redraw && !blocked()) { drawPrompt(label, out); redraw = false; }
        char k = inputHandler.getKeyboardInput();
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (blocked()) continue;
        if (e == TBALL_CLICK)              return false;
        if (k == '\r' || k == '\n')        return true;
        if (k == '\b') { if (len > 0) { out[--len] = '\0'; redraw = true; } }
        else if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7f && len < (int)cap - 1) {
            out[len++] = k; out[len] = '\0'; redraw = true;
        }
    }
}

// 0 = cancel/stay, 1 = save & exit, 2 = discard & exit.
static int confirmSaveExit() {
    bool redraw = true;
    for (;;) {
        if (unlocked()) redraw = true;
        if (redraw && !blocked()) {
            int y = ED_HINT_Y;
            tft.setTextSize(1.0, 1.0);
            tft.fillRect(0, y - LINE_HEIGHT, SCREEN_WIDTH, LINE_HEIGHT * 2, TFT_BLACK);
            tft.fillRect(0, y - LINE_HEIGHT - 1, SCREEN_WIDTH, 1, TFT_YELLOW);
            tft.setCursor(4, y - LINE_HEIGHT + 2);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.print("Unsaved changes");
            tft.setCursor(4, y + 2);
            tft.setTextColor(0x7BEF, TFT_BLACK);   tft.print("[s]save&exit [d]discard ");
            tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.print("click=cancel");
            redraw = false;
        }
        char k = inputHandler.getKeyboardInput();
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (blocked()) continue;
        if (e == TBALL_CLICK)        return 0;
        if (k == 's' || k == 'S')    return 1;
        if (k == 'd' || k == 'D')    return 2;
    }
}

// ── Menu action handlers ──────────────────────────────────────────────────────
static const char* saveFailReason() {
    return sdCardManager.canAccessSD() ? "Save failed — bad path/dir?" : "Save failed (no SD)";
}

static void doSave() {
    if (g_readOnly)        { setStatus("Read-only — cannot save"); return; }
    if (saveBuffer())      { char m[40]; snprintf(m, sizeof(m), "Saved %d lines", (int)g_lines.size()); setStatus(m); }
    else                     setStatus(saveFailReason());
}

static void doSaveAs() {
    char np[128];
    if (!promptLine("Save as:", np, sizeof(np), g_path) || np[0] == '\0') return;
    char resolved[128];
    sdCardManager.resolvePath(np, resolved, sizeof(resolved));
    strncpy(g_path, resolved, sizeof(g_path) - 1); g_path[sizeof(g_path) - 1] = '\0';
    updateFdisp();
    g_readOnly = false;
    setStatus(saveBuffer() ? "Saved" : saveFailReason());
}

static void doFind() {
    char term[48];
    if (!promptLine("Find:", term, sizeof(term), "") || term[0] == '\0') return;
    bool found = false;
    for (int r = g_curRow; r < (int)g_lines.size() && !found; r++) {
        int from = (r == g_curRow) ? g_curCol + 1 : 0;
        int idx  = g_lines[r].indexOf(term, from);
        if (idx >= 0) { g_curRow = r; g_curCol = idx; found = true; }
    }
    for (int r = 0; r <= g_curRow && !found; r++) {       // wrap to top
        int idx = g_lines[r].indexOf(term);
        if (idx >= 0) { g_curRow = r; g_curCol = idx; found = true; }
    }
    setStatus(found ? "Found" : "Not found");
}

static void doGoto() {
    char num[12];
    if (!promptLine("Go to line:", num, sizeof(num), "") || num[0] == '\0') return;
    int ln = atoi(num);
    if (ln < 1) ln = 1;
    if (ln > (int)g_lines.size()) ln = (int)g_lines.size();
    g_curRow = ln - 1; g_curCol = 0;
}

static void doCut() {
    if (g_readOnly) { setStatus("Read-only"); return; }
    snapshot(); g_lastAction = ACT_NONE;
    g_clip = g_lines[g_curRow];
    if (g_lines.size() > 1) g_lines.erase(g_lines.begin() + g_curRow);
    else                    g_lines[0] = "";
    if (g_curRow >= (int)g_lines.size()) g_curRow = (int)g_lines.size() - 1;
    g_curCol = 0; g_modified = true; setStatus("Cut line");
}

static void doPaste() {
    if (g_readOnly) { setStatus("Read-only"); return; }
    if ((int)g_lines.size() >= ED_MAXLINES) { setStatus("Max lines reached"); return; }
    snapshot(); g_lastAction = ACT_NONE;
    g_lines.insert(g_lines.begin() + g_curRow + 1, g_clip);
    g_curRow++; g_curCol = 0; g_modified = true; setStatus("Pasted line");
}

static void doUndo() {
    if (!g_undoValid) { setStatus("Nothing to undo"); return; }
    g_lines = g_undo;
    g_curRow = g_undoRow; g_curCol = g_undoCol;
    if (g_curRow >= (int)g_lines.size()) g_curRow = (int)g_lines.size() - 1;
    if (g_curRow < 0) g_curRow = 0;
    if (g_curCol > (int)g_lines[g_curRow].length()) g_curCol = g_lines[g_curRow].length();
    g_undoValid = false; g_lastAction = ACT_NONE; g_modified = true;
    setStatus("Undone");
}

static void doTop()    { g_curRow = 0; g_curCol = 0; }
static void doBottom() { g_curRow = (int)g_lines.size() - 1; g_curCol = 0; }

// ── Cursor movement (trackball) ───────────────────────────────────────────────
static void moveLeft() {
    if (g_curCol > 0) g_curCol--;
    else if (g_curRow > 0) { g_curRow--; g_curCol = g_lines[g_curRow].length(); }
}
static void moveRight() {
    if (g_curCol < (int)g_lines[g_curRow].length()) g_curCol++;
    else if (g_curRow < (int)g_lines.size() - 1) { g_curRow++; g_curCol = 0; }
}
static void moveUp() {
    if (g_curRow > 0) { g_curRow--; g_curCol = min(g_curCol, (int)g_lines[g_curRow].length()); }
}
static void moveDown() {
    if (g_curRow < (int)g_lines.size() - 1) { g_curRow++; g_curCol = min(g_curCol, (int)g_lines[g_curRow].length()); }
}

// ── Text editing (keyboard) ───────────────────────────────────────────────────
static void editKey(char k) {
    if (g_readOnly) { setStatus("Read-only"); return; }
    if (k == '\r' || k == '\n') {
        if ((int)g_lines.size() >= ED_MAXLINES) { setStatus("Max lines reached"); return; }
        snapshot(); g_lastAction = ACT_NONE;                 // structural: always snapshot
        // Auto-indent: the new line inherits the current line's leading whitespace
        // (skipped when the cursor sits inside that indent, to avoid doubling it).
        const String& cur = g_lines[g_curRow];
        int ind = 0;
        while (ind < (int)cur.length() && (cur[ind] == ' ' || cur[ind] == '\t')) ind++;
        String indent = (g_curCol >= ind) ? cur.substring(0, ind) : String("");
        String tail   = cur.substring(g_curCol);
        g_lines[g_curRow] = cur.substring(0, g_curCol);
        g_lines.insert(g_lines.begin() + g_curRow + 1, indent + tail);
        g_curRow++; g_curCol = indent.length(); g_modified = true;
    } else if (k == '\b') {
        if (g_lastAction != ACT_DEL) { snapshot(); g_lastAction = ACT_DEL; }
        if (g_curCol > 0) { g_lines[g_curRow].remove(g_curCol - 1, 1); g_curCol--; g_modified = true; }
        else if (g_curRow > 0) {
            int pl = g_lines[g_curRow - 1].length();
            g_lines[g_curRow - 1] += g_lines[g_curRow];
            g_lines.erase(g_lines.begin() + g_curRow);
            g_curRow--; g_curCol = pl; g_modified = true;
        }
    } else if ((uint8_t)k >= 0x20 && (uint8_t)k < 0x7f) {
        String& ln = g_lines[g_curRow];
        if ((int)ln.length() >= ED_MAXLINE) { setStatus("Line too long"); return; }
        if (g_lastAction != ACT_TYPE) { snapshot(); g_lastAction = ACT_TYPE; }
        ln = ln.substring(0, g_curCol) + k + ln.substring(g_curCol);
        g_curCol++; g_modified = true;
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────
static void cleanup() {
    g_lines.clear(); g_lines.shrink_to_fit();
    g_undo.clear();  g_undo.shrink_to_fit();
    g_clip = "";
}

void runEditor(char* args) {
    if (!args || !*args) {
        displayManager.println("Usage: edit <path>");
        displayManager.printCommandScreen();
        return;
    }
    if (!sdCardManager.canAccessSD()) {
        displayManager.println("No SD card mounted.");
        displayManager.printCommandScreen();
        return;
    }

    char resolved[128];
    sdCardManager.resolvePath(args, resolved, sizeof(resolved));
    strncpy(g_path, resolved, sizeof(g_path) - 1); g_path[sizeof(g_path) - 1] = '\0';
    updateFdisp();
    g_clip = ""; g_status[0] = '\0';
    g_undoValid = false; g_lastAction = ACT_NONE;
    g_accelDir = TBALL_NONE; g_accelStep = 1; g_accelLast = 0;
    for (int r = 0; r < ED_ROWS; r++) g_rowDirty[r] = false;

    if (!loadBuffer()) {
        cleanup();
        displayManager.printText("Is a directory: ");
        displayManager.println(g_path);
        displayManager.printCommandScreen();
        return;
    }

    displayManager.clearScreen();
    markAll(); g_hintDirty = true;     // initial full draw

    for (;;) {
        if (unlocked()) { markAll(); g_hintDirty = true; }
        if (!blocked() && anyDirty()) flushDraw();
        if (g_status[0] && millis() - g_statusMs > 2500) { g_status[0] = '\0'; g_hintDirty = true; }

        char k = inputHandler.getKeyboardInput();
        TrackballEvent e = inputHandler.getTrackballEvent();
        if (blocked()) continue;

        // CLICK → command menu (handled separately; always full-redraws on return)
        if (e == TBALL_CLICK) {
            int act = runMenu();
            if (act == A_EXIT) {
                if (g_modified && !g_readOnly) {
                    int r = confirmSaveExit();
                    if (r == 1 && !saveBuffer()) { setStatus(saveFailReason()); markAll(); g_hintDirty = true; continue; }
                    if (r != 0) { cleanup(); displayManager.clearScreen(); displayManager.printCommandScreen(); return; }
                    // r == 0 → cancel, fall through to redraw
                } else {
                    cleanup(); displayManager.clearScreen(); displayManager.printCommandScreen(); return;
                }
            } else {
                switch (act) {
                    case A_SAVE:   doSave();   break;
                    case A_SAVEAS: doSaveAs(); break;
                    case A_FIND:   doFind();   break;
                    case A_GOTO:   doGoto();   break;
                    case A_TOP:    doTop();    break;
                    case A_BOTTOM: doBottom(); break;
                    case A_UNDO:   doUndo();   break;
                    case A_CUT:    doCut();    break;
                    case A_PASTE:  doPaste();  break;
                    default: break;            // -1 cancel
                }
            }
            adjustScroll();
            markAll(); g_hintDirty = true;
            continue;
        }

        int oldRow = g_curRow, oldSR = g_scrollRow, oldSC = g_scrollCol;
        size_t oldCnt = g_lines.size();
        bool act = false;

        switch (e) {
            case TBALL_LEFT:  { int n = accelStep(e); while (n--) moveLeft();  g_lastAction = ACT_NONE; act = true; break; }
            case TBALL_RIGHT: { int n = accelStep(e); while (n--) moveRight(); g_lastAction = ACT_NONE; act = true; break; }
            case TBALL_UP:    { int n = accelStep(e); while (n--) moveUp();    g_lastAction = ACT_NONE; act = true; break; }
            case TBALL_DOWN:  { int n = accelStep(e); while (n--) moveDown();  g_lastAction = ACT_NONE; act = true; break; }
            default: break;
        }

        if (k != 0) { editKey(k); act = true; }

        if (act) {
            adjustScroll();
            if (oldSR != g_scrollRow || oldSC != g_scrollCol || oldCnt != g_lines.size()) markAll();
            else { markBufRow(oldRow); markBufRow(g_curRow); }
        }
    }
}
