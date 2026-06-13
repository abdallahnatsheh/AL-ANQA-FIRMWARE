---
title: SSH Client
nav_order: 11
---

# SSH Client — `ssh` / `sc`

A real interactive SSH client running on the T-Deck, built on the
[LibSSH-ESP32](https://github.com/ewpa/LibSSH-ESP32) port of libssh (libssh
0.11.x). Full modern crypto — curve25519/ECDH key exchange, AES/ChaCha20
ciphers, ed25519/RSA host keys — using the ESP32-S3's hardware AES/SHA
acceleration. Works on **both** T-Deck and T-Deck Plus.

```
CMD> cw 3                 # connect to WiFi first
CMD> ssh 192.168.1.10 pi  # then SSH in
```

## Usage

```
ssh <ip|name> [user]                  # connect (name = a saved profile)
ssh save <name> <ip> [user] [port]    # save/update a host profile
ssh list                              # list saved profiles
ssh rm <name>                         # delete a profile
```

- Connecting requires an active WiFi STA connection — run `cw <index>` first (the
  client reuses that connection; it doesn't touch WiFi mode). The `save`/`list`/`rm`
  subcommands work without WiFi.
- If `user` is omitted you're prompted for it; the password is always prompted
  (masked).
- Connect + key exchange takes ~1–3 s.

## Saved host profiles

Instead of retyping the IP and user every time, save a named profile:

```
CMD> ssh save nas 192.168.1.50 admin
CMD> ssh nas                          # connects to admin@192.168.1.50, asks password
```

- Stored in `/apps/ssh/hosts.csv` as `name,ip,port,user` — **passwords are never
  saved** (a plaintext file on a removable card); you're always prompted.
- A bare name (`ssh nas`) is resolved against the profile list first; anything not
  found is used directly as an IP/hostname, so `ssh 192.168.1.10` and
  `ssh example.com` still work.
- User precedence: an explicit arg overrides the profile's user, which overrides
  the prompt. Custom ports are kept per profile (`ssh save vps 203.0.113.9 root 2222`).
- **Autocomplete** completes saved names: `ssh '` lists `save list rm` + your host
  names, and `ssh n'` → `ssh nas`.

## Terminal

A colour terminal with scrollback, drawn to the 320×240 screen (≈52×13 chars):

| Input | Action |
|-------|--------|
| Typing | Sent straight to the remote shell |
| **Trackpad up / down** | Scroll through 120 lines of history |
| **Trackpad click** | Disconnect |

- **16-colour ANSI** support — coloured `ls`, prompts, `grep --color` render in
  their real colours; default text is light grey. 256-colour/truecolor codes are
  mapped down or skipped.
- **Scrollbar** on the right edge (cyan; yellow + a `[SCROLL -N]` header tag
  while you're scrolled back). Typing snaps back to live.
- A minimal VT100 engine handles newlines, CR, backspace, tabs, cursor moves and
  erase-line/screen. Good for shells, `ls`, `cat`, `pwd`, `git`, package
  managers. Full-screen TUIs (`vim`, `htop`) render roughly for now.

## How it runs

libssh needs a large stack, so the whole session runs in a dedicated FreeRTOS
task (~50 KB) while the CLI waits for it to finish. The ESP32-S3's PSRAM covers
the libssh heap comfortably.

## Security notes

- Password is wiped from RAM when the session ends.
- **Host-key verification is currently skipped (trust-on-first-use, no pinning).**
  `known_hosts` pinning and public-key auth are planned (under `/apps/ssh/`:
  `known_hosts`, `keys/`).
- There's no Ctrl-C yet (the I2C keyboard can't easily emit control chars) —
  trackpad-click to disconnect is the way out of a long-running remote command.

## Stability

The library docs recommend disabling `CONFIG_MBEDTLS_HARDWARE_SHA` for stability
under concurrency, which isn't possible with the precompiled Arduino core. SSH
crypto and WiFi share the single hardware SHA engine, so if a crash happens
**during the connect/key-exchange phase**, that shared engine is the prime
suspect.

## SD layout

`/apps/ssh/` is created on first boot/format.
- `hosts.csv` — saved host profiles (implemented; `name,ip,port,user`).
- `known_hosts`, `keys/` — host-key pinning and key auth (planned).

Profile reads/writes happen at command time (WiFi idle), not during a live
session, so they're within the safe SD-access window.
