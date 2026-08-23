#pragma once
#include <Arduino.h>
#include <vector>
#include "cap_parse.h"    // capparse::CrackJob

// Background WPA/WPA2 dictionary crack for `cc bg`. A COOPERATIVE, time-sliced
// crack pumped from the main loop's getKeyboardInput() hook (the wg-bg / pwn-idle
// pattern) — single-threaded, so there is NO concurrent SHA and it sidesteps the
// HW-SHA concurrency hazard the ssh client documents. It keeps cracking while you
// use the CLI AND under the undercover cover (the point: grind wordlists in public
// behind the disguise). A win is silent under the cover (NotificationManager
// self-gates on g_covert) and always lands in cracked.csv. Reuses the shared
// resume cursor, so stop / reboot / yielding to WiFi loses nothing.
//
// GDMA-safe: the poll skips its SD slice whenever WiFi is in a DMA-heavy mode
// (promiscuous / AP), so it coexists with wg-bg etc. without touching them.

bool        startCapcrackBg(const capparse::CrackJob& job,
                            const std::vector<String>& wlFiles, bool useBuiltin);
void        stopCapcrackBg();        // save cursor + tear down (also called before a foreground crack)
void        pollCapcrackBg();        // hooked in getKeyboardInput()
bool        capcrackBgActive();
const char* capcrackBgSsid();        // SSID of the running job (for the status line)
uint32_t    capcrackBgTried();       // candidates tried so far
const char* capcrackBgStatus();      // last/current outcome: running / found / done: not found / stopped
const char* capcrackBgCurrent();     // current candidate being tried (for the live monitor)
int         capcrackBgPct();         // position through the current wordlist (0-100), -1 = built-in/none
