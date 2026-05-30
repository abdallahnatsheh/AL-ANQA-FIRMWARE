---
title: Help & Manual
parent: System
nav_order: 1
---

# Help & Manual

## `help` / `hlp` — Command List

```
CMD> help            # all commands grouped by category
CMD> help deauth     # detail for one command
CMD> hlp da
```

Commands are grouped by category (System, WiFi, Network, Bluetooth, SD Card, Diagnostics), paginated at 5 per page.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

---

## `man` / `mn` — Manual Pages

```
CMD> man deauth
CMD> mn da
```

Full on-device manual for any command — syntax, steps, keys, options, files, warnings. Short names work (`mn da` = `mn deauth`).

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

---

## `show` / `sh` — Re-display Last Scan

```
CMD> show wifi      # last scanwifi result
CMD> show ble       # last scanblue result
CMD> show hosts     # last netdiscover result
```

Re-renders the last cached scan table without running a new scan. Shows `No scan data` if that scan has not been run yet in this session.

---

## `clear` / `clr` — Clear Screen

```
CMD> clear
```

Clears the output area and resets the prompt.

---

## `MATRIX` / `matrix` — Matrix Animation

```
CMD> MATRIX
```

Launches the Matrix digital rain animation. Press `q` to exit.
