---
title: Lock Screen
lang: en
parent: System
nav_order: 4
---

# Lock Screen

The lock screen protects the T-Deck from being used when left unattended. It can be triggered manually, by a hold gesture from any screen, automatically after a configurable idle timeout, or at every power-on (`lock boot on`).

---

## Locking

### Manual lock

```
CMD> lock
```

Locks immediately without a PIN prompt. No confirmation required.

### Trackpad hold (any screen)

Hold the **trackpad center button for 3 seconds** from any screen — the command prompt, a running scan, wguard view, anywhere. The lock screen appears the moment you release.

> The 3-second hold threshold prevents accidental locks. A brief trackpad tap will not trigger it.

### Idle timeout

```
CMD> lock timeout 120    # lock after 2 minutes of no keypresses
CMD> lock timeout 0      # disable auto-lock
```

Once set, the device locks automatically after the specified number of seconds with no activity. Both keyboard presses and trackpad movements reset the idle timer — the lock only fires when neither input has been used for the full timeout period. The timeout survives reboots (saved to SD).

---

## Unlocking

### No PIN set

The dormant screen shows:

```
  .------.
 /        \
+---------+
|   (.)   |
+---------+

Press [SPACE] x3 to unlock
```

Press **Space three times** in a row to unlock. Any other key between presses resets the counter — the screen shows `(1/3)` / `(2/3)` progress as you go.

> Three presses instead of one prevents accidental unlock from a key being pressed while the device is in a bag or pocket.

### With PIN set

The first keypress activates the PIN entry overlay:

```
PIN:  * * * _

[DEL] delete     [Enter] unlock
```

Type your PIN (any printable characters, up to 16), then press **Enter**. The characters are masked with `*` as you type.

| Key | Action |
|-----|--------|
| Any printable key | Append to PIN buffer |
| `DEL` / `Backspace` | Delete last character |
| `Enter` | Confirm — unlocks if correct |
| `Esc` | Cancel PIN entry, return to dormant screen |

A wrong PIN triggers a 1.5-second red flash before you can try again.

---

## PIN Management

### Set a new PIN

```
CMD> lock new
```

Prompts for a new PIN twice (confirm). Minimum 4 characters. Any keyboard character is valid — letters, numbers, symbols, mixed.

### Change your PIN

```
CMD> lock update
```

Requires your **current PIN** first, then asks for the new PIN twice to confirm. You cannot change the PIN without knowing the old one.

### Remove the PIN (know current PIN)

```
CMD> lock clean
```

Requires your **current PIN**. After removal, the device returns to no-PIN mode (Space ×3 to unlock).

### Remove the PIN (forgot current PIN)

```
CMD> lock wipe
```

Recovery command — see the [Recovery](#recovery-forgot-pin) section below.

### Lock at power-on

```
CMD> lock boot on     # require the PIN every time the device powers on
CMD> lock boot off    # boot straight to the CLI (default)
```

When **on** (and a PIN is set), Al-Anqa shows the lock screen immediately at boot — nobody reaches the CLI without the PIN. The setting is stored in `/config/lockscreen.conf` (`lockonboot=1`). It has no effect until you set a PIN with `lock new`.

### Check status

```
CMD> lock status
```

Shows whether the device is currently locked, whether a PIN is set, whether it locks on boot, and the current timeout value.

---

## Security

PIN is never stored in plaintext. The following happens when you run `lock new`:

1. **Salt** — 8 random bytes generated via `esp_random()`, stored as 16 hex chars
2. **Hash** — SHA-256(`saltHex` + `pin`) computed via mbedTLS, stored as 64 hex chars
3. `hash`, `salt`, `timeout`, `lockonboot` are written to `/config/lockscreen.conf` on the **SD card**

When you enter a PIN to unlock, the same hash is computed and compared. The original PIN cannot be recovered from the stored hash.

**Storage is the SD card by design.** The PIN deliberately does *not* live in the device's internal flash — keeping it on the card is what makes the forgot-PIN recovery simple (remove the card) and keeps the card PC-readable. The trade-off: a device booted **without** its card has no PIN to load and comes up unlocked.

**Trackball events are fully blocked while locked.** Rolling or clicking the trackball does nothing until the screen is unlocked via keyboard.

> **Scope of protection.** This protects the *running device* from someone who picks it up. It does **not** protect the SD card's contents: the card is plain FAT, readable in any PC, and removing it disables the lock. Don't treat the lock as encryption.

---

## Recovery (Forgot PIN)

### Easiest — remove the SD card

1. **Power off** the T-Deck
2. **Remove the SD card** (the PIN lives only on the card)
3. **Power on** — no config to load, so the device boots **unlocked**

Re-insert the card and run `lock new` (or `lock clean`/`lock wipe`) to set a fresh PIN. Note this also disables the lock for anyone who removes the card — that's the accepted trade-off of SD-only storage.

### Or — reset flag on the SD card

If you'd rather not pull the card, recover from a PC:

1. **Power off**, remove the SD card, put it in a computer.
2. Open `/config/lockscreen.conf` in a text editor and add a line:
   ```
   reset=1
   ```
3. Save, re-insert the SD, **power on**.

On that boot Al-Anqa clears the PIN, **rewrites the file without the flag** (one-shot — it can't keep wiping), and boots unlocked. Your `timeout` and `lockonboot` settings are preserved; just run `lock new` to set a fresh PIN.

> Both recovery paths are owner convenience, not extra security — anyone with the card can do them. The lock deters someone who grabs the running device; it is not designed to resist an attacker who has the SD card.

---

## Config File

`/config/lockscreen.conf` — key=value, written by the `lock` command, lives on the **SD card**.

```
timeout=120
hash=a3f2...64hexchars...
salt=b7c1...16hexchars...
lockonboot=1
```

| Key | Meaning |
|-----|---------|
| `timeout` | Idle seconds before auto-lock (`0` = off) |
| `hash` | SHA-256(salt + PIN), 64 hex chars |
| `salt` | 8 random bytes, 16 hex chars |
| `lockonboot` | `1` = lock screen shown at power-on |
| `reset` | Add `reset=1` manually to clear the PIN on next boot (one-shot; removed automatically) |

Don't edit `hash`/`salt` by hand. The fields meant for manual editing are `reset=1` (forgot-PIN recovery) and, if you like, `lockonboot`.
