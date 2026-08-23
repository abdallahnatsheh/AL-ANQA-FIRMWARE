#include "capcrack_bg.h"
#include <SD.h>
#include "esp_wifi.h"
#include "mbedtls/md.h"
#include "sdcard_manager.h"
#include "display_manager.h"
#include "wpa_crack.h"
#include "crack_progress.h"
#include "notification_manager.h"
#include "powersave_manager.h"   // isScreenOff() — screen-state crack throttle
#include "covert.h"              // g_covert — pause under the on-screen cover (touch priority)

extern DisplayManager displayManager;
extern SDCardManager  sdCardManager;

#define CCBG_SLICE_MS   10                       // work budget per pumped slice
#define CCBG_SAVE_MS    2000                      // resume-cursor checkpoint cadence
static const char* CCBG_PROG = SD_DIR_CAPCRACK "/progress.csv";

// ── running state (one background crack at a time) ────────────────────────────
struct CcBg {
    bool     active = false;
    capparse::CrackJob  job;
    std::vector<String> files;
    bool     useBuiltin = true;
    size_t   fi = 0;                              // current wordlist index
    File     wl; bool wlOpen = false;
    long     wlSize = 0;
    String   wid;
    bool     inBuiltin = false;
    int      bi = 0;
    char     macStr[18] = {0};
    bool     resumeOk = false;
    uint32_t tried = 0, lastSave = 0;
    mbedtls_md_context_t     ctx;
    const mbedtls_md_info_t* sha1 = nullptr;
    bool     ctxReady = false;
};
static CcBg    B;
static uint32_t s_lastSlice = 0;
// Outcome of the last/ current bg run so `cc bg status` can tell "still running"
// from "finished the list (not found)" from "found" from "stopped" — otherwise a
// run that simply exhausts its wordlist looks like it silently died.
static char    s_status[52] = "";
static char    s_cur[40]     = "";   // current candidate (for the live monitor)

static bool tryPass(const char* pw) {
    return B.job.haveHs
        ? wpacrack::verifyHandshake(pw, B.job.ssid, B.job.apMac, B.job.staMac, B.job.anonce,
                                    B.job.snonce, B.job.eapol, B.job.eapolLen, B.job.mic, &B.ctx, B.sha1)
        : wpacrack::verifyPMKID(pw, B.job.ssid, B.job.apMac, B.job.staMac, B.job.pmkid, &B.ctx, B.sha1);
}

// GDMA guard: never touch SD while WiFi is doing bulk DMA (promiscuous / AP) —
// yields to wg-bg, wm, evil-twin, etc. without them having to know about us.
static bool wifiBusy() {
    bool promisc = false; esp_wifi_get_promiscuous(&promisc);
    if (promisc) return true;
    wifi_mode_t m = WIFI_MODE_NULL; esp_wifi_get_mode(&m);
    return (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA);
}

static void teardown(bool saveCursor) {
    if (B.wlOpen) {
        if (saveCursor && B.resumeOk && B.wlSize > 0)
            crackprog::set(CCBG_PROG, B.macStr, B.job.ssid, B.wid, (long)B.wl.position());
        B.wl.close(); B.wlOpen = false;
    }
    if (B.ctxReady) { mbedtls_md_free(&B.ctx); B.ctxReady = false; }
    B.files.clear(); B.files.shrink_to_fit();
    B.active = false;
    displayManager.setCcActive(false);
}

// Open files[fi], compute its wordlist_id, honour the resume cursor. Returns
// false if the file can't be opened OR is already exhausted (caller skips it).
static bool openCurrentFile() {
    B.wl = SD.open(B.files[B.fi].c_str(), FILE_READ);
    if (!B.wl) return false;
    B.wlSize = (long)B.wl.size();
    B.wid = B.files[B.fi]; B.wid.replace(',', ' '); B.wid += ":"; B.wid += String(B.wlSize);
    B.wlOpen = true;
    if (B.resumeOk) {
        long at = crackprog::get(CCBG_PROG, B.macStr, B.job.ssid, B.wid);
        if (B.wlSize > 0 && at >= B.wlSize) { B.wl.close(); B.wlOpen = false; return false; }  // already done
        if (at > 0) B.wl.seek(at);
    }
    return true;
}

// Advance the state machine to the next candidate; nullptr when everything is
// exhausted. Returned pointer is valid until the next call.
static const char* nextCandidate() {
    static char line[72];
    while (!B.inBuiltin) {                        // wordlist phase
        if (!B.wlOpen) {
            if (B.fi >= B.files.size()) { B.inBuiltin = true; B.bi = 0; break; }
            if (!openCurrentFile()) { B.fi++; continue; }
        }
        if (!B.wl.available()) {                  // file finished → mark exhausted + next
            if (B.resumeOk && B.wlSize > 0)
                crackprog::set(CCBG_PROG, B.macStr, B.job.ssid, B.wid, (long)B.wl.position());
            B.wl.close(); B.wlOpen = false; B.fi++;
            continue;
        }
        String s = B.wl.readStringUntil('\n'); s.trim();
        if (s.length() < 8 || s.length() > 63) continue;
        strncpy(line, s.c_str(), sizeof(line) - 1); line[sizeof(line) - 1] = '\0';
        return line;
    }
    if (B.useBuiltin && B.bi < wpacrack::kBuiltinCount)   // built-in phase (last)
        return wpacrack::kBuiltins[B.bi++];
    return nullptr;                              // exhausted
}

