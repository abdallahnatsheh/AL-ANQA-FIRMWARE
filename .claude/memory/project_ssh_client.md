---
name: SSH client (ssh/sc)
description: Interactive SSH client on LibSSH-ESP32 — architecture, terminal model, the hardware-SHA stability caveat, what's done and planned
type: project
---

`ssh/sc <ip> [user]` — `wifi/tools/sshcon/ssh_client.cpp` + `.h`. Built + field-tested
working (first connect succeeded). Both T-Deck and Plus.

## Library
- **LibSSH-ESP32** `ewpa/LibSSH-ESP32 @ ^5.8.0` — added to `platformio.ini` `lib_deps`
  (a REAL registry dependency; NOT a vendored `lib/` lib like libg722/es7210).
- Port of libssh 0.11.x. Uses the SDK's mbedTLS + ESP32-S3 HW AES/SHA. Just
  `#include "libssh_esp32.h"` + `#include <libssh/libssh.h>`, call `libssh_begin()`
  once per session. No extra build flags needed; it compiled clean.
- Supports ESP32-S3 + Arduino core 2.0.17/3.0.7 (this project is core 2.x).

## Architecture
- **Dedicated ~50 KB FreeRTOS task** (`xTaskCreatePinnedToCore(sshTask,"ssh",51200,...,core 1)`).
  libssh needs way more stack than the 8 KB main-loop task. `runSshCon()` (main task)
  prompts user/pass, creates the task, then blocks `while(!s_taskDone) vTaskDelay(50)`.
  Only the SSH task touches `tft`/`inputHandler` while main waits → no display/kbd race.
- Reuses the existing `cw` STA connection (checks `WiFi.status()==WL_CONNECTED`); does
  NOT re-init WiFi. libssh uses lwIP sockets over it.
- `SSH_OPTIONS_PROCESS_CONFIG=0` (skip nonexistent ~/.ssh/config), TIMEOUT=15s, PTY=`xterm`.
- Password auth (`ssh_userauth_password`). Password wiped from RAM on exit.

## Terminal model
- Scrollback ring `s_buf[SB=120][COLS=52]` + per-cell colour `s_col[][]` (1 byte:
  hi nibble fg, lo nibble bg → `PAL[16]` ANSI-16 RGB565). Default fg = idx 7 (light grey,
  not green). ROWS=13 visible below a 1-line header.
- `termPut()` ANSI/VT100 state machine: SGR colours (incl 256/truecolor skip), cursor
  `H/f/A/B/C/D`, erase `J/K`, `\n \r \b \t`. Full-screen TUIs (vim/htop) still rough.
- Trackpad UP/DOWN scrolls `s_view`; typing snaps to live; CLICK disconnects. Scrollbar
  at x=314 (cyan, yellow when scrolled, `[SCROLL -N]` header). Per-row dirty + 30fps throttle.

## KNOWN stability risk
LibSSH-ESP32 README: disable `CONFIG_MBEDTLS_HARDWARE_SHA` for concurrency stability —
NOT possible with the precompiled Arduino core. SSH crypto + WiFi share the one HW SHA
engine. **Crash during connect/key-exchange → that shared engine is the prime suspect.**

## SD — host profiles IMPLEMENTED (2026-06-13)
`/apps/ssh/hosts.csv` = `name,ip,port,user` (NO password — plaintext on removable card).
`ssh save/list/rm` subcommands + `ssh <name>` resolution (profile first, else token used as
ip/hostname directly). User precedence: arg > profile > prompt. `hostLoadAll/Find/Save/Remove/
List` in ssh_client.cpp; ESP32 `FILE_WRITE`="w" truncates (rewrites whole file). SshParams
gained `port`. Read/write at command time only (WiFi idle) — GDMA-safe.
Autocomplete: `kArgHints` in command_manager.cpp has `{"ssh"/"sc","","save list rm"}` so
`ssh '` lists subcommands. (Note: `sc` is now ssh's short name — the old README autocomplete
example that used `sc` as a prefix was fixed.)
Still planned: `known_hosts` (host-key pinning), `keys/` (pubkey auth) under `/apps/ssh/`.

## Planned next
`<#>` index from `nd` scan · host-key pinning + pubkey auth on SD · Ctrl-C (keyboard can't
emit control chars easily — maybe a trackpad-click menu) · fuller VT100 for TUI apps.
Related: [[next_steps]].
