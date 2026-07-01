// T-REX — offensive security firmware for LilyGo T-DECK
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

void runNotesUi();

#endif // NOTES_UI_H
