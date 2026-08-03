---
title: Undercover Mode
parent: System
nav_order: 12
---

# Undercover Mode — `notes` / `nt` and `undercover` / `uc`
{: .no_toc }

<span class="label label-yellow">EXP</span>

A real, SD-backed **Notes app** that doubles as a disguise. To an onlooker the device just looks like a phone's notepad — while your passive tools keep running and logging underneath.

1. TOC
{:toc}

---

## Commands

```
CMD> notes            # standalone Notes cover UI (no silent mode)
CMD> undercover       # silent Notes disguise (the real cover)
CMD> uc set           # set a secret exit passphrase
CMD> uc clear         # remove the exit passphrase
CMD> uc status        # show passphrase + boot-cover state
CMD> uc boot on       # boot directly into Notes (no splash)
CMD> uc boot off      # restore normal boot
CMD> uc panic set     # arm an instant-hide key (press the key to bind it)
CMD> uc panic off     # disable the instant-hide key
```

`notes` just runs the cover UI. `undercover` runs the same UI **and** raises the covert flag: notification sounds and the hidden-SSID beep go silent, and the real AL-ANQA status bar stays hidden. Passive tools (`wguard`, `macwatch`, `espchat`, …) keep running and logging to SD underneath — only the audible/visible tells go quiet.

---

## Using the Notes UI

| Action | Result |
|--------|--------|
| Tap a card / trackball up-down + click | Open a note |
| Tap a line inside a note | Move the cursor there |
| Type / Backspace / Enter | Edit the note in place |
| Trackball up / down (in a note) | Move cursor a line at a time |
| Trackball left / right (in a note) | Move cursor a character at a time (wraps across lines) |
| Tap the save icon | Save to SD (toast: "Saved" or "No SD") |
| Tap the **+** FAB | New note |
| Tap the back chevron | Return to the list |
| Type the secret passphrase (`uc` only) | Exit silently to the CLI |
| Press the panic key (default `@`) | Instantly drop into the cover from anywhere |
| `[q]` | Exit — fallback only, disabled once a passphrase is set |

Notes are stored at `/apps/notes/NNN.txt`. **No SD card** → notes still work for the session (edit/navigate) but nothing persists across a reboot — no crash, just no save.

---

## Security & OPSEC

- The passphrase is stored as `SHA-256(salt + phrase)` in `/config/undercover.conf` — **never** in plaintext.
- On a passphrase-match exit the open note is deliberately **not saved**, so the phrase's own keystrokes (typed into the note before the match is detected) never reach the SD card.
- Typos you backspace-correct while typing the passphrase are handled — the match tracks your edited text.

---

## Boot-cover

`uc boot on` boots the device **straight into the Notes disguise** — no AL-ANQA splash, no CLI flash. SD is read first so the flag is known before anything draws. If a lock PIN is also set (`lock new` + `lock boot on`), the lock screen fires *after* the passphrase exits Notes — so from cold boot through unlock an observer only ever sees Notes.

---

## Panic key

`uc panic set` arms a single key as an instant-hide trigger. Pressing it **anywhere — even mid-command** (a scan, a monitor, an attack) — drops straight into the cover; type the passphrase to return. Default is `@` (Sym+P), chosen to avoid clashing with index targeting like `ps #2`. It only fires once an exit passphrase is set (so there's always a way back); reserved keys (`'`, `q`, space, Enter, Backspace) are rejected. While armed, that key can't be typed in commands (it hides instead); inside the cover it types normally again.

**Touch wake** (undercover only) — one tap wakes a half-dimmed screen; a double-tap (within 500 ms) wakes a fully screen-off device. The plain terminal and standalone `notes` never wake on touch.

> **Still TODO:** a duress/decoy passphrase (a second phrase that opens a clean notepad under coercion) — not currently planned.

---

## See also

- [Lock Screen](lock) — PIN lock that stacks with the cover
- [Home Launcher](home) — the disguise base the cover is built on
