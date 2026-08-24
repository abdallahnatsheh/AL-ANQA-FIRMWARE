// AL-ANQA — offensive security firmware for LilyGo T-Deck
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// crack / cc — offline WPA/WPA2 .cap cracker. See capcrack.h.

#include "capcrack.h"
#include "display_manager.h"
#include "input_handling.h"
#include "sdcard_manager.h"
#include "lockscreen_manager.h"
#include "dot11.h"          // 802.11 parse (SSID IE, EAPOL)
#include "pcap_writer.h"    // pcap read helpers
#include "cap_parse.h"      // shared .cap → CrackJob parser (also used by pwn)
#include "wpa_crack.h"      // PBKDF2 / handshake-MIC / PMKID dictionary crack
#include "crack_progress.h" // shared per-cap wordlist resume cursor (also used by pwn)
#include "path_prompt.h"    // interactive path input with '-key autocomplete
#include "capcrack_bg.h"    // cc bg — cooperative background crack (runs under the cover)
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <string.h>
#include <strings.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// crack material now lives in the shared parser (rule 5b — also used by `pwn`).
using CrackJob = capparse::CrackJob;

// ── small UI helpers ─────────────────────────────────────────────────────────
static void header(const char* noun) {
    DisplayManager& dm = displayManager;
    dm.clearScreen();
    dm.setDefaultTextSize();
    dm.setCursor(4, outputY);
    dm.setTextColor(0x7BEF);     dm.printText("[");
    dm.setTextColor(TFT_CYAN);   dm.printText("CRACK");
    dm.setTextColor(0x7BEF);     dm.printText("::");
    dm.setTextColor(TFT_YELLOW); dm.printText(noun);
    dm.setTextColor(0x7BEF);     dm.println("]");
    dm.printSeparator();
}

static void msgScreen(const char* line1, uint16_t c1, const char* line2) {
    DisplayManager& dm = displayManager;
    header("INFO");
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(c1); dm.println(line1);
    if (line2 && *line2) {
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE); dm.println(line2);
    }
    dm.printCommandScreen();
}

// Like msgScreen but waits for a key and RETURNS to the caller (stays in-flow),
// instead of dropping to the CLI — for errors shown inside a picker back-loop.
static void notifyWait(const char* line1, uint16_t c1, const char* line2) {
    DisplayManager& dm = displayManager;
    header("INFO");
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(c1); dm.println(line1);
    if (line2 && *line2) { dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE); dm.println(line2); }
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.println("any key...");
    while (inputHandler.getKeyboardInput() == 0) vTaskDelay(pdMS_TO_TICKS(15));
}

