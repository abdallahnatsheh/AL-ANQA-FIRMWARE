---
title: BadUSB
parent: USB Gadget
nav_order: 3
---

# BadUSB

## `usbexec` / `ux` — DuckyScript Executor

Executes keystroke injection scripts against the connected PC. Compatible with Flipper Zero DuckyScript v1.

```
CMD> ux demo                      # built-in demo (opens Notepad, draws T-Rex)
CMD> ux /badusb/payload.txt       # run from SD card
```

Scripts live in `/badusb/` on the SD card (auto-created on first boot).

### Supported commands

| Command | Description |
|---------|-------------|
| `REM` / `//` | Comment |
| `DELAY <ms>` | Wait |
| `DEFAULT_DELAY <ms>` | Delay after every line |
| `STRING <text>` | Type text |
| `STRINGLN <text>` | Type text + Enter |
| `REPEAT <n>` | Repeat previous line |
| `WAIT_FOR_BUTTON_PRESS` | Pause until trackball click |
| `GUI` `CTRL` `ALT` `SHIFT` | Modifiers |
| `ENTER` `BACKSPACE` `TAB` `ESC` `DEL` | Special keys |
| `UP` `DOWN` `LEFT` `RIGHT` `F1`–`F24` | Navigation + function keys |

### Modifier combos

Both formats are equivalent:

```
CTRL ALT DELETE          # space-separated
CTRL-ALT DELETE          # hyphenated (Flipper Zero format)
```

### Abort

Press `q` on the T-Deck — script stops at the next `DELAY` boundary.
