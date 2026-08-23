#pragma once
// Per-cap wordlist RESUME CURSOR, shared by pwn (idle, time-sliced background
// crack) and capcrack/cc (foreground one-shot crack). Extracted from pwn.cpp so
// both cracks resume from exactly where they left off — even across reboots —
// instead of restarting a slow on-device dictionary run from word 0 (rule 5b,
// no duplicated cursor logic).
//
// Storage: one CSV row per (bssid,ssid,wordlist_id) — `bssid,ssid,wordlist_id,
// next` — in a small file the caller names (pwn: /apps/pwn/progress.csv, cc:
// /apps/capcrack/progress.csv). Keying on the wordlist_id too (not just the cap)
// means one cap cracked against SEVERAL wordlists keeps an independent cursor per
// list — so cc's directory mode (`cc cap <dir>`) resumes each list where it
// stopped instead of re-running earlier ones. The file is rewritten whole on
// set()/remove() (it is tiny). `wordlist_id` = the wordlist's identity (cc: full
// path+size, pwn: size); an unseen id just starts that list from the top. `next`
// = a BYTE OFFSET into an SD wordlist (O(1) seek resume, no per-slice re-skip) or
// an array index into a built-in list — the caller decides which. remove() drops
// ALL of a cap's rows (a solved cap needs no cursor for any list); a row for a
// since-edited wordlist lingers harmlessly until then.
//
// set()/remove() wrap the SD write in ScopedPromiscPause: REQUIRED for pwn
// (promiscuous is live, GDMA rule) and a safe no-op for cc (no WiFi active),
// so neither caller has to think about it.

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include "wifi_sd_guard.h"   // ScopedPromiscPause (no-op when promiscuous is off)

namespace crackprog {

struct Cursor { String bssid, ssid, wid; long next; };

// Strip commas from the SSID key so a comma inside an SSID can't shift the CSV
// columns and corrupt the row (pwn already passes a comma-free ssid; cc may pass
// a raw one — sanitising here makes both callers safe, idempotently).
inline String _ssidKey(const char* ssid) { String s(ssid); s.replace(',', ' '); return s; }

// Load every cursor row from `path` (missing file → empty, no error).
inline void load(const char* path, std::vector<Cursor>& v) {
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    while (f.available()) {
        String ln = f.readStringUntil('\n'); ln.trim();
        int c1 = ln.indexOf(','), c2 = ln.indexOf(',', c1 + 1), c3 = ln.indexOf(',', c2 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0) continue;
        Cursor c;
        c.bssid = ln.substring(0, c1); c.ssid = ln.substring(c1 + 1, c2);
        c.wid   = ln.substring(c2 + 1, c3); c.next = ln.substring(c3 + 1).toInt();
        v.push_back(c);
    }
    f.close();
}

// Resume point for (mac,ssid) against wordlist `wid`. Returns 0 (start over) if
// there is no row for this exact (cap, wordlist) — a new/edited list starts fresh.
inline long get(const char* path, const char* mac, const char* ssid, const String& wid) {
    String sk = _ssidKey(ssid);
    std::vector<Cursor> v; load(path, v);
    for (auto& c : v)
        if (c.bssid.equalsIgnoreCase(mac) && c.ssid == sk && c.wid == wid)
            return c.next;
    return 0;
}

inline void save(const char* path, const std::vector<Cursor>& v) {
    ScopedPromiscPause _;                    // GDMA-safe for pwn; no-op for cc
    File f = SD.open(path, FILE_WRITE);      // truncate + rewrite (file is tiny)
    if (!f) return;
    for (auto& c : v)
        f.printf("%s,%s,%s,%ld\n", c.bssid.c_str(), c.ssid.c_str(), c.wid.c_str(), c.next);
    f.close();
}

// Upsert the (mac,ssid,wid) cursor to `next` and persist. Distinct wordlists for
// the same cap get distinct rows (directory-mode resume stays independent).
inline void set(const char* path, const char* mac, const char* ssid, const String& wid, long next) {
    String sk = _ssidKey(ssid);
    std::vector<Cursor> v; load(path, v);
    bool found = false;
    for (auto& c : v)
        if (c.bssid.equalsIgnoreCase(mac) && c.ssid == sk && c.wid == wid) { c.next = next; found = true; break; }
    if (!found) { Cursor c{ String(mac), sk, wid, next }; v.push_back(c); }
    save(path, v);
}

// Drop ALL of a cap's cursors (e.g. after a successful crack — nothing to resume).
inline void remove(const char* path, const char* mac, const char* ssid) {
    String sk = _ssidKey(ssid);
    std::vector<Cursor> v; load(path, v);
    bool changed = false;
    for (size_t i = 0; i < v.size(); ) {
        if (v[i].bssid.equalsIgnoreCase(mac) && v[i].ssid == sk) { v.erase(v.begin() + (long)i); changed = true; }
        else ++i;
    }
    if (changed) save(path, v);
}

} // namespace crackprog
