# bleinfo / bi — BLE GATT Enumeration & Interaction

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

| Key | Action |
|-----|--------|
| `a` / `l` | Previous / next page |
| `n` | Start notify/indicate sniff (30 s) |
| `w` | Write to a writable characteristic |
| `f` | Fuzz a writable characteristic |
| `p` | Toggle pairing / bonding mode |
| `s` | Save GATT tree to SD card |
| `q` | Quit |

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
  0xfb005d~  [W  ] (Control Point)
────────────────────────────────
[q]qt [a/l]pg [n]sniff [w]wr[f]fz [s]save [p]pair
```

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
  1.2s 0xfb005~  48 00 00 00..
  2.4s 0xfb005~  49 00 00 00..
  3.6s 0xfb005~  47 00 00 00..
  4.8s 0x2a37    49 00
────────────────────────────────
subs:2  total:4  [q]stop
```

- **Notify** — device pushes without acknowledgement (fire and forget)
- **Indicate** — device pushes and waits for your ACK before sending next

Both are captured in the same ring buffer. After the session ends, the log is
automatically saved to `/logs/bleinfo/<mac>_sniff.txt` if SD is present.

**Stop early:** press `q`. The partial log is still saved.

---

## Write (`[w]`)

Sends bytes to a writable characteristic. Use for:
- Sending commands (start/stop, config changes)
- Testing input validation
- Triggering response notifications

**Flow:**
1. Press `[w]` — select target char from numbered list
2. Enter payload as hex (`DE AD BE EF`) or ASCII (`hello`) then Enter
3. `bi` reconnects, finds the char by UUID, sends with ACK request
4. Shows `Write OK.` or `Write failed.`

Hex input rules:
- Spaces optional: `DEADBEEF` and `DE AD BE EF` both work
- If input is not valid hex, treated as raw ASCII bytes
- Max 20 bytes

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
Done. Results in /logs/bleinfo/
```

- Timeout per device: 4 seconds
- Press `q` to abort the sweep early
- Saved files: `/logs/bleinfo/<mac>.txt` for each device that responded
- Devices that reject connection or have no services are logged as `failed` on screen

Use this to map all BLE devices in a space without interacting with each manually.

---

## SD Card Output

### GATT structure — `/logs/bleinfo/<mac>.txt`
Saved by `[s]` key or `bi all`:
```
MAC: aa:bb:cc:dd:ee:ff

SVC 0x180a  DeviceInfo
  0x2a29 [R  ] Manufacturer:"Polar"
  0x2a26 [R  ] FirmwareRev:"3.1.1"

SVC 0xfb005c~ []
  0xfb005d~ [W  ] (Control Point)
```

### Notify log — `/logs/bleinfo/<mac>_sniff.txt`
Auto-saved after each sniff session (appended, not overwritten):
```
MAC: aa:bb:cc:dd:ee:ff
--- notify sniff ---
   1.2s 0xfb005~  48 00 00 00..
   2.4s 0xfb005~  49 00 00 00..
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

Run `bi all` after `sbl`. Review `/logs/bleinfo/` on a PC. For each device note:
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
bi 2                   # connect and enumerate
                       # read: Manufacturer, FirmwareRev, SerialNum from DeviceInfo
                       # note: 3 writable chars in proprietary service
[n]                    # sniff — catch heart rate, step count, battery pushes
[s]                    # save GATT tree to SD
[w]                    # write — test the control characteristic with known command
[f] → mode 3           # boundary fuzz the control char
                       # watch for disconnect = crash found
[p] → [n]             # enable pairing, sniff again — more services visible?
q
```

Results in `/logs/bleinfo/`:
- `aa-bb-cc-dd-ee-ff.txt` — full GATT map
- `aa-bb-cc-dd-ee-ff_sniff.txt` — captured sensor data streams