static void onFound(const char* pw) {
    if (B.resumeOk) {
        sdCardManager.ensureDir(SD_DIR_CAPCRACK);
        File f = SD.open(SD_DIR_CAPCRACK "/cracked.csv", FILE_APPEND);
        if (f) { f.printf("%s,%s,%s\n", B.job.ssid, pw, B.job.haveHs ? "HS" : "PMKID"); f.close(); }
        crackprog::remove(CCBG_PROG, B.macStr, B.job.ssid);   // solved → drop the cursor
    }
    // Silent under the cover (notify() self-gates on g_covert). The win is in
    // cracked.csv either way; you find it when you drop the disguise.
    snprintf(s_status, sizeof(s_status), "FOUND (in cracked.csv) after %lu", (unsigned long)B.tried);
    NotificationManager::getInstance().notify(NOTIF_SUCCESS);
    teardown(false);
}

void pollCapcrackBg() {
    if (!B.active) return;
    if (wifiBusy()) return;                       // GDMA-safe: yield SD while WiFi does DMA
    // Throttle by what's happening ON the screen:
    //  - Under the undercover cover with the screen ON, the user is looking at /
    //    TOUCHING the disguise. A crack slice (~1 PBKDF2, tens of ms) stalls the cover
    //    loop, which inflates the touch tap-timer (heldMs > TOUCH_TAP_MS) and DROPS
    //    every tap (trackball, being latched, survives — the reported symptom). So we
    //    PAUSE entirely here — touch + a smooth disguise take priority.
    //  - Screen OFF (pocket / put down, nobody touching) → crack HARD.
    //  - CLI, screen on → crack GENTLY (no touch there; keeps typing/menus responsive).
    bool screenOff = PowerSaveManager::getInstance().isScreenOff();
    if (g_covert && !screenOff) return;                 // on-screen disguise → don't stall touch
    uint32_t gap = screenOff ? 6 : 150;
    uint32_t now = millis();
    if (now - s_lastSlice < gap) return;
    s_lastSlice = now;

    do {
        const char* cand = nextCandidate();
        if (!cand) {                               // whole wordlist(s) + built-ins tried, no hit
            snprintf(s_status, sizeof(s_status), "done: not found (%lu tried)", (unsigned long)B.tried);
            NotificationManager::getInstance().notify(NOTIF_INFO);   // soft cue on the CLI; silent under cover
            teardown(false);
            return;
        }
        strncpy(s_cur, cand, sizeof(s_cur) - 1); s_cur[sizeof(s_cur) - 1] = '\0';   // for the live monitor
        B.tried++;
        if (tryPass(cand)) { onFound(cand); return; }
        if (B.resumeOk && B.wlOpen && millis() - B.lastSave >= CCBG_SAVE_MS) {
            crackprog::set(CCBG_PROG, B.macStr, B.job.ssid, B.wid, (long)B.wl.position());
            B.lastSave = millis();
        }
    } while (millis() - now < CCBG_SLICE_MS);
}

bool startCapcrackBg(const capparse::CrackJob& job, const std::vector<String>& wlFiles, bool useBuiltin) {
    stopCapcrackBg();                             // one at a time
    B.job        = job;
    B.files      = wlFiles;
    B.useBuiltin = useBuiltin;
    B.fi = 0; B.wlOpen = false; B.inBuiltin = false; B.bi = 0;
    B.tried = 0; B.lastSave = millis();
    snprintf(B.macStr, sizeof(B.macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             job.apMac[0], job.apMac[1], job.apMac[2], job.apMac[3], job.apMac[4], job.apMac[5]);
    B.resumeOk = sdCardManager.canAccessSD();
    if (B.resumeOk) sdCardManager.ensureDir(SD_DIR_CAPCRACK);
    // A prior tool may have left WiFi in a DMA-heavy mode (promiscuous / AP). The
    // per-slice GDMA guard would then block the crack FOREVER (it never advances,
    // looks dead). cc has no WiFi need, so drop stale promiscuous/AP to STA-idle
    // here so it can run. (We still YIELD mid-run to any tool started AFTER us.)
    if (wifiBusy()) { esp_wifi_set_promiscuous(false); esp_wifi_set_mode(WIFI_MODE_STA); }
    B.sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_init(&B.ctx); mbedtls_md_setup(&B.ctx, B.sha1, 1); B.ctxReady = true;
    B.active = true;
    s_lastSlice = 0;
    snprintf(s_status, sizeof(s_status), "running");
    displayManager.setCcActive(true);
    return true;
}

void stopCapcrackBg() {
    if (!B.active) return;
    snprintf(s_status, sizeof(s_status), "stopped (%lu tried, cursor saved)", (unsigned long)B.tried);
    teardown(true);   // save cursor on a deliberate stop
}

bool        capcrackBgActive() { return B.active; }
const char* capcrackBgSsid()   { return B.active ? B.job.ssid : ""; }
uint32_t    capcrackBgTried()  { return B.tried; }
const char* capcrackBgStatus()  { return s_status; }
const char* capcrackBgCurrent() { return s_cur; }
// Position through the CURRENT wordlist (0-100), or -1 on the built-in list / none.
// Reflects the resume seek, so a resumed crack shows e.g. 5% not 0% (the tried
// counter is only session-relative, which otherwise looks like a restart).
int capcrackBgPct() {
    if (B.active && B.wlOpen && B.wlSize > 0) return (int)(B.wl.position() * 100 / B.wlSize);
    return -1;
}
