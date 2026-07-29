// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// wps / wps — WPS recon + assisted attack (WiFi).
//
// RECON: parses the WPS Information Element out of the target AP's beacon (via
// promiscuous capture) — WPS version, AP-Setup-Locked state, config methods, and
// the device manufacturer / model / name / serial it leaks. Plus an offline
// candidate-PIN calculator (default + ComputePIN from the BSSID).
//
// ATTACK: `wps pbc <idx>` attempts a WPS push-button connect (only works while the
// AP's WPS button is active) → recovers SSID+PSK via the enrollee success event.
//
// HONEST LIMIT: an automated WPS PIN / Pixie-Dust attack is NOT possible on the
// ESP32 — the closed WiFi stack won't let you supply/test a PIN or act as a
// registrar (esp_wps_config_t has no PIN field; registrar mode is unsupported).
// The PIN calculator only *computes* candidates to try on a Reaver-capable radio.
//
// Own networks only. Free-function entry (wardrive/isoscan pattern).
#pragma once

void runWps(char* args);
