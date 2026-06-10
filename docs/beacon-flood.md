---
title: Beacon Flood
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 6
---

# Beacon Flood (`beaconflood` / `bf`)

> **Authorized use only.** Beacon flooding disrupts WiFi network discovery for every device in range. Only use against networks and environments you own or have explicit written permission to test.

Beacon flood injects hundreds of fake 802.11 beacon frames per second into the air — each advertising a different SSID with a randomly generated MAC address. Every device in range sees its WiFi list flooded with fake networks.

---

## Usage

```
CMD> bf
```

Typing `bf` with no arguments opens the mode picker:

```
[BCON::FLOOD]  select mode
──────────────────────────
[1] list      funny SSIDs
[2] rickroll  Never gonna...
[3] seq       base + number
[4] file      /apps/beaconflood/wordlist.txt
[5] clone     pick from scan
──────────────────────────
[q] cancel
```

Press a number to select. Press `q` to cancel.

---

## Modes

### `[1] list` — Built-in SSID list

```
CMD> bf
CMD> bf list
```

Cycles continuously through 40 built-in humorous network names ("Abraham Linksys", "FBI Surveillance Van", "Silence of the LANs", etc.). Each SSID gets a fresh random MAC address. Channels hop automatically.

---

### `[2] rickroll` — Rickroll

```
CMD> bf rickroll
```

Floods with the lyrics of Never Gonna Give You Up as sequential SSIDs:

```
01 Never gonna give you up
02 Never gonna let you down
03 Never gonna run around
04 And desert you
...
```

---

### `[3] seq` — Sequential

```
CMD> bf seq
CMD> bf seq Starbucks
CMD> bf seq HotelGuest_Room
```

When selected from the menu, prompts you to type a base name and press Enter to confirm. Press Enter on an empty input to cancel back to the menu. Generates numbered variants:

```
Starbucks1 → Starbucks2 → ... → Starbucks9999 → wraps + hop channel
```

Useful for simulating a venue with many APs or making one network appear to have hundreds of repeaters.

---

### `[4] file` — SD file

```
CMD> bf file
CMD> bf file /mylist.txt
```

Reads SSIDs from `/apps/beaconflood/wordlist.txt` on the SD card (one SSID per line, max 32 characters each). Loops the file continuously. Optionally pass a custom path as a second argument.

**Example `/apps/beaconflood/wordlist.txt`:**
```
Free Airport WiFi
Hotel Guest
xfinitywifi
ATT_WIFI_4821
NETGEAR_5G
CorporateGuest
```

> Requires an SD card. Returns an error if the card or file is not present.

---

### `[5] clone` — Clone from scan

```
CMD> bf clone
```

Picks a real network from the last `scanwifi` result and floods the air with beacons using that exact SSID — but with random MACs and no real AP behind them. Every nearby device sees dozens of networks with the same name and cannot tell which is legitimate.

**Requires a prior scan.** If no scan data exists you will see:

```
[BCON::FLOOD]  clone
────────────────────
No scan data.
Run scanwifi first, then bf.
[q] back
```

After running `sw`, select `[5]` to open the network picker:

```
[BCON::FLOOD]  01/02
────────────────────
[1] ch6   Starbucks_WiFi
[2] ch1   BT-HomeHub-XYZ
[3] ch11  NETGEAR_5G
[4] ch6   linksys
...
1-9=pick  a/l=page  q=cancel
```

Pick by index. Flood starts immediately and **stays locked to that network's channel** — unlike other modes, clone never hops. This is intentional: devices scan the real AP's channel and clone beacons must be there to be seen.

**Invisible character trick:** each clone beacon appends a different number of U+200B zero-width spaces to the SSID. The bytes are different so Windows and Android treat each variant as a distinct network — they appear as separate entries in the scan list, all showing the same name. A 10-character SSID gets up to 7 visible clone entries; a 32-character SSID has no room and falls back to single-entry behaviour.

> If the target SSID is 32 bytes (max length) there is no room for the invisible suffix — clones will merge into the real AP's entry in the scan list.

---

## While running

All modes show a live stats screen:

```
[BCON::FLOOD]  clone Starbucks_WiFi
────────────────────────────────────
Ch:  6   Sent: 4,821
Err: 0   Rate: ~94/s
SSID: Starbucks_WiFi
[q] stop
```

| Field | Meaning |
|-------|---------|
| Ch | Current transmit channel |
| Sent | Total beacon frames injected |
| Err | Frames rejected by the WiFi driver |
| Rate | Approximate frames per second (updated every 200 ms) |
| SSID | Current SSID being flooded |

Press **`q`** to stop. WiFi is restored cleanly to STA mode on exit.

---

## How it works

Each beacon is a dynamically-built 802.11 management frame (subtype 8):

- **Packed IEs, no padding** — frame is built byte-by-byte so Supported Rates and RSN IEs immediately follow the SSID data. A fixed 32-byte SSID slot would leave zero-padding that breaks 802.11 IE parsers on Windows/Android, causing WEP to be reported instead of WPA2.
- **Accurate security flags** — clone mode matches the real network (open = no Privacy bit, no RSN IE, ~57-byte frame; WPA2 = Privacy bit + RSN IE CCMP-PSK, ~79-byte frame); all other modes inject as WPA2
- **Random locally-administered MAC** per frame — `mac[0] = (mac[0] & 0xFE) | 0x02` — so every fake AP appears as a distinct device
- **Channel hops** every 20 beacons through all 13 channels (spread-first: 1 → 6 → 11 → 2 → 7 → 12 ...) to maximise 2.4 GHz band coverage — clone mode stays locked to the target channel
- **2 frames per SSID** with 1 ms gap — improves acceptance rate on devices that filter single unseen frames
- Injected via `esp_wifi_80211_tx` — bypasses the normal WiFi stack and puts frames directly on air

---

## Compatibility note

Beacon flood and `wguard` **cannot run at the same time** — both use promiscuous mode and wguard locks to a specific channel. To test wguard's beacon flood detection (`BEACON FLOOD` — 100+ unique BSSIDs / 30 s), run `bf` on a second device while `wg` runs on the T-Deck.
