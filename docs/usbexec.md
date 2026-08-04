---
title: BadUSB
lang: en
parent: USB Gadget
nav_order: 3
---

# BadUSB

## `usbexec` / `ux` — DuckyScript Executor

Executes keystroke injection scripts. Compatible with Flipper Zero DuckyScript v1. The
**same script** can be delivered over **USB HID** (plug in the cable) or **BLE HID**
(the T-Deck acts as a Bluetooth keyboard) — see [BadBLE](#badble--ux-over-ble-hid).

```
CMD> ux demo                      # USB, built-in demo (OS-aware — draws the Al-Anqa phoenix)
CMD> ux /apps/badusb/scripts/payload.txt       # USB, run from SD card
CMD> ux ble                       # BLE, interactive menu (mode / target / name / script)
CMD> ux ble demo                  # BLE, fresh keyboard
CMD> ux ble clone 3 payload.txt   # BLE, spoof device #3 from the last sbl scan
CMD> ux ble name "Magic Keyboard" demo   # BLE, custom advertised name
```

Scripts live in `/apps/badusb/scripts/` on the SD card (auto-created on first boot).

### Built-in demo (`ux demo`) — OS-aware

The demo types an **Al-Anqa phoenix** in ASCII art. Over USB it first **detects the host OS**
(NumLock-LED probe, same method as `ux auto`) and opens the right target before drawing:

| OS | What it opens |
|----|---------------|
| **Windows** | `Win+R` → **notepad** |
| **macOS** | `Cmd+Space` (Spotlight) → **TextEdit** → `Cmd+N` |
| **Linux** | `Ctrl+Alt+T` (terminal) → `cat` heredoc that **echoes** the art |

`ux ble demo` can't read the NumLock LED over BLE, so it falls back to the Windows preamble.

### Supported commands

| Command | Description |
|---------|-------------|
| `REM` / `//` | Comment |
| `DELAY <ms>` | Wait |
| `DEFAULT_DELAY <ms>` | Delay after every line |
| `STRING <text>` | Type text |
| `STRINGLN <text>` | Type text + Enter |
| `REPEAT <n>` | Repeat previous line |
| `HOLD <key...>` | Press and hold key(s)/modifier(s) — not released |
| `RELEASE` | Release everything held by `HOLD` |
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

---

## BadBLE — `ux` over BLE HID

Prefix any `ux` invocation with `ble` to run the DuckyScript over **Bluetooth LE HID**
instead of USB. The T-Deck advertises as a BLE keyboard; once a host connects, the same
engine types the payload. No cable required.

```
ux ble                              # interactive menu (recommended)
ux ble demo                         # fresh keyboard, run the demo
ux ble <script>                     # fresh keyboard, run an SD script
ux ble name "<Name>" <script>       # spoof a custom advertised name
ux ble clone <mac|#> <script>       # BLESA MAC-clone (see below)
ux ble clone <#> name "<Name>" ...  # clone MAC, override the name
```

### Interactive mode (`ux ble`)

Bare `ux ble` opens a guided flow, no syntax to remember:

1. **Mode** — Connect (fresh keyboard) or Spoof (clone a bonded device)
2. **Target** (spoof only) — an `sbl`-style paged table showing the **full MAC**
   (`# NAME RSSI AT MAC`, RSSI-sorted, `a`/`l` paging). It shows the **same list as the last
   `sbl` scan** (so devices cross-reference by MAC/index); `[u]` runs a fresh scan. Green
   `*`/`rnd` rows are **actually cloneable** (static-random address); grey `pub`/RPA rows are
   name-only (see the limitation below). Press **`[i]`** to inspect the selected device's GATT
   with `bi` (reveals its real name, services, and security posture) without leaving the picker.
3. **Name** — the spoof **automatically uses the target's real advertised name** (never a
   generic default that would expose the clone). If the target is **nameless**, a short list of
   believable names appears (Keyboard / Magic Keyboard / … / Custom); if you just inspected it
   with `[i]` and it exposed a real name (GAP 0x2A00), that name is offered **pre-selected**
   `(found)` — one keypress, no typing. `q` here goes **back** to the target picker.
4. **Script** — pick a file from `/apps/badusb/scripts/` or the built-in demo

Trackball or `1`/`2` to select, Enter/click to pick, `q` to go back a screen.

> If the BLE host disconnects mid-payload, the run **aborts** (`Host disconnected.`) instead of
> silently typing into a dead link.

### Two modes

| Mode | Command | How it lands on the host |
|------|---------|--------------------------|
| **Connect** (fresh) | `ux ble …` | Advertises a new keyboard; the victim **connects to it** (no PIN, no bond) |
| **Spoof** (clone) | `ux ble clone <mac\|#> …` | Impersonates a bonded keyboard so the host **auto-reconnects** to it |

- **No PIN, no bond** — connect, inject, disconnect, nothing left on the host. (HID-over-GATT
  still requires an *encrypted* link, so it uses Just Works pairing without bonding — it can't
  be fully plaintext.)
- **Random MAC** — a fresh randomized address every `ux ble` session (evil-twin style), so the
  T-Deck leaves no stable Bluetooth identity. A **successful clone** uses the target's exact MAC.
- The `btkbd` (`bk`) keyboard is unaffected — it keeps its stable, bonded address.

### Clone / BLESA — limitations (honest)

Cloning spoofs a bonded keyboard's **MAC + address type + advertised + GAP name** so a host
already paired to it auto-reconnects. It only works when:

1. **The target uses a static-random address** — the ESP32 controller **rejects** public and
   resolvable-private (RPA) addresses, so those can only be **name-only** (the picker shows
   `*` on the cloneable ones; the screen says `MAC spoof FAILED (RPA/public)` otherwise).
2. **The host is BLESA-vulnerable / does not re-authenticate on reconnect** — patched stacks
   demand the original key and drop the link.
3. **The real device is off / out of range** — so the host reconnects to the T-Deck. The ESP32
   cannot jam the real device off the air.

Marked `[EXP]`. It is an isolation/reconnect-auth audit tool, not a guaranteed hijack.
