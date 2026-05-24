---
title: Lock Screen
nav_order: 7
---

# Lock Screen

The lock screen protects the T-Deck from being used when left unattended. It can be triggered manually, by a hold gesture from any screen, or automatically after a configurable idle timeout.

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

### Check status

```
CMD> lock status
```

Shows whether the device is currently locked, whether a PIN is set, and the current timeout value.

---

## Security

PIN is never stored in plaintext. The following happens when you run `lock new`:

1. **Salt** — 8 random bytes generated via `esp_random()`, stored as 16 hex chars
2. **Hash** — SHA-256(`saltHex` + `pin`) computed via mbedTLS, stored as 64 hex chars
3. Both written to `/lockscreen.conf` on the SD card

When you enter a PIN to unlock, the same hash is computed and compared. The original PIN cannot be recovered from the stored hash.

**Trackball events are fully blocked while locked.** Rolling or clicking the trackball does nothing until the screen is unlocked via keyboard.

---

## Recovery (Forgot PIN)

### Step 1 — Get back in

1. **Power off** the T-Deck
2. **Remove the SD card**
3. **Power on** — T-Rex boots with no PIN loaded (nothing to read from SD)
4. **Press Space ×3** to unlock

> **Why hot-pulling the SD won't work:** The PIN hash is loaded into RAM at boot. Removing the SD while already locked does not clear the hash — reboot is required. This prevents an attacker from yanking the card while you're away.

### Step 2 — Clear the old PIN

Once unlocked (no PIN loaded), run:

```
CMD> lock wipe
```

Two scenarios:

**SD card is accessible** (you re-inserted it while running):
```
PIN config removed from SD.
Run 'lock new' to set a new PIN.
```
Done — the config file is deleted immediately.

**SD card is still absent**:
```
Wipe scheduled in NVS.
Insert SD card and reboot to apply.
```
T-Rex writes a wipe flag to internal NVS flash. Power off → insert SD → power on. On the next boot T-Rex detects the flag, deletes `/lockscreen.conf`, clears the flag, and starts with no PIN.

### Step 3 — Set a new PIN

```
CMD> lock new
```

> `lock wipe` only works when no PIN is loaded in memory (i.e., you booted without SD). If a PIN is active, `lock wipe` is blocked — this prevents bypassing the lock without first proving physical access via the SD-removal step.

---

## Config File

`/lockscreen.conf` — key=value format, written by the `lock` command.

```
timeout=120
hash=a3f2...64hexchars...
salt=b7c1...16hexchars...
```

Do not edit this file manually. If the hash or salt are corrupted, recovery is the same as a forgotten PIN (remove SD + reboot).