// Paginated list picker. `sub` (optional) is shown under the header — used to make
// the directory being listed explicit. Returns the chosen index, or -1 on cancel/empty.
static int pickList(const char* noun, const std::vector<String>& items, const char* sub = nullptr) {
    DisplayManager& dm = displayManager;
    int n = (int)items.size();
    if (n == 0) return -1;
    const int PER = 8;
    int page = 0;
    while (true) {
        int pages = (n + PER - 1) / PER; if (page >= pages) page = pages - 1;
        header(noun);
        if (sub && *sub) {
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_CYAN);
            char d[46]; snprintf(d, sizeof(d), "dir: %.39s", sub); dm.println(d);
        }
        int start = page * PER, end = start + PER < n ? start + PER : n;
        for (int i = start; i < end; i++) {
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(TFT_WHITE);
            char l[44]; snprintf(l, sizeof(l), "[%d] %.36s", i - start + 1, items[i].c_str());
            dm.println(l);
        }
        dm.printSeparator();
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
        dm.println("1-8 pick  a/l page  q cancel");
        while (true) {
            if (LockScreenManager::getInstance().consumeJustUnlocked()) break;  // unlock blanked menu → repaint
            char k = inputHandler.getKeyboardInput();
            if (k == 'q' || k == 'Q') return -1;
            if ((k == 'l' || k == 'L') && page < pages - 1) { page++; break; }
            if ((k == 'a' || k == 'A') && page > 0)          { page--; break; }
            if (k >= '1' && k <= '8') {
                int idx = start + (k - '1');
                if (idx < end) return idx;
            }
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

// List files in `dir` whose name ends with `ext` (case-insensitive). `bases` gets
// the bare filenames (for display), `paths` the full dir/name paths.
static void listByExt(const char* dir, const char* ext,
                      std::vector<String>& bases, std::vector<String>& paths) {
    File d = SD.open(dir);
    if (d && d.isDirectory()) {
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            if (!f.isDirectory()) {
                String nm = f.name();
                int sl = nm.lastIndexOf('/');
                String base = sl >= 0 ? nm.substring(sl + 1) : nm;
                String low = base; low.toLowerCase();
                if (low.endsWith(ext)) {
                    bases.push_back(base);
                    paths.push_back(String(dir) + (dir[strlen(dir) - 1] == '/' ? "" : "/") + base);
                }
            }
            f.close();
        }
    }
    if (d) d.close();
}

// .cap parsing now lives in capparse::parseCap (cap_parse.h) — shared with `pwn`.

// ── the dictionary attack ────────────────────────────────────────────────────
static void runCrack(const CrackJob& job, const std::vector<String>& wlFiles, bool useBuiltin) {
    DisplayManager& dm = displayManager;
    const mbedtls_md_info_t* sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx); mbedtls_md_setup(&ctx, sha1, 1);

    auto tryPass = [&](const char* pw) -> bool {
        return job.haveHs
            ? wpacrack::verifyHandshake(pw, job.ssid, job.apMac, job.staMac, job.anonce,
                                        job.snonce, job.eapol, job.eapolLen, job.mic, &ctx, sha1)
            : wpacrack::verifyPMKID(pw, job.ssid, job.apMac, job.staMac, job.pmkid, &ctx, sha1);
    };

    const char* noun = job.haveHs ? "HSHAKE" : "PMKID";
    int32_t statY = 0;
    // Static header (title + SSID) — redrawn after a lock-screen blanks it.
    auto drawHeader = [&]() {
        header(noun);
        dm.setCursor(4, dm.getCursorY());
        dm.setTextColor(0x7BEF); dm.printText("SSID "); dm.setTextColor(TFT_WHITE);
        char s[34]; snprintf(s, sizeof(s), "%.31s", job.ssid); dm.println(s);
        dm.printSeparator();
        statY = dm.getCursorY();
    };
    drawHeader();

    char     found[64] = {0};
    uint32_t tried = 0, lastRedraw = 0, t0 = millis();
    bool     done = false, aborted = false;
    char     srcLabel[40] = "built-in";

    // Resume cursor (shared with pwn): a slow on-device wordlist run can be [q]-
    // aborted and picked up later at the exact byte offset instead of restarting
    // from word 0. Keyed by this cap's (BSSID,SSID); persisted to
    // /apps/capcrack/progress.csv. Only the SD-wordlist path resumes — the tiny
    // built-in list finishes in seconds, so it is not worth a cursor.
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             job.apMac[0], job.apMac[1], job.apMac[2], job.apMac[3], job.apMac[4], job.apMac[5]);
    const bool  resumeOk = sdCardManager.canAccessSD();
    const char* progPath = SD_DIR_CAPCRACK "/progress.csv";
    if (resumeOk) sdCardManager.ensureDir(SD_DIR_CAPCRACK);
    uint32_t lastSave = 0;

    auto status = [&](const char* cand) {
        dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 3, TFT_BLACK);
        uint32_t el = (millis() - t0) / 1000;
        uint32_t rate = el ? tried / el : tried;
        dm.setCursor(4, statY); dm.setTextColor(TFT_WHITE);
        char b[40]; snprintf(b, sizeof(b), "Tried %lu  %lu/s", (unsigned long)tried, (unsigned long)rate);
        dm.println(b);
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
        char l[40]; snprintf(l, sizeof(l), "src %.30s", srcLabel); dm.println(l);
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x4208);
        char c[36]; snprintf(c, sizeof(c), "%.30s", cand); dm.println(c);
    };

    // one wordlist file → stream + try each line, resuming from the saved cursor
    auto runFile = [&](const String& path) {
        File wl = SD.open(path.c_str(), FILE_READ);
        if (!wl) return;
        long   wlSize = (long)wl.size();
        // wordlist_id = full path + size. Path so two different lists (even the same
        // size) never share a cursor and a typed-path vs cwd-.txt are distinguished;
        // size so EDITING a list re-arms it. Commas stripped so the id can't shift the
        // CSV columns. A path-string mismatch only ever restarts from the top (safe) —
        // never a wrong-offset resume (which a size-only id could do on a size clash).
        String wid = path; wid.replace(',', ' ');
        wid += ":"; wid += String(wlSize);
        { int sl = path.lastIndexOf('/');
          snprintf(srcLabel, sizeof(srcLabel), "%.38s", sl >= 0 ? path.c_str() + sl + 1 : path.c_str()); }
        if (resumeOk) {
            long at = crackprog::get(progPath, macStr, job.ssid, wid);
            if (wlSize > 0 && at >= wlSize) { wl.close(); return; }   // already exhausted → skip
            if (at > 0) {                                            // resume mid-list
                wl.seek(at);
                int n = strlen(srcLabel), pct = wlSize ? (int)(at * 100 / wlSize) : 0;
                snprintf(srcLabel + n, sizeof(srcLabel) - n, " @%d%%", pct);
            }
        }
        while (wl.available() && !done && !aborted) {
            String line = wl.readStringUntil('\n'); line.trim();
            if (line.length() < 8 || line.length() > 63) continue;
            tried++;
            uint32_t now = millis();
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawHeader(); status(line.c_str()); lastRedraw = now;
            }
            if (now - lastRedraw >= 300) {
                lastRedraw = now; status(line.c_str());
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') { aborted = true; break; }
                vTaskDelay(1);
            }
            // persist the resume point ~every 2s so a q/abort/reboot picks up here
            if (resumeOk && now - lastSave >= 2000) {
                crackprog::set(progPath, macStr, job.ssid, wid, (long)wl.position());
                lastSave = now;
            }
            if (tryPass(line.c_str())) { strncpy(found, line.c_str(), sizeof(found) - 1); done = true; }
        }
        // record where we stopped (aborted mid-list, or exhausted = position==size)
        // so the next run resumes exactly here or skips a finished list. Skip empty
        // lists (wlSize==0) — they'd only write a useless offset-0 row.
        if (resumeOk && !done && wlSize > 0)
            crackprog::set(progPath, macStr, job.ssid, wid, (long)wl.position());
        wl.close();
    };

    for (size_t i = 0; i < wlFiles.size() && !done && !aborted; i++) runFile(wlFiles[i]);

    if (useBuiltin && !done && !aborted) {
        strncpy(srcLabel, "built-in (100)", sizeof(srcLabel));
        for (int i = 0; i < wpacrack::kBuiltinCount && !done && !aborted; i++) {
            tried++;
            uint32_t now = millis();
            if (LockScreenManager::getInstance().consumeJustUnlocked()) {
                drawHeader(); status(wpacrack::kBuiltins[i]); lastRedraw = now;
            }
            if (now - lastRedraw >= 300) {
                lastRedraw = now; status(wpacrack::kBuiltins[i]);
                char k = inputHandler.getKeyboardInput();
                if (k == 'q' || k == 'Q') { aborted = true; break; }
                vTaskDelay(1);
            }
            if (tryPass(wpacrack::kBuiltins[i])) { strncpy(found, wpacrack::kBuiltins[i], sizeof(found) - 1); done = true; }
        }
    }
    mbedtls_md_free(&ctx);

    dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 3, TFT_BLACK);
    dm.setCursor(4, statY);
    if (done) {
        if (resumeOk) crackprog::remove(progPath, macStr, job.ssid);   // solved → nothing to resume
        dm.setTextColor(TFT_GREEN);
        char b[80]; snprintf(b, sizeof(b), "FOUND: %s", found); dm.println(b);
        if (sdCardManager.canAccessSD()) {
            sdCardManager.ensureDir(SD_DIR_CAPCRACK);
            File f = SD.open(SD_DIR_CAPCRACK "/cracked.csv", FILE_APPEND);
            if (f) { f.printf("%s,%s,%s\n", job.ssid, found, job.haveHs ? "HS" : "PMKID"); f.close(); }
        }
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
        dm.println("Saved /apps/capcrack/cracked.csv");
    } else {
        char b[40];
        snprintf(b, sizeof(b), aborted ? "Aborted (%lu tried)" : "Not found (%lu tried)",
                 (unsigned long)tried);
        dm.setTextColor(TFT_YELLOW); dm.println(b);
    }
    dm.printCommandScreen();
}

