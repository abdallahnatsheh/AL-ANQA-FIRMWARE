---
title: SD Commands
parent: System
nav_order: 7
---

# SD Commands

```
CMD> sdinfo                  # SD card type, size, usage
CMD> sdls [path]             # list directory (default: current)
CMD> cd <path|..>            # change working directory
CMD> cat <path>              # read file — scrollable viewer
CMD> rm <path>               # delete file
CMD> sdformat [init]         # format SD to FAT32 (WARNING: destroys all data)
```

### Notes

- **`cat`**: loads up to 400 lines, strips Windows `\r`, scrollable with a cyan scrollbar. Trackball up/down scrolls.
- **`sdformat`**: prompts for confirmation before formatting. `sdf init` formats and re-creates the full directory structure in one step.
- **Tab-complete**: works for `sdls`, `cd`, `cat`, and `rm` — press `'` to complete paths.

See [SD Card](sdcard.md) for the full file layout.
