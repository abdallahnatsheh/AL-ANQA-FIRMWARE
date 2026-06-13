// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh

#ifndef SSH_CLIENT_H
#define SSH_CLIENT_H

// Interactive SSH client (libssh / LibSSH-ESP32).
//   ssh <ip> [user]
// Requires an active WiFi STA connection (use `cw` first). Password auth,
// PTY shell, basic terminal rendering. Trackpad CLICK disconnects.
void runSshCon(char* args);

#endif // SSH_CLIENT_H
