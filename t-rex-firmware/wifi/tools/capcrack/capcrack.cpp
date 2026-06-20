// T-REX — offensive security firmware for LilyGo T-Deck
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
#include "wpa_crack.h"      // PBKDF2 / handshake-MIC / PMKID dictionary crack
#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <string.h>
#include <strings.h>

extern DisplayManager displayManager;
extern InputHandling  inputHandler;
extern SDCardManager  sdCardManager;

// ── extracted crack material ────────────────────────────────────────────────
struct CrackJob {
    bool     haveHs;
    bool     havePmkid;
    char     ssid[33];
    uint8_t  apMac[6], staMac[6];
    uint8_t  anonce[32], snonce[32], mic[16];
    uint8_t  eapol[256]; uint16_t eapolLen;     // M2 frame, MIC zeroed
    uint8_t  pmkid[16];
};

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

// ── parse a .cap into a CrackJob ────────────────────────────────────────────
static bool parseCap(const char* path, CrackJob& job, char* err, size_t errN) {
    memset(&job, 0, sizeof(job));
    File f = SD.open(path, FILE_READ);
    if (!f) { snprintf(err, errN, "Cannot open file"); return false; }

    bool sw = false; uint32_t lt = 0;
    if (!pcap::readGlobalHeader(f, &sw, &lt)) {
        f.close(); snprintf(err, errN, "Not a classic .cap/.pcap"); return false;
    }

    bool haveM1 = false, haveM2 = false;
    uint8_t fr[512];
    uint16_t n;
    while ((n = pcap::readRecord(f, sw, fr, sizeof(fr))) > 0) {
        if (n < 24) continue;
        uint8_t ft = dot11::fType(fr), st = dot11::fSubtype(fr);

        if (ft == 0 && (st == dot11::ST_BEACON || st == dot11::ST_PROBE_RESP)) {
            if (!job.ssid[0]) {
                char s[33];
                if (dot11::extractSSID(fr, n, st, s, sizeof(s))) { strncpy(job.ssid, s, 32); job.ssid[32] = '\0'; }
            }
            continue;
        }
        if (ft != 2) continue;

        dot11::Eapol ek;
        if (!dot11::parseEapol(fr, n, ek)) continue;

        if (ek.msg == 1 && ek.len >= 49) {
            if (!haveM1) {
                memcpy(job.anonce, ek.p + 17, 32);
                memcpy(job.apMac, dot11::dataBssid(fr), 6);     // M1 fromDS: bssid = addr2
                haveM1 = true;
            }
            // PMKID KDE in M1 Key Data: DD <len> 00 0F AC 04 <16B PMKID>
            if (!job.havePmkid && ek.len >= 99) {
                uint16_t kdl = ((uint16_t)ek.p[97] << 8) | ek.p[98];
                uint32_t maxkd = ek.len - 99; if (kdl > maxkd) kdl = (uint16_t)maxkd;
                const uint8_t* kd = ek.p + 99;
                for (uint16_t i = 0; i + 22 <= kdl; i++) {
                    if (kd[i] == 0xDD && kd[i + 2] == 0x00 && kd[i + 3] == 0x0F &&
                        kd[i + 4] == 0xAC && kd[i + 5] == 0x04) {
                        memcpy(job.pmkid, kd + i + 6, 16);
                        memcpy(job.apMac,  dot11::dataBssid(fr), 6);
                        memcpy(job.staMac, fr + 4, 6);          // M1 fromDS: DA = STA
                        job.havePmkid = true;
                        break;
                    }
                }
            }
        } else if (ek.msg == 2 && ek.len >= 97 && !haveM2) {
            memcpy(job.snonce, ek.p + 17, 32);
            memcpy(job.mic,    ek.p + 81, 16);
            uint16_t el = (((uint16_t)ek.p[2] << 8) | ek.p[3]) + 4;
            if (el > ek.len)            el = ek.len;
            if (el > sizeof(job.eapol)) el = sizeof(job.eapol);
            memcpy(job.eapol, ek.p, el);
            memset(job.eapol + 81, 0, 16);                       // zero MIC for the HMAC
            job.eapolLen = el;
            memcpy(job.apMac,  dot11::dataBssid(fr), 6);         // M2 toDS: bssid = addr1
            memcpy(job.staMac, fr + 10, 6);                      // M2 toDS: SA = STA
            haveM2 = true;
        }
    }
    f.close();

    if (haveM1 && haveM2 && job.ssid[0]) job.haveHs = true;
    if (job.haveHs || (job.havePmkid && job.ssid[0])) return true;

    snprintf(err, errN, "No HS/PMKID:%s%s%s%s", job.ssid[0] ? "" : " noESSID",
             haveM1 ? "" : " noM1", haveM2 ? "" : " noM2",
             job.havePmkid ? " (PMKID found but no ESSID)" : "");
    return false;
}

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

    // one wordlist file → stream + try each line
    auto runFile = [&](const String& path) {
        File wl = SD.open(path.c_str(), FILE_READ);
        if (!wl) return;
        { int sl = path.lastIndexOf('/');
          snprintf(srcLabel, sizeof(srcLabel), "%.38s", sl >= 0 ? path.c_str() + sl + 1 : path.c_str()); }
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
            if (tryPass(line.c_str())) { strncpy(found, line.c_str(), sizeof(found) - 1); done = true; }
        }
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

// ── wordlist selection ────────────────────────────────────────────────────────
// Builds the list of wordlist files to run + whether to also try the built-in.
// arg==nullptr → interactive picker over the current dir. Returns false on cancel.
static bool chooseWordlists(const char* arg, const char* cwd,
                            std::vector<String>& out, bool& useBuiltin) {
    out.clear(); useBuiltin = true;

    if (arg && *arg) {                                   // explicit path/dir given
        char resolved[160]; sdCardManager.resolvePath(arg, resolved, sizeof(resolved));
        File t = SD.open(resolved);
        bool isDir = t && t.isDirectory(); if (t) t.close();
        if (isDir) {
            std::vector<String> bases, paths;
            listByExt(resolved, ".txt", bases, paths);
            out = paths;
        } else {
            out.push_back(String(resolved));
        }
        return true;
    }

    // interactive: built-in / all-in-dir / a specific .txt in cwd
    std::vector<String> bases, paths;
    listByExt(cwd, ".txt", bases, paths);
    std::vector<String> menu;
    menu.push_back("Built-in list (100)");
    menu.push_back("ALL *.txt in this dir");
    for (auto& b : bases) menu.push_back(b);
    int sel = pickList("WLIST", menu, cwd);
    if (sel < 0) return false;
    if (sel == 0) { useBuiltin = true; return true; }            // built-in only
    if (sel == 1) { out = paths; return true; }                  // every .txt here
    out.push_back(paths[sel - 2]);                               // one chosen file
    return true;
}

// ── entry point ───────────────────────────────────────────────────────────────
void runCapCrack(char* args) {
    DisplayManager& dm = displayManager;
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
    if (!parseCap(capPath, job, err, sizeof(err))) { msgScreen(err, TFT_RED, capPath); return; }

    // wordlists
    std::vector<String> wl; bool useBuiltin = true;
    if (!chooseWordlists(wlArg, cwd, wl, useBuiltin)) { dm.printCommandScreen(); return; }

    runCrack(job, wl, useBuiltin);
}