// Resolve a wordlist path token into the file(s) to run: a directory → every
// `*.txt` inside it; a plain file → just that file. Shared by the explicit-arg
// path and the interactive "Type a path…" option (no duplicated resolution).
static void resolveWlPath(const char* path, std::vector<String>& out) {
    char resolved[160]; sdCardManager.resolvePath(path, resolved, sizeof(resolved));
    File t = SD.open(resolved);
    bool isDir = t && t.isDirectory(); if (t) t.close();
    if (isDir) {
        std::vector<String> bases, paths;
        listByExt(resolved, ".txt", bases, paths);
        out.insert(out.end(), paths.begin(), paths.end());
    } else {
        out.push_back(String(resolved));
    }
}

// ── wordlist selection ────────────────────────────────────────────────────────
// Builds the list of wordlist files to run + whether to also try the built-in.
// arg==nullptr → interactive picker over the current dir. Returns false on cancel.
static bool chooseWordlists(const char* arg, const char* cwd,
                            std::vector<String>& out, bool& useBuiltin) {
    out.clear(); useBuiltin = true;

    if (arg && *arg) { resolveWlPath(arg, out); return true; }   // explicit path/dir given

    // interactive: built-in / all-in-dir / type-a-path / a specific .txt in cwd
    std::vector<String> bases, paths;
    listByExt(cwd, ".txt", bases, paths);
    std::vector<String> menu;
    menu.push_back("Built-in list (100)");
    menu.push_back("ALL *.txt in this dir");
    menu.push_back("Type a path... (' completes)");
    const int NFIXED = 3;
    for (auto& b : bases) menu.push_back(b);

    // Back-stack (isoscan idiom): a cancelled/invalid sub-choice re-shows the picker
    // instead of aborting cc. Only [q] on the picker itself (sel<0) cancels the command.
    while (true) {
        int sel = pickList("WLIST", menu, cwd);
        if (sel < 0) return false;                               // [q] on the picker → cancel cc
        if (sel == 0) { useBuiltin = true; return true; }        // built-in only
        if (sel == 1) { out = paths; return true; }              // every .txt here
        if (sel == 2) {                                          // type a wordlist file or dir path
            char typed[160];
            if (!pathprompt::prompt("Wordlist path (file or dir):", typed, sizeof(typed)))
                continue;                                        // cancelled → back to picker
            char resolved[160]; sdCardManager.resolvePath(typed, resolved, sizeof(resolved));
            if (!SD.exists(resolved)) {                          // typo / missing → tell them, stay in the picker
                notifyWait("Path not found", TFT_YELLOW, resolved); continue;
            }
            out.clear(); resolveWlPath(resolved, out);
            if (out.empty()) {                                   // a dir with no *.txt inside
                notifyWait("No .txt in that folder", TFT_YELLOW, resolved); continue;
            }
            return true;
        }
        out.push_back(paths[sel - NFIXED]);                      // one chosen .txt in cwd
        return true;
    }
}

