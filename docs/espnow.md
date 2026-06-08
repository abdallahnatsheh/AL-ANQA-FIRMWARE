---
title: ESP-NOW
nav_order: 7
---

# ESP-NOW

Three commands for off-grid ESP-NOW communication — no router, no WiFi association required. Range: 200 m+ line-of-sight at 1 Mbps.

---

## `espchat` / `ec` — Encrypted Off-Grid Chat

```
CMD> ec              # interactive chat, public broadcast ch1
CMD> ec bg           # background listener mode
```

### Modes

| Mode | Description |
|------|-------------|
| **Public** | Broadcast on ch1 (default). Any ESP32/ESP8266 with ESP-NOW on the same channel can receive/send. |
| **Private** | AES-128 encrypted unicast (ESP-NOW CCMP). LMK derived via SHA-256(PIN + sorted MACs). |

### Pairing Flow (Private Mode)

1. Initiator opens the pair dialog — a 4-digit PIN is displayed.
2. Receiver enters the same PIN.
3. A 3-attempt encrypted round-trip validates the PIN (`* pair ok` / `* pin ack`).
4. Wrong PIN: ESP-NOW hardware drops the frame — no confirmation sent.

A pair-request bar (amber) appears when an incoming pairing request is received.

### Keys

| Key | Action |
|-----|--------|
| `[+]` / `[-]` | Change channel (flashes yellow) |
| Trackball UP/DOWN | Scroll message history |
| Trackball click | Pair action |
| Hold trackball 1.5 s | Exit |

### Notifications

| Event | Sound |
|-------|-------|
| Public message | PING (short beep) |
| Private / known contact message | INFO |
| Pairing request | WARNING (double beep) |

### UI Elements

- Timestamps on every message (HH:MM)
- Scroll slider and unread badge (`+N new`)
- Contact name in header for private conversations
- Character counter on input line
- `EC` badge in status bar when background mode is active

### Background Mode

`ec bg` listens passively without occupying the screen. A popup bar appears and a PING plays on new public messages; INFO plays on private contact.

### Files

| Path | Contents |
|------|----------|
| `/espchat/contacts.csv` | Saved contacts (MAC, name, LMK). RAM-only if no SD — cleared on reboot. |
| `/espchat/pub/chN.log` | Public channel N message log |
| `/espchat/prv/<MAC>.log` | Private conversation log per peer |

---

## `espsniff` / `es` — Passive ESP-NOW Frame Sniffer

```
CMD> es              # sniff on ch1
CMD> es 6            # sniff on channel 6
```

Passive promiscuous capture of ESP-NOW action frames. Does not transmit.

### Keys

| Key | Action |
|-----|--------|
| `[c]` | Hop to next channel / lock current channel |
| `[+]` / `[-]` | Change channel |
| `[j]` / `[k]` or trackball | Move row selection |
| `[Enter]` | Frame detail view |
| `[a]` / `[l]` | Page left / right |
| `[s]` | Save current capture to SD |
| `[f]` | Filter frames |
| `[x]` | Clear frame list |
| `[q]` | Quit |

### Files

| Path | Contents |
|------|----------|
| `/logs/espsniff/NNN.csv` | Decoded frame log (session-numbered, never overwritten) |
| `/logs/espsniff/NNN.pcap` | Raw PCAP capture (same session number) |

---

## `esptest` / `est` — ESP-NOW TX/RX Diagnostic

```
CMD> est             # broadcast test on ch1
CMD> est 6           # broadcast test on channel 6
```

Broadcasts a test frame every 2 seconds and logs all received ESP-NOW frames. Use to verify range, channel, and peer compatibility.

### Keys

| Key | Action |
|-----|--------|
| `[+]` / `[-]` | Change channel |
| `[q]` | Quit |

---

## Interoperability — Public Channel Wire Format

Any ESP32 or ESP8266 device with ESP-NOW on the same channel can join the **public** chat by sending a 114-byte binary frame:

```
type(1) + seq(1) + name[12] + text[100]
```

- `type` = `0x01` (public chat)
- `seq` = incrementing sequence number
- `name` = null-padded sender name (12 bytes)
- `text` = null-padded message body (100 bytes)
- Destination MAC: `FF:FF:FF:FF:FF:FF` (broadcast)

Private chat requires the SHA-256 LMK derivation (`SHA-256(PIN + sorted MACs)`) and is not interoperable with third-party devices that do not implement the same key derivation.

---

## Notes

- All three commands operate on the 2.4 GHz ISM band. Channel must match on all peers.
- BLE and WiFi share one antenna — stop any BLE command before using ESP-NOW commands.
- SD writes in `es` follow the GDMA rule: promiscuous is paused briefly during each flush.
- `ec` contacts saved to SD persist across reboots; without SD, contacts are session-only.
