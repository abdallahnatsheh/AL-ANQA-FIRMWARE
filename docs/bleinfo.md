---
title: BLE Info
parent: Bluetooth
nav_order: 2
---

# BLE Info (`bleinfo` / `bi`) — GATT Enumeration & Interaction

## Overview

`bleinfo` (short: `bi`) is a BLE GATT client built into T-REX. It connects to a
Bluetooth Low Energy device, reads its full service/characteristic tree, decodes
values using standard GATT type descriptors, and provides interactive tools for
passive monitoring and active interaction.

It is the BLE equivalent of a port scanner + banner grabber + packet injector in
one command.

---

## Quick Start

```
sbl               # scan for BLE devices first
bi 0              # connect to device at index 0
bi 3              # connect to device at index 3
bi aa:bb:cc:dd:ee:ff   # connect by raw MAC address
bi all            # enumerate every device from last sbl scan
```

---

## Navigation Keys

**GATT view:**

| Key | Action |
|-----|--------|
| `a` / `l` | Previous / next page |
| `n` | Start notify/indicate sniff (30 s) |
| `r` | Write-cap — replay a captured notification value back to the device |
| `w` | Write to a writable characteristic (reconnects, then writes) |
| `f` | Fuzz a writable characteristic |
| `b` | Auth leak audit — show only flagged/suspicious characteristics |
| `p` | Toggle pairing / bonding mode |
| `s` | Save GATT tree to SD card |
| `q` | Quit |

**Sniff screen (during `[n]`):**

| Key | Action |
|-----|--------|
| `w` | Write to a char **without disconnecting** — response appears in sniff stream |
| `q` | Stop sniff, save log, return to GATT view |

> Keys only appear in the footer when relevant: `[n]` if notify/indicate chars exist, `[r]` if a `.ble` capture file exists, `[b]` if any characteristic was flagged as suspicious.

---

## What It Shows

After connecting, `bi` reads the full GATT structure and displays it paginated:

```
[GATT] aa:bb:cc:dd:ee:ff  1/3
────────────────────────────────
SVC 0x180a   DeviceInfo
  0x2a29  [R  ] Manufacturer:"Polar"
  0x2a24  [R  ] ModelNum:"H10"
  0x2a26  [R  ] FirmwareRev:"3.1.1"
  0x2a25  [R  ] SerialNum:"12345678"
SVC 0x180f   Battery
  0x2a19  [R  ] BattLevel:87
SVC 0xfb005c~ [RWN]
  0xfb005c~  [RWN] 12 00 ..
!0xfb005d~  [W  ] (UnlockKey)
────────────────────────────────
[q]qt [a/l]pg [n]sniff [w]wr[f]fz [b]audit [s]save [p]pair
```

**Risk colour coding** (inline, on every line):
- `!` prefix — HIGH risk (binary 16 or 32 bytes = AES key size, or base64-looking blob)
- `~` prefix — MED risk (hex string ≥ 8 chars = encoded secret, or 4–8 digit value = PIN)
- orange line — LOW risk (writable + sensitive-looking user description)
- white / normal — no flag

**Column breakdown:**

- **UUID** — 16-bit shown as `0x1234`, 128-bit truncated as `12345678~`
- **Props** — `R` readable · `W` writable · `N` notifiable · `I` indicate
- **Value** — decoded using 0x2904 Presentation Format if available, otherwise:
  - Printable ASCII → `"quoted string"`
  - Single byte → `0xXX`
  - Binary → hex dump (`DE AD BE EF..`)
- **Name** — well-known UUIDs resolved to human names (DeviceName, BattLevel, etc.)
- **(desc)** — 0x2901 User Description shown in parens for unnamed write-only chars

---

## Descriptor Reading

`bi` reads two GATT descriptors beyond the value:

### 0x2901 — User Description
Free-text label set by the device manufacturer. Often reveals the purpose of
proprietary 128-bit characteristics that aren't in the GATT spec.

Example: a write-only char on a smart lock might expose `(UnlockCommand)` or
`(SetPIN)` via its User Description, even though you can't read its current value.

### 0x2904 — Presentation Format
Tells `bi` how to decode the raw bytes:

| Type | Decodes as |
|------|-----------|
| `0x04` | uint8 |
| `0x06` | uint16 LE |
| `0x08` | uint32 LE |
| `0x0C` | int8 |
| `0x0D` | int16 LE |
| `0x16` | UTF-8 string |

A battery level without 0x2904 shows `0x57`. With it: `87`. A heart rate without
it shows `48 00`. With it: `72`.

---

## Notify / Indicate Sniff (`[n]`)

Subscribes to every `N` and `I` characteristic simultaneously, then streams live
values for 30 seconds.