// ── entry point ───────────────────────────────────────────────────────────────
// Live monitor for the running background crack — real-time tried/rate/candidate.
// Pumping getKeyboardInput() here also advances the crack. [q] leaves the monitor
// WITHOUT stopping it (it keeps running in the background). If the crack finishes
// while you watch, the outcome is shown until you press a key.
static void ccBgMonitor() {
    DisplayManager& dm = displayManager;
    auto drawChrome = [&]() {
        header("CC BG");
        dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.printText("SSID ");
        dm.setTextColor(TFT_WHITE);
        char s[34]; snprintf(s, sizeof(s), "%.27s", capcrackBgSsid()); dm.println(s);
        dm.printSeparator();
    };
    drawChrome();
    int32_t  statY   = dm.getCursorY();
    uint32_t t0      = millis(), tried0 = capcrackBgTried(), lastDraw = 0;
    while (capcrackBgActive()) {
        char k = inputHandler.getKeyboardInput();          // also pumps the crack forward
        if (k == 'q' || k == 'Q') { dm.printCommandScreen(); return; }   // leave; crack keeps running
        if (k == 's' || k == 'S') { stopCapcrackBg(); break; }           // stop it → fall to the outcome screen
        if (dm.isBlocked()) { vTaskDelay(pdMS_TO_TICKS(15)); continue; }
        if (LockScreenManager::getInstance().consumeJustUnlocked()) { drawChrome(); statY = dm.getCursorY(); }
        uint32_t now = millis();
        if (now - lastDraw >= 300) {
            lastDraw = now;
            uint32_t tried = capcrackBgTried();
            uint32_t el    = (now - t0) / 1000;
            uint32_t rate  = el ? (tried - tried0) / el : 0;
            dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 4, TFT_BLACK);
            dm.setCursor(4, statY); dm.setTextColor(TFT_WHITE);
            int pct = capcrackBgPct();   // list position (reflects resume); tried is session-relative
            char b[56];
            if (pct >= 0) snprintf(b, sizeof(b), "list %d%%  +%lu  %lu/s", pct, (unsigned long)tried, (unsigned long)rate);
            else          snprintf(b, sizeof(b), "built-in  +%lu  %lu/s", (unsigned long)tried, (unsigned long)rate);
            dm.println(b);
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x4208);
            char c[40]; snprintf(c, sizeof(c), "%.34s", capcrackBgCurrent()); dm.println(c);
            dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF);
            dm.println("[q] back (keeps running)  [s] stop");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // finished while watching — show the outcome, wait for a key
    dm.fillRect(4, statY, SCREEN_WIDTH - 8, LINE_HEIGHT * 4, TFT_BLACK);
    dm.setCursor(4, statY); dm.setTextColor(TFT_GREEN); dm.println(capcrackBgStatus());
    dm.setCursor(4, dm.getCursorY()); dm.setTextColor(0x7BEF); dm.println("any key...");
    while (inputHandler.getKeyboardInput() == 0) vTaskDelay(pdMS_TO_TICKS(15));
    dm.printCommandScreen();
}

