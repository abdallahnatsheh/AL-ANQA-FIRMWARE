---
title: Troubleshooting
nav_order: 3
---

# Troubleshooting

Common problems and how to fix them.

---

## Flashing & Upload

### "Chip stopped responding" or upload times out

The ESP32-S3 was not in download mode when the upload started.

**Fix:**
1. Hold the **trackball button** (GPIO0)
2. While holding, plug in the USB cable
3. Release the trackball button
4. Click **Upload** in VSCode / PlatformIO

The device is now in download mode and will accept the firmware.

---

### PlatformIO can't find the serial port

**Windows:** Open Device Manager → Ports (COM & LPT). If no COM port appears, the cable is charge-only or the USB driver is missing.

**Linux / macOS:** Run `ls /dev/ttyUSB*` or `ls /dev/ttyACM*` before and after plugging in — the new entry is your port.

**Fix:**
- Try a different USB cable (must be a **data cable**, not charge-only)
- Try a different USB port on your PC
- Install CP2102 or CH340 driver if your OS doesn't recognise the device

---

### Upload succeeds but the device doesn't boot

**Check:** did you select the correct environment?

| Environment | Device |
|-------------|--------|
| `env:T-Deck` | Standard T-Deck |
| `env:T-Deck-Plus` | T-Deck Plus (with GPS) |

Flashing the wrong env won't brick the device — just reflash with the correct one.

---

## SD Card

### `sdinfo` shows nothing / "No SD card"

- The SD card must be **inserted before power-on** — hot-insert is not supported
- Card must be **FAT32** formatted (exFAT and NTFS are not supported)
- Reformat with `sdf init` if unsure

---

### `sdls` or `cat` hangs

The card may be responding slowly or have filesystem errors.

**Fix:** Remove the card, format it to FAT32 on a PC, reinsert before power-on, then run `sdf init`.

---

### WiFi credentials / logs not saving between reboots

The SD card is required for persistence. NVS (on-device flash) stores WiFi credentials but logs, scan history, and config files (`lockscreen.conf`, `notif.conf`, etc.) all require the SD card.

**Fix:** Insert an SD card and run `sdf init` to create the directory structure.

---

## WiFi

### `sw` (scanwifi) shows no networks

- Make sure you're not in an area with no WiFi
- BLE was running — BLE and WiFi share one antenna. Stop any BLE operation first
- Try rescanning: `sw` or press `u` while in the scan view

---

### `cw` connects but IP is 0.0.0.0

The DHCP lease timed out or the AP rejected the connection.

**Fix:** Run `cw` again. If the issue persists, check `info` page 2 for RSSI — a very weak signal (< −85 dBm) can cause connection failures.

---

### `wpasniff` never captures M1+M2

- No clients are currently connected to the target AP — deauths are sent but nobody re-authenticates
- The target AP may be on a non-standard channel — pass the channel explicitly: `ws AA:BB:CC:DD:EE:FF 11`
- The T-Deck is too far from the target — RSSI matters for capturing EAPOL frames

**Fix:** Wait near the target or use `wifimon` (`wm <ch>`) to confirm traffic is visible before running `wpasniff`.

---

### WPA crack never succeeds

The password is not in the wordlist.

**Fix:**
1. Add a custom wordlist to `/wordlist.txt` on the SD card (one password per line)
2. Copy the `.cap` file from `/logs/hs/<BSSID>.cap` to a PC using `usbmsc` and crack offline with hashcat or aircrack-ng — GPU cracking is orders of magnitude faster

---

### `wguard` shows no events

- The AP you selected may be on a different channel than expected — verify with `sw` first
- Use `wm <ch>` (WiFi monitor) to confirm you can see traffic on the target channel before starting wguard
- There may simply be no attacks happening — that is the expected (good) outcome

---

## Bluetooth

### `sbl` finds no devices

BLE and WiFi share one antenna — they cannot run simultaneously.

**Fix:** Stop any active WiFi scan or attack before running `sbl`.

---

### `buddy` / `btkbd` won't appear on Windows Bluetooth

- The device advertises as `Claude-XXXX` (buddy) or `T-REX-KBD` (btkbd) — search for these names in Windows BT settings
- If Windows shows "auth errors" or immediately disconnects: remove the device from Windows BT settings and re-pair from scratch

---

### `btkbd` / `buddy` bond issues after switching between them

They use separate BLE addresses (suffix `:CB` for btkbd, `:BD` for buddy) so Windows should bond them independently. If bonds are mixed up, remove both devices from Windows BT settings and pair each one fresh.

---

## GPS (T-Deck Plus only)

### GPS never gets a fix (`GPS:srch` stays yellow)

- GPS needs **clear sky view** — it will not fix indoors or near tall buildings
- Cold start takes **~4 minutes** — this is normal
- If `GPS:srch` shows a rising character count, the module is alive and receiving signals

**Fix:** Go outdoors, wait 4 minutes. Once fixed (icon turns green), the fix is warm for the rest of the session.

---

### `trackme` says "GPS: none"

The firmware was compiled for the standard T-Deck (`env:T-Deck`). GPS-dependent features require `env:T-Deck-Plus`.

---

## Lock Screen

### Forgot PIN

See the [Recovery (Forgot PIN)](lock#recovery-forgot-pin) section in the Lock Screen guide.

**Short version:** Power off → remove SD card → power on → press Space three times to unlock → run `lock wipe` → set a new PIN with `lock new`.

---

### Lock screen appears after every boot

Auto-lock timeout is set very short.

**Fix:**
```
CMD> lock timeout 0     # disable auto-lock
CMD> lock timeout 300   # or set to 5 minutes
```

---

## Display

### Screen is black / nothing visible

Power save has turned the screen off.

**Fix:** Press any key or roll the trackball — the screen restores immediately.

---

### Screen has garbage pixels or freezes after a crash

The ESP32-S3 rebooted mid-draw and left the display in a corrupt state.

**Fix:** Run `clear` (`clr`) to reset the display. If the display is completely frozen, press the physical reset button on the T-Deck.

---

## Audio (T-Deck Plus)

### `spktest` plays no sound

- Volume may be zero — run `nf vol 80` to set notification volume
- Check that the physical speaker connector is seated
- Run `spktest` and press keys `1`–`6` — these bypass the volume setting entirely and play at full amplitude; if there is still no sound, the speaker hardware connection is the issue

---

### Notifications are silent during attacks

Notifications may be disabled or volume is zero.

**Fix:**
```
CMD> nf status          # check what's enabled
CMD> nf on              # enable all levels
CMD> nf vol 70          # set volume
```
