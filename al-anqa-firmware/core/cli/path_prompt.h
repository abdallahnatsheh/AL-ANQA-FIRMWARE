#pragma once
#include <stddef.h>
// Reusable single-line PATH input with '-key autocomplete, for interactive apps
// that need the user to type an SD path (e.g. cc's "type a wordlist dir" option).
// Autocompletion reuses SDCardManager::listCompletions — the SAME directory walk
// the CLI's own tab-complete uses — so there is no duplicated listing logic
// (rule 5b). Key scheme follows the text-editor's promptLine (the codebase's
// text-input precedent): ' (KEY_AUTOCOMPLETE) completes, Enter confirms, and
// cancel is trackball CLICK or Enter on an empty line — deliberately NOT 'q',
// so every printable char incl. 'q' stays typeable in a path. Lock-aware.

namespace pathprompt {

// Prompt for a path with the given caption. `out` (capacity `cap`) receives the
// typed path (relative to the cwd or absolute). Returns true on Enter with a
// non-empty path, false on cancel.
bool prompt(const char* label, char* out, size_t cap);

} // namespace pathprompt