void runCapCrack(char* args) {
    DisplayManager& dm = displayManager;

    // Bare `cc` while a background crack is running = watch it live (what you'd
    // instinctively type). To start a NEW foreground crack instead, pass a cap
    // (`cc <cap>`), or stop the bg one first (`cc bg stop`).
    if ((!args || !*args) && capcrackBgActive()) { ccBgMonitor(); return; }

    // ── background sub-commands: cc bg [stop|status] | cc bg [cap] [wordlist] ────
    // 'bg' runs the crack cooperatively off the main loop so it keeps going under
    // the CLI AND the undercover cover (grind wordlists in public). stop/status
    // are explicit; anything else after 'bg' (incl. nothing) starts a bg crack via
    // the normal interactive/arg cap+wordlist selection below.
    bool bgMode = false;
    if (args && strncmp(args, "bg", 2) == 0 && (args[2] == '\0' || args[2] == ' ')) {
        char* rest = args + 2; while (*rest == ' ') rest++;
        if (strncmp(rest, "stop", 4) == 0) {
            stopCapcrackBg();
            msgScreen("cc bg: stopped", TFT_YELLOW, "Cursor saved - 'cc bg' resumes.");
            return;
        }
        if (strncmp(rest, "status", 6) == 0) {
            if (capcrackBgActive()) ccBgMonitor();   // live real-time view
            else {
                // Not running: show WHY (finished the list / found / stopped) so an
                // exhausted run doesn't look like it silently died.
                const char* s = capcrackBgStatus();
                if (s && *s) msgScreen("cc bg: not running", TFT_YELLOW, s);
                else         msgScreen("cc bg: idle", 0x7BEF, "cc bg <cap> [wordlist] to start.");
            }
            return;
        }
        // bare 'cc bg' while a crack is already running = WATCH it live (you can't run
        // two anyway); with none running it falls through to start one interactively.
        if (*rest == '\0' && capcrackBgActive()) { ccBgMonitor(); return; }
        bgMode = true; args = rest;   // remainder (maybe empty) is the cap [wordlist] selection
    }

    if (!sdCardManager.canAccessSD()) {
        msgScreen("No SD card.", TFT_RED, "Cracking reads .cap from SD.");
        return;
    }
    const char* cwd = sdCardManager.getCwd();

    // split args → capArg + wlArg (two tokens)
    char buf[160]; buf[0] = '\0';
    if (args) { strncpy(buf, args, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0'; }
    char* capArg = strtok(buf, " ");
    char* wlArg  = strtok(nullptr, " ");

    // resolve the cap: explicit file, a dir to pick from, or pick from cwd
    char capPath[160];
    if (!capArg) {                                       // no arg → pick a .cap in cwd
        std::vector<String> bases, paths;
        listByExt(cwd, ".cap", bases, paths);
        listByExt(cwd, ".pcap", bases, paths);
        if (paths.empty()) { msgScreen("No .cap here.", TFT_YELLOW, "cd to the capture dir, or pass a path."); return; }
        int sel = pickList("PICK CAP", bases, cwd);
        if (sel < 0) { dm.printCommandScreen(); return; }
        strncpy(capPath, paths[sel].c_str(), sizeof(capPath) - 1); capPath[sizeof(capPath) - 1] = '\0';
    } else {
        char resolved[160]; sdCardManager.resolvePath(capArg, resolved, sizeof(resolved));
        File t = SD.open(resolved);
        bool isDir = t && t.isDirectory(); if (t) t.close();
        if (isDir) {                                     // dir → pick a cap inside it
            std::vector<String> bases, paths;
            listByExt(resolved, ".cap", bases, paths);
            listByExt(resolved, ".pcap", bases, paths);
            if (paths.empty()) { msgScreen("No .cap in dir.", TFT_YELLOW, resolved); return; }
            int sel = pickList("PICK CAP", bases, resolved);
            if (sel < 0) { dm.printCommandScreen(); return; }
            strncpy(capPath, paths[sel].c_str(), sizeof(capPath) - 1); capPath[sizeof(capPath) - 1] = '\0';
        } else {
            strncpy(capPath, resolved, sizeof(capPath) - 1); capPath[sizeof(capPath) - 1] = '\0';
        }
    }

    // parse
    CrackJob job; char err[64];
    if (!capparse::parseCap(capPath, job, err, sizeof(err))) { msgScreen(err, TFT_RED, capPath); return; }

    // wordlists
    std::vector<String> wl; bool useBuiltin = true;
    if (!chooseWordlists(wlArg, cwd, wl, useBuiltin)) { dm.printCommandScreen(); return; }

    if (bgMode) {
        startCapcrackBg(job, wl, useBuiltin);
        msgScreen("cc bg: cracking in background",
                  TFT_ORANGE, "Runs under the cover. 'cc bg stop' halts.");
    } else {
        stopCapcrackBg();                 // don't run a foreground + background crack on the same SD/cursor
        runCrack(job, wl, useBuiltin);
    }
}
