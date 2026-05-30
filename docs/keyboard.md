---
title: Keyboard Reference
nav_order: 3
---

# Keyboard Reference

The T-Deck has a physical **QWERTY keyboard** that communicates with the ESP32-S3 via I2C. It sends one ASCII byte per keypress — the keyboard firmware handles modifier keys and resolves the final character before T-Rex sees it.

---

## Physical Layout

```
┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
│ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │
├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
│ A │ S │ D │ F │ G │ H │ J │ K │ L │ ⏎ │
├───┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴┬──┴───┤
│SYM │ Z │ X │ C │ V │ B │ N │ M │  ⌫  │
├────┴──┬┴───┴───┴───┴───┴───┴┬──┴──────┤
│  ALT  │        SPACE        │  (mic)  │
└───────┴─────────────────────┴─────────┘
```

> The **T-Deck Plus** has a microphone button on the bottom row. On the standard T-Deck it may be absent or replaced by a blank key.

---

## Modifier Keys

### SYM — Special Characters

Hold **SYM** and press a letter to type the symbol shown on the key's lower face. The secondary characters are printed on the physical keycaps — check your device for the exact layout.

**Confirmed from firmware:**

| Combo | Character | Used for |
|-------|-----------|---------|
| SYM + K | `'` (apostrophe) | **Autocomplete trigger** in the CLI |

The physical keys show all other Sym characters directly — look at the lower-right of each keycap.

### ALT — Numbers and Punctuation

The **ALT** key accesses numbers and additional punctuation. On most T-Deck keyboards the top row keys (Q through P) double as 1–0 when combined with ALT.

Check your physical keyboard — the secondary characters are labeled on the keycap faces.

---

## Special Keys

| Key | Sends | Effect in CLI |
|-----|-------|--------------|
| `⏎` Enter | Execute | Runs the current command |
| `⌫` Backspace | `\b` | Deletes the character to the left of the cursor |
| **Hold** Backspace 1.5s | repeated `\b` | Auto-delete at ~16 chars/sec |
| SYM + K | `'` | Triggers autocomplete |
| Space | ` ` | Inserts a space |

---

## Trackball

The trackball acts as both a mouse (in USB/BLE keyboard mode) and a CLI navigation control.

### At the command prompt

| Action | Effect |
|--------|--------|
| Roll **left** | Move cursor one position left within the typed command |
| Roll **right** | Move cursor one position right |
| Roll **up** | Load previous command from history |
| Roll **down** | Load next command from history (or restore draft) |
| **Click** | Execute command (same as Enter) |
| **Double-click** | Toggle screen off / on |
| **Hold 3 seconds** | Lock screen immediately (works from any screen) |

### In paginated views (scan results, help, man pages)

| Action | Effect |
|--------|--------|
| Roll **up** / `a` | Previous page |
| Roll **down** / `l` | Next page |
| `q` | Quit / close |

### In the `cat` file viewer

| Action | Effect |
|--------|--------|
| Roll **up** | Scroll up |
| Roll **down** | Scroll down |
| `q` | Quit |

---

## CLI Shortcuts

### Autocomplete — `'` (SYM + K)

Press `'` at any point while typing to trigger autocomplete:

| Situation | What happens |
|-----------|-------------|
| One match | Word is completed + space added |
| Multiple matches | Up to 8 options listed below the prompt |
| Common prefix | Prefix is filled, cursor advances |
| No match | Nothing happens |

Works for **command names** and **file paths** when using `sdls`, `cd`, `cat`, `rm`.

Example:
```
CMD> sca'       → completes to "scanwifi " or lists: scanwifi, scanblue
CMD> cat /lo'   → completes to "cat /logs/"
```

### Command History

The last **16 commands** are stored in a ring buffer in RAM (not persisted across reboots).

| Action | Effect |
|--------|--------|
| Trackball **up** | Load previous command. Your in-progress text is saved automatically. |
| Trackball **down** | Move forward in history. Pressing past the most recent entry restores your original in-progress text. |

Duplicate consecutive commands are not stored — pressing Enter on the same command twice only stores one entry.

### Cursor Editing

You can edit anywhere in your command without retyping from scratch:

1. Roll the trackball left/right to position the cursor
2. Type to **insert** characters at the cursor position
3. Backspace deletes the character to the **left** of the cursor

This is the same as a standard terminal line editor.

---

## Key Behavior in Apps

Most interactive commands (scans, attacks, viewers) respond to:

| Key | Typical effect |
|-----|---------------|
| `q` / `Q` | Quit / stop the current command |
| `l` | Next page |
| `a` | Previous page |
| `u` | Re-scan / refresh |
| `s` | Save to SD card |
| `y` / `n` | Yes / No confirmation |
| Trackball up/down | Scroll (in viewers) or navigate history (at prompt) |

---

## Typing Tips

- **Uppercase**: Hold SYM while typing letters, or press SYM then ALT — check your keyboard silkscreen for the exact behavior
- **Numbers**: Use ALT + top-row keys (or check if your keyboard has a number row)
- **Tab**: Some keyboards map SYM + Space to Tab — otherwise use the `'` autocomplete key to navigate paths
- **Fixing a typo**: Roll the trackball to the typo position and type over it — insert mode is always active
- **Long commands**: Use autocomplete (`'`) to avoid typing full paths and command names
