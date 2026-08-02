// AL-ANQA — offensive security firmware for LilyGo T-DECK
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abdallah Natsheh
//
// edit / ed — on-device nano-style text editor for SD files.

#ifndef TEXT_EDITOR_H
#define TEXT_EDITOR_H

// Open <args> (a path, resolved against the SD cwd) in the editor.
// Nonexistent path => new empty buffer, created on first save.
void runEditor(char* args);

#endif // TEXT_EDITOR_H