```
[SNIFF] aa:bb:cc:dd:ee:ff  00:24
────────────────────────────────
  1.2s 0xfb005~  48 00 00 00 1A 2B
  2.4s 0xfb005~  49 00 00 00 1A 2C
  3.6s 0xfb005~  47 00 00 00 1A 2B
  4.8s 0x2a37    49 00
────────────────────────────────
subs:2 total:4 [w]write [q]stop
```

- **Notify** — device pushes without acknowledgement (fire and forget)
- **Indicate** — device pushes and waits for your ACK before sending next

Both are captured in the same ring buffer. After the session ends, the log is
automatically saved to `/apps/bleinfo/<mac>_sniff.txt` if SD is present.

**Stop early:** press `q`. The partial log is still saved.

### Write during sniff (`[w]` inside sniff screen)

Press `[w]` while sniff is running to send a command **without disconnecting**.
The subscription stays active — any response the device sends appears immediately
in the sniff stream. This is the correct way to probe request/response protocols:

```
[n]   → subscribe to all notify chars
[w]   → pick char → type payload → Enter → "Sent! Watch sniff for reply."
      → response appears on screen within milliseconds
[q]   → stop sniff, auto-save log
```

This avoids the reconnect delay that a separate `[w]` from the GATT view would require.

---

## Write (`[w]`)

Sends bytes to a writable characteristic. Use for:
- Sending commands (start/stop, config changes)
- Testing input validation
- Triggering response notifications

**Flow:**
1. Press `[w]` — select target char from numbered list
2. Enter payload as hex (`DE AD BE EF`) or ASCII (`hello`) then Enter
3. `bi` connects, finds the char by UUID, tries write-with-response then falls back to write-without-response automatically
4. Shows `Write OK.` or `Write failed.`

Hex input rules:
- Spaces optional: `DEADBEEF` and `DE AD BE EF` both work
- If input is not valid hex, treated as raw ASCII bytes
- Max 20 bytes
- **Trackpad left/right** moves the cursor within the input — fix a typo without deleting everything
- **Backspace** deletes the character to the left of the cursor

---

## Fuzz (`[f]`)

Sends a sequence of write requests to a writable characteristic to find input
validation bugs, trigger unexpected state transitions, or crash the device.

**Modes:**

| Mode | Payloads | Use case |
|------|----------|----------|
| `1` seq | `0x00` → `0xFF` (256 writes, 1 byte each) | Full single-byte space coverage |
| `2` rand | 64 writes, 1–4 random bytes (LCG) | Multi-byte protocol fuzzing |
| `3` boundary | `0x00 0x01 0x7F 0x80 0xFE 0xFF` | Off-by-one and overflow testing |

**What to look for:**

- **Error count rises** — device is rejecting bad values (good security)
- **Error count stays 0** — device accepts everything including garbage (bad)
- **"Device disconnected!"** — crash or protection triggered; note the payload
- **Watch screen reacts** — you hit an active control path
- **Error jumps at specific byte** — boundary condition found

Writes are sent every 80 ms. `[q]` stops early.

---

## Auth Leak Audit (`[b]`)

Displays only characteristics that scored a risk flag — a filtered view for rapid triage without scrolling through all services.

```
[AUDIT] aa:bb:cc:dd:ee:ff
────────────────────────────────
! 0xfb005d~ [W  ] (UnlockKey)
  RISK: binary 16B (AES key size?)
~ 0xfb005e~ [RW ] 31 32 33 34 35 36
  RISK: 6-digit value (PIN?)
────────────────────────────────
[q]back
```

**Risk scoring logic:**

| Flag | Condition | Meaning |
|------|-----------|---------|
| `!` HIGH | Value is exactly 16 or 32 bytes of binary | Matches AES-128 / AES-256 key size |
| `!` HIGH | Value looks like a base64-encoded blob (≥16 chars, trailing `=`) | Encoded secret or key material |
| `~` MED | Hex string ≥ 8 hex chars | Could be a key/token in hex encoding |
| `~` MED | 4–8 digit numeric string | Looks like a PIN code |
| LOW | Writable char with a sensitive-sounding User Description | Direct write access to a sensitive control |

Risk is scored at enumeration time. If the footer shows `[b]audit` at least one char was flagged.

Press `q` to return to the main GATT view.

---

## Write-Cap (`[r]wcap`)

Write-cap lets you take a value captured during `[n]` sniff and write it back to a writable characteristic on the same (or a different) device. It is the BLE equivalent of packet replay at the GATT value layer.

**When this works:**
- Custom/proprietary IoT devices that use the same characteristic for both output (notify) and input (write) — e.g., write a target heart-rate zone to the same char the watch uses to push readings
- Devices with simple command protocols where commands are sent as characteristic writes and acknowledged as notifications on the same char

