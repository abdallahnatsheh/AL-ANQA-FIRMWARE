---
title: SD Commands
lang: en
parent: System
nav_order: 7
---

# SD Commands

```
CMD> sdinfo                  # SD card type, size, usage
CMD> sdls [path]             # list directory (default: current)
CMD> cd <path|..>            # change working directory
CMD> cat <path>              # read file — scrollable viewer
CMD> edit <path>             # nano-style text editor (alias: ed)
CMD> rm <path>               # delete file
CMD> rm -d <dir>             # delete directory + all contents (asks y/N)
CMD> sdformat [init]         # format SD to FAT32 (WARNING: destroys all data)
```

### Notes

- **Paths are relative to your current directory** (set with `cd`) — no leading `/` needed. `cat creds.csv`, `edit wordlist.txt`, `rm -d oldtool` all work from wherever you are. Absolute paths (`/apps/...`) are also accepted.
- **`cat`**: loads up to 400 lines, strips Windows `\r`, scrollable with a cyan scrollbar. Trackball up/down scrolls.
- **`edit` / `ed`**: on-device text editor for SD files (handy for tweaking wordlists, captive-portal HTML, configs in the field). Opening a path that doesn't exist starts a new buffer that's written on the first save — and any missing parent folders are created automatically (e.g. `ed notes/today.txt` works even if `notes/` doesn't exist yet). Loads up to 500 lines; a larger file opens **read-only** so it can't be truncated on save.
  - **Type** to insert, **Backspace** to delete (hold to auto-repeat), **Enter** splits the line (with **auto-indent** — the new line keeps the current line's leading whitespace).
  - **Trackball** moves the cursor (Up/Down/Left/Right, wrapping across line ends). **Roll fast** to accelerate — a quick roll jumps several lines/chars at once, so paging through big files is fast.
  - **Click** opens the command **menu**: Save · Save As · Find · Go to line · Top · Bottom · Undo · Cut line · Paste line · Exit. (There is no `q` quit — `q` is a normal character — so exit via the menu.)
  - **Undo** reverts the last change (single level — coalesced per typing/delete run).
  - Exiting with unsaved changes prompts: `[s]` save & exit, `[d]` discard, click = cancel.
- **`rm`**: deletes a file with no confirmation. **`rm -d <dir>`** removes a directory **and everything inside it** (recursive) — this one asks `y/N` first, and refuses to delete the card root or the current working directory (use `cd` out first). Deleting a plain directory without `-d` is refused.
- **`sdformat`**: prompts for confirmation before formatting. `sdf init` formats and re-creates the full directory structure in one step.
- **Tab-complete**: works for `sdls`, `cd`, `cat`, `edit`, and `rm` — press `'` to complete paths. `rm` now completes **both files and directories** (so you can tab into nested folders and target a dir for `rm -d`).

See [SD Card](sdcard.md) for the full file layout.
