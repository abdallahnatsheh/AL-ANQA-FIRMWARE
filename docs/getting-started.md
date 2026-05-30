---
title: Getting Started
nav_order: 2
---

# Getting Started

> This guide takes you from a fresh T-Deck to your first scan. It covers flashing, SD setup, navigating the CLI, and the first commands to run. Takes about 10 minutes.

---

## What You Need

| Item | Notes |
|------|-------|
| LilyGo T-Deck or T-Deck Plus | T-Deck Plus adds GPS + speaker |
| USB-C data cable | Data cable, **not** a charge-only cable |
| microSD card | Any size, FAT32 formatted |
| [VSCode](https://code.visualstudio.com) + [PlatformIO](https://platformio.org) extension | For building and flashing |

---

## Step 1 — Build and Flash

```bash
git clone https://github.com/abdallahnatsheh/T-REX-FIRMWARE
```

Open the cloned folder in VSCode. PlatformIO will detect the project automatically.

**Select your environment** (bottom status bar in VSCode):

| Environment | Use when |
|-------------|---------|
| `env:T-Deck` | Standard T-Deck (no GPS, no speaker) |
| `env:T-Deck-Plus` | T-Deck Plus (GPS + I2S speaker) |

Click **Upload** (→ arrow in the bottom bar) with the T-Deck connected via USB.

**If upload fails:**
Hold the **trackball button** while plugging in the USB cable — this forces the ESP32-S3 into download mode. Then try Upload again. You can release the trackball button once the upload starts.

---

## Step 2 — First Boot

On first power-on you will see:
1. The T-Rex splash screen
2. The status bar at the top (battery, Bluetooth, WiFi, GPS icons)
3. The command prompt: `CMD>`

The device is ready. Nothing else is needed before you start typing commands.

**Status bar icons — what they mean at boot:**

| Icon | What you see | Meaning |
|------|-------------|---------|
| Battery | Green / Yellow / Red | Charge level |
| `ᛒ` | Grey | Bluetooth off (normal at boot) |
| Satellite | Grey | GPS off — run `gpson` to start (T-Deck Plus) |
| Shield | Grey | wguard not running |

---

## Step 3 — SD Card Setup

The SD card is **optional but recommended** — without it, WiFi credentials and scan logs are not saved between reboots.

**Format requirements:** FAT32, any size.

Insert the card and run:

```
CMD> sdf init
```

This formats the card (if not already FAT32) and creates the standard directory structure in one step:

```
/logs/          ← attack and scan logs
/badusb/        ← DuckyScript payloads
/captures/      ← reserved
```

Confirm it worked:

```
CMD> sdinfo      # shows card type and capacity
CMD> sdls /      # lists the root directory
```

---

## Step 4 — Set Your Timezone

The status bar clock and all session log timestamps use the configured timezone. Set it once and it survives reboots:

```
CMD> tz +3               # UTC+3 (Riyadh, Moscow, etc.)
CMD> tz -5               # UTC-5 (US Eastern, no DST)
CMD> tz +1               # CET (Central Europe, no DST)
CMD> tz status           # confirm what's set
```

Full POSIX strings with DST rules also work — see [Timezone](tz) for details.

---

## Step 5 — Learn the CLI

### The prompt

```
CMD> _
```

Type your command and press **Enter** or **click the trackball**. Commands are case-insensitive. Every command has a short alias — `sw` = `scanwifi`, `nd` = `netdiscover`, etc.

### Trackball navigation

| Trackball action | What it does |
|-----------------|-------------|
| Roll **left / right** | Move the cursor within your typed command — edit without retyping |
| Roll **up** | Previous command from history (up to 16 entries) |
| Roll **down** | Next command, or restore the draft you were typing |
| **Click** | Execute command (same as Enter) |
| **Double-click** | Toggle screen off / on |
| **Hold 3 seconds** | Lock screen (from any screen, any app) |

### Autocomplete

Press **`'`** (apostrophe — the **Sym + K** key on the T-Deck keyboard) at any point while typing:

- **Partial match** — fills the longest common prefix
- **Single match** — completes the word and adds a space
- **Multiple matches** — lists up to 8 options below the prompt

Works for command names and file paths (for `sdls`, `cd`, `cat`, `rm`).

### Command history

The last 16 commands are saved in a ring buffer. Use trackball **up/down** to navigate. Your current in-progress line is preserved — pressing **down** past the first entry restores it.

---

## Step 6 — Your First Commands

Run these in order to verify everything is working:

```
CMD> info         # device info: chip, MAC addresses, battery, SD status
CMD> help         # browse all commands by category
CMD> sw           # scan for WiFi networks — confirms radio is working
CMD> sbl          # scan for BLE devices — confirms Bluetooth is working
CMD> sdls /       # list SD root — confirms card is accessible
```

If `sw` returns networks and `sbl` returns devices, your T-Deck is fully operational.

---

## Step 7 — Connect to WiFi

Most network tools require an active WiFi connection:

```
CMD> sw           # scan — note the index of your network
CMD> cw 2         # connect to network at index 2
```

If the password is not saved yet, you will be prompted to enter it. It is saved to SD and NVS automatically for future use.

Verify connection:

```
CMD> info         # page 2 shows IP address and RSSI
CMD> pg 8.8.8.8   # ping Google DNS — confirms internet access
```

---

## What's Next

| I want to… | Start here |
|-----------|-----------|
| Capture a WPA2 handshake | [WPA Sniff](wpasniff) |
| Monitor my network for attacks | [WGuard IDS](wguard) |
| Check what devices are on my network | [Net Discover](netdiscover) → [Port Scan](portscan) |
| Detect if someone is tracking me | [Tracking Detection](trackme) |
| Use the T-Deck as a keyboard | [BT Keyboard](btkbd) or [USB Keyboard](usbkbd) |
| Learn all the commands | [Help & Manual](help-man) or use `help` on-device |
| See real-world usage examples | [Workflows](workflows) |