**When this does NOT work:**
- Standard GATT profiles: notify-only chars can't be written (connection will reject the write)
- Challenge-response authentication: even if you replay the right bytes, the server changes its nonce each session
- Devices that validate sequence numbers or timestamps embedded in the payload

**Saved captures:** `[n]` sniff auto-saves a `.ble` file to `/apps/bleinfo/<mac>_replay.ble` after each session. Write-cap reads from this file or from the current in-memory sniff buffer.

**Flow:**
1. Press `[r]` — source picker appears:
   - `[1]` Use current sniff session (in memory)
   - `[2]` Load from SD — lists `.ble` files in `/apps/bleinfo/`
2. Packet picker — shows captured packets, select one
3. Char picker — shows writable characteristics, select target
4. `bi` reconnects and writes the selected bytes to the selected char

---

## `.ble` Capture File Format

Sniff sessions are saved as plain-text `.ble` files:

```
TREX_BLE_REPLAY
MAC aa:bb:cc:dd:ee:ff
TYPE 1
PKT 0x2a37 1234 4800
PKT 0xfb005~ 2500 DEADBEEF01020304
```

- `MAC` — device MAC address
- `TYPE` — address type (0=public, 1=random)
- `PKT <uuid> <elapsed_ms> <hex_bytes>` — one captured notification per line; hex bytes are concatenated without spaces

These files can be loaded back into write-cap on the same or a different T-Deck.

---

## Pairing (`[p]`)

Toggles bonding + MITM + Secure Connections for subsequent reconnects.

When enabled (`[p]PAIR ON` shown in footer), `bi` sets:
- **Bonding** — saves long-term key for future reconnects
- **MITM protection** — requires user interaction to confirm
- **Secure Connections** — uses ECDH key exchange

If the device requests a passkey, a prompt appears:
```
Passkey (6 digits):
> _
```
Type the 6-digit PIN shown on the device.

If numeric comparison is used:
```
Confirm PIN 123456? [y/n]
```

**When to use:** some devices hide services or restrict characteristic access until
bonded. Enable `[p]`, then press `[n]` or `[w]` — `bi` will bond on the next
reconnect and the full service tree becomes accessible.

**When not to use:** recon and passive sniffing. Bonding writes a long-term key to
the device's memory. For pure recon, leave pairing off.

---

## Multi-Device Scan (`bi all`)

Connects to every device from the last `sbl` scan in sequence, enumerates GATT,
and saves each result to SD automatically.

```
bi all
[1/8] aa:bb:cc:dd:ee:ff
ok (3 svcs)
[2/8] 11:22:33:44:55:66
failed
[3/8] ...
Done. Results in /apps/bleinfo/
```

- Timeout per device: 4 seconds
- Press `q` to abort the sweep early
- Saved files: `/apps/bleinfo/<mac>.txt` for each device that responded
- Devices that reject connection or have no services are logged as `failed` on screen

Use this to map all BLE devices in a space without interacting with each manually.

---

## SD Card Output

### GATT structure — `/apps/bleinfo/<mac>.txt`
Saved by `[s]` key or `bi all`:
```
MAC: aa:bb:cc:dd:ee:ff

SVC 0x180a  DeviceInfo
  0x2a29 [R  ] Manufacturer:"Polar"
  0x2a26 [R  ] FirmwareRev:"3.1.1"

SVC 0xfb005c~ []
  0xfb005d~ [W  ] (Control Point)
```

### Notify log — `/apps/bleinfo/<mac>_sniff.txt`
Auto-saved after each sniff session (appended, not overwritten):
```
MAC: aa:bb:cc:dd:ee:ff
--- notify sniff ---
   1.2s 0xfb005~  48 00 00 00..
   2.4s 0xfb005~  49 00 00 00..
```

### Write-cap capture — `/apps/bleinfo/<mac>_replay.ble` ⚠️ Not yet tested
Auto-saved alongside the sniff log. Contains raw bytes for every captured notification.
Can be loaded into `[r]wcap` on any T-Deck to replay the packets:
```
TREX_BLE_REPLAY
MAC aa:bb:cc:dd:ee:ff
TYPE 1
PKT 0x2a37 1234 4800
PKT 0xfb005~ 2500 DEADBEEF01020304
```

---

## Detection Use Cases

### 1. Identify a device without its app

Run `bi <index>`. Read the DeviceInfo service (0x180A) — Manufacturer, ModelNum,
FirmwareRev, SerialNum are usually readable without bonding on consumer devices.
Cross-reference ModelNum and Manufacturer to identify the exact hardware.

Useful when you find an unknown BLE device on a network audit and need to name it
without installing its companion app.

---

### 2. Find hidden control channels

