// T-REX — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// home_ui — a BlackBerry/modern-phone "home launcher" undercover disguise, an
// alternative to the Notes-only cover. At a glance it reads as an ordinary phone
// home screen: status bar, live clock + weather hero, and a 4x2 grid of app tiles.
// Only the Notes tile opens anything real (launches runNotesUi()); every other
// tile is cosmetic and opens nothing. Reuses the Notes cover machinery — the same
// PSRAM sprite compositing, baked Noto smooth fonts, touch/trackball handling,
// dim/wake repaint, lock stand-down, and the secret-passphrase rolling-buffer exit.
//
//   standalone=true  (default, called directly by the `home`/`hm` command): on exit
//                    it restores the CLI (setBlocked(false) + printCommandScreen).
//   standalone=false (reserved for an undercover cover-style choice): on exit it
//                    leaves setBlocked(true) and does not touch the CLI.
// Returns true iff it exited via the secret passphrase.

#ifndef HOME_UI_H
#define HOME_UI_H

bool runHomeUi(bool standalone = true);

#endif // HOME_UI_H
