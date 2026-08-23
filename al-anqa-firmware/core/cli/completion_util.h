#pragma once
#include <string.h>
// Longest-common-prefix over the first `n` fixed-width completion rows. Shared by
// the CLI tab-complete (command_manager) and the interactive path prompt
// (path_prompt) so the common-prefix fill lives in ONE place (rule 5b). Templated
// on the row width so it serves both match tables (command names [128], file
// names [64]) without a copy.
namespace completion {

template <int W>
inline int commonPrefixLen(const char m[][W], int n) {
    if (n <= 0) return 0;
    int L = (int)strlen(m[0]);
    for (int i = 1; i < n; i++) {
        int j = 0;
        while (j < L && m[i][j] && m[0][j] == m[i][j]) j++;
        L = j;
    }
    return L;
}

} // namespace completion
