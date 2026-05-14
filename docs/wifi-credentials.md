---
title: WiFi Credentials
nav_order: 3
---

# WiFi Credential Management

T-Rex stores WiFi credentials in two places: **NVS** (on-device flash) and **SD card** (`/wpa_supplicant.conf`). Both are used automatically — you never have to think about which one.

---

## How Credentials Work

When you connect to a network, T-Rex resolves the password in this order:

1. **NVS** — fastest, survives reboots, device-local
2. **SD card** (`/wpa_supplicant.conf`) — shared across devices, Linux-compatible
3. **Prompt** — you type the password; it is saved to both NVS and SD automatically

On successful connection the network is always appended to `/wpa_supplicant.conf` on the SD card (if inserted) in standard Linux `wpa_supplicant` format.

---

## `wifipass` / `wp` — View Saved Passwords

```
CMD> wp
```

Shows all saved credentials in a paginated table. Reads from SD card first; falls back to NVS if no SD or no file found.

Header shows the active source in colour: **SD** (green) or **NVS** (yellow).

| Display | Meaning |
|---------|---------|
| plain text | usable password |
| `[open]` | open network, no password needed |
| `[hex-psk]` | Linux-hashed entry — T-Rex cannot use the hash; run `cw <ssid>` to enter the password once and fix it |
| `~name` in cyan | hidden network (`scan_ssid=1`) |

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `q` | Quit |

---

## `connectwifi` / `cw` — Connect by Name

```
CMD> cw <index>       # use number from last sw scan
CMD> cw <ssid>        # connect by SSID name — no scan needed
```

Connecting by SSID name is useful when:
- The network is hidden (does not broadcast its SSID)
- You have the network on the SD card from a previous session or imported from Linux
- You do not want to run a full scan

T-Rex sends directed probe requests for hidden networks automatically — no extra configuration needed.

---

## SD Card File: `/wpa_supplicant.conf`

T-Rex uses the exact same format as Linux `wpa_supplicant`. Every entry T-Rex writes looks like this:

```
network={
    ssid="MyNetwork"
    #psk="mypassword"
    psk="mypassword"
}
```

- `psk="plain"` — Linux connects with this immediately
- `#psk="plain"` — comment that survives if Linux rehashes; T-Rex reads this to recover the plain password
- `/wpa_supplicant.bak` — auto-created the first time T-Rex modifies the file; your original is always safe

---

## Linux Sync

### Raspberry Pi / Headless Linux → T-Deck

Copy directly — the format is identical:

```bash
sudo cp /etc/wpa_supplicant/wpa_supplicant.conf /media/$USER/<sdcard>/wpa_supplicant.conf
```

Insert the SD card, boot T-Deck. All networks are available immediately.

---

### Desktop Linux (NetworkManager) → T-Deck

NetworkManager does not use `/etc/wpa_supplicant/wpa_supplicant.conf` as its primary store — it keeps plaintext passwords in `/etc/NetworkManager/system-connections/`. This is a one-time migration:

**Step 1 — list all saved SSIDs and passwords:**

```bash
sudo nmcli -s -g NAME,802-11-wireless.ssid,802-11-wireless-security.psk connection show
```

Output:
```
HomeWiFi  :HomeWiFi  :mypassword123
WorkNet   :WorkNet   :workpass456
```

**Step 2 — generate the SD file:**

```bash
wpa_passphrase "HomeWiFi" "mypassword123" >> /media/$USER/<sdcard>/wpa_supplicant.conf
wpa_passphrase "WorkNet"  "workpass456"  >> /media/$USER/<sdcard>/wpa_supplicant.conf
```

Done. This is a **one-time setup**. After that, any new network you connect to on T-Deck is automatically appended to the file — copy it back to Linux and it works there too.

---

### T-Deck → Linux

Copy the SD file to any Linux machine:

```bash
sudo cp /media/$USER/<sdcard>/wpa_supplicant.conf /etc/wpa_supplicant/wpa_supplicant.conf
sudo systemctl restart wpa_supplicant
```

Linux accepts `psk="plaintext"` and connects immediately. No conversion needed.

---

## `update_config=1` Caveat

If Linux has `update_config=1` set, it rewrites `wpa_supplicant.conf` after connecting and **strips all comments** — including the `#psk=` line. The entry becomes:

```
network={
    ssid="MyNetwork"
    psk=a3f9bc12e4...    ← 64-char hash, T-Rex cannot use this
}
```

If you copy this file back to T-Deck, affected networks show `[hex-psk]`.

**Fix:** run `cw <ssid>`, enter the password once. T-Rex connects, saves the plain password to NVS, and upgrades the SD entry. The network works permanently from that point on without re-entering.

---

## Format Compatibility Table

| Linux format | T-Rex behaviour |
|---|---|
| `psk="plaintext"` | ✅ connects directly |
| `#psk="plain"` + `psk=hexhash` (`wpa_passphrase` output) | ✅ recovers plain from comment |
| `psk=hexhash` only (`update_config=1` stripped comment) | ⚠️ shows `[hex-psk]` — enter password once to fix |
| `key_mgmt=NONE` (open network) | ✅ connects directly |
| `scan_ssid=1` (hidden network) | ✅ `cw <ssid>` sends directed probe |
| `priority=`, `bssid=`, `proto=`, `pairwise=` | ✅ parsed or silently ignored, file never corrupted |
| `ctrl_interface=`, `update_config=`, `country=` | ✅ silently ignored |
| `ssid=4d79...` (hex-encoded SSID, non-ASCII) | ❌ not supported — network skipped |
| `key_mgmt=WPA-EAP` (enterprise / certificates) | ❌ not supported on ESP32 |
