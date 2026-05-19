---
name: project_shell_ux
description: "Shell UX features — ls improvements, cd CWD navigation, command history (ring buffer), tab autocomplete (Sym+K), and sdrm/matchesCmd bug fix"
metadata: 
  node_type: memory
  type: project
  originSessionId: b2fd8bd3-c9c7-4514-9e2b-eadeb2e1fb6d
---

## Shell UX features (implemented 2026-05-19)

### `ls` — Linux-style directory listing
- Non-recursive, shows current CWD at top in yellow
- Dirs in cyan with trailing `/`, files in white with size (`%uB` or `%uKB`)
- Paginated: 8 entries per page, "any key / q" prompt between pages
- `sdls` or `ls [path]` — no arg → lists CWD, arg → resolves relative or absolute

### `cd` — CWD navigation
- `cd <dir>` / `cd ..` / `cd /` — updates `_cwd` in `SDCardManager`
- All file commands (`sdr`, `srm`, `ux`, `sdls`) call `resolvePath()` internally — CWD is automatically applied
- `resolvePath()` handles absolute paths (starting `/`), relative paths, and `..` segments via parts[12][64] stack
- CWD stored in `SDCardManager::_cwd[128]`, default `/`

### Command history (16-entry ring buffer)
- `_hist[16][128]`, `_histHead`, `_histCount`, `_histNav`, `_histSaved`
- Trackpad UP → older entries; trackpad DOWN → newer / restore in-progress command
- Skips empty commands and consecutive duplicates on execute
- `_histNav = -1` means not navigating; save current line to `_histSaved` on first UP

### Tab autocomplete (`'` key, Sym+K → 0x27)
- Triggered by `KEY_AUTOCOMPLETE '\''` defined in `input_handling.h`
- `wordStart == 0`: complete command names (prefix match on `commands[i].name`)
- `wordStart > 0`: complete file/dir paths from SD via `sdCardManager.listCompletions()`
- `CompType` per command controls what is suggested:
  - `COMP_ANY` → files + dirs (ls)
  - `COMP_DIR` → dirs only (cd)
  - `COMP_FILE` → files only (sdr, srm, ux)
  - `COMP_NONE` → no file suggestions (network cmds etc.)
- Common prefix insertion (Linux-style): fills as much as is unambiguous
- Single match → inserts + space; then shows CWD listing filtered by CompType
- Multiple matches → shows up to 8 in colour (cyan=dir, green=cmd), `... N more` if excess
- Typing a prefix like `scan` + `'` shows `scanwifi` and `scanblue`

### `Utils::matchesCmd` — command dispatch bug fix
- **Bug**: `sdrm` was matched by `sdread` (short name `sdr`) because `startsWith("sdrm","sdr")` = true → args became `"m"` → `/m not found`
- **Fix**: added `Utils::matchesCmd(str, prefix)` → requires `str[strlen(prefix)]` to be `'\0'` or `' '`
- `executeCommand()` now uses `matchesCmd` instead of `startsWith` for command dispatch

**Why:** `startsWith` is correct for prefix checks generally; `matchesCmd` is the word-boundary variant needed for command matching.

**How to apply:** Always use `matchesCmd` in `executeCommand()` dispatch loop. Use `startsWith` only for non-command string prefix checks.
