// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// notes_ui — the undercover "Notes cover" UI (Phase 2 of PLAN-undercover-touch).
// THIS IS A UI TEST ONLY: renders the disguise (Notes list + note detail) so the
// look/feel + touch/trackball navigation can be iterated on hardware. It does NOT
// yet carry any undercover machinery — no g_covert wiring, no secret-passphrase
// exit, no duress/decoy, no SD notes. Sample notes are hardcoded. Exit with `q`.

#ifndef NOTES_UI_H
#define NOTES_UI_H

// Runs the Notes cover UI (blocks until exit).
//   standalone=true  (default, called directly by the `notes`/`undercover` command):
//                    on exit it restores the CLI (setBlocked(false) + printCommandScreen).
//   standalone=false (called from the Home launcher, which owns the screen): on exit it
//                    tears down its own sprite/fonts but leaves setBlocked(true) and does
//                    NOT touch the CLI, so the caller can repaint its own UI without a flash.
// Returns true iff it exited via the secret passphrase — the caller (Home) uses this to
// propagate the covert exit all the way to the CLI instead of returning to the launcher.
bool runNotesUi(bool standalone = true);

#endif // NOTES_UI_H