Most BLE devices expose more than their app uses. `bi` shows all characteristics
including those the official app never touches. Look for:
- `[W]` chars the app never writes to — undocumented commands
- `[R]` chars with unusual values — debug flags, internal counters
- `[N]` chars the app never subscribes to — hidden telemetry

Use `[n]` sniff to catch what the device broadcasts before any pairing. Some
devices leak MAC, device name, or sensor data in unauthenticated notifications.

---

### 3. Detect insecure BLE devices

Signs of a poorly secured device (visible from `bi` without touching it):

| Observation | Implication |
|------------|-------------|
| All services visible without bonding | No access control |
| Write chars accept any value (`[f]` errors=0) | No input validation |
| DeviceInfo returns real serial/firmware | No obfuscation |
| Notify pushes data before authentication | Unauthenticated telemetry |
| Same static MAC every boot | Trackable, no randomization |

---

### 4. Map BLE attack surface before a pentest

Run `bi all` after `sbl`. Review `/apps/bleinfo/` on a PC. For each device note:
- Services present → identifies device class
- Write characteristics present → active attack surface
- Notify without bonding → data leakage
- Whether pairing is required → authentication posture

This gives you a structured inventory of every BLE device in range ranked by
attack surface before you connect to any of them interactively.

---

### 5. Validate your own devices

Run `bi` against your own BLE hardware to verify:
- Only expected services are exposed
- Write characteristics reject invalid input (`[f]` mode)
- Sensitive characteristics require bonding (`[p]` mode needed to read them)
- No unintended data leaks via unauthenticated notifications

---

## Address Type Notes

Most modern BLE peripherals use **random addresses** (type 1). Older or embedded
devices use **public addresses** (type 0, burned into hardware).

- `bi <index>` — uses the address type captured during `sbl` scan (correct automatically)
- `bi <mac>` — tries random first, falls back to public if connection fails

If connection keeps failing on a known device, it may have changed its random
address (resolvable private address). Run `sbl` again to get the current address.

---

## Workflow Example: Smart Watch Audit

```
sbl                    # scan — find watch at index 2
bi 2                   # connect and enumerate full GATT tree
                       # DeviceInfo: Manufacturer, Model, FirmwareRev
                       # proprietary services show [W][N] chars
                       # footer shows [b]audit → suspicious values flagged
[b]                    # audit view — see only risk-flagged chars at a glance
[s]                    # save full GATT tree to SD
[n]                    # sniff — all notify/indicate chars subscribed simultaneously
                       # live stream: heart rate, steps, battery, sensor data
  [w]                  # (inside sniff) pick command char → type payload → Enter
                       # response appears in sniff stream within milliseconds
                       # this is how you probe request/response protocols
[q]                    # stop sniff — auto-saves _sniff.txt + _replay.ble to SD
[r]                    # write-cap — load captured packet, replay to writable char
[f] → mode 3           # boundary fuzz the control char
                       # "Device disconnected!" = crash found, note the payload
[p] → [n]             # enable pairing, sniff again — more services visible?
q
```

Results in `/apps/bleinfo/`:
- `aa-bb-cc-dd-ee-ff.txt` — full GATT map with complete hex values and full UUIDs
- `aa-bb-cc-dd-ee-ff_sniff.txt` — captured notification stream with full raw bytes
- `aa-bb-cc-dd-ee-ff_replay.ble` — packet archive for write-cap replay

---

## Protocol Reverse Engineering

`bi` is particularly useful for understanding proprietary BLE protocols with no
public documentation. The sniff+write flow lets you map a device's command
structure without any app or source code.

**Approach:**

1. `[n]` sniff while the device is idle — note baseline packets (keep-alives, sensor polls)
2. Interact with the device physically (press a button, set an alarm, change a setting)
3. Note any new packet that appeared — that packet encodes the action you just took
4. Try sending that packet back on a writable char using `[w]` inside sniff
5. Watch for a response — if the device reacts, you've found the command channel

**Decoding packets:**

Most proprietary BLE protocols follow a pattern:
```
[START] [CMD] [BRAND/SYNC] [LENGTH] [DATA...] [CHECKSUM?]
```

- Fixed bytes that appear in every packet = protocol header/sync bytes
- Bytes that change with physical actions = the meaningful payload
- `0x57` in a health sensor packet at a consistent position = likely a sensor value (e.g. 87 bpm)
- Alternating `00`/`FD` = signed delta or idle/active flag

**Write-without-response:** Many proprietary command chars use write-without-response
(no BLE ACK). `bi` tries both automatically — `Write OK.` means the bytes were
delivered regardless of write type.

**Multiple response channels:** Some devices respond on a different char than the
one you wrote to. If `[w]` shows `Write OK.` but you see a new packet type appear
in the sniff stream on a different UUID, that's the response channel.
