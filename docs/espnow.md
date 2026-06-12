---
title: ESP-NOW
nav_order: 7
---

# ESP-NOW

Four commands for off-grid ESP-NOW communication — no router, no WiFi association required. Range: 200 m+ line-of-sight at 1 Mbps.

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
| `/apps/espchat/contacts.csv` | Saved contacts (MAC, name, LMK). RAM-only if no SD — cleared on reboot. |
| `/apps/espchat/pub/chN.log` | Public channel N message log |
| `/apps/espchat/prv/<MAC>.log` | Private conversation log per peer |

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
| `/apps/espsniff/NNN.csv` | Decoded frame log (session-numbered, never overwritten) |
| `/apps/espsniff/NNN.pcap` | Raw PCAP capture (same session number) |

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

## `espvoice` / `ev` — ESP-NOW Walkie-Talkie (HD Voice)

```
CMD> ev              # walkie-talkie on ch1
CMD> ev 6            # walkie-talkie on channel 6
```

Half-duplex push-to-talk voice over ESP-NOW. No router, no pairing — every T-Deck
on the same channel hears every transmission (broadcast). Requires the ES7210
microphone (present on **both** T-Deck and T-Deck Plus).

### Codec

ITU-T **G.722** wideband — 16 kHz / 64 kbps (Mode 1), via the vendored
public-domain `libg722` (`lib/libg722/`). One 20 ms frame = 320 PCM samples →
**160 bytes** → one ESP-NOW packet. That's true 7 kHz audio bandwidth (HD voice)
in the same packet budget as narrowband G.711 — twice the fidelity. G.722 is
stateless across packet loss, so a dropped frame is just a 25 ms gap, no
decoder drift.

### Push-to-talk is a TOGGLE

The T-Deck's I2C keyboard reports no key-up event, so true hold-to-talk is
impossible. Press **SPACE** to start transmitting; press it again to return to
listening.

### Walkie-talkie signaling

- The talker's screen shows a red `TRANSMITTING` banner.
- The listener's screen shows `>> RECEIVING <mac>` (the sender's MAC) while audio
  arrives.
- On release, the talker broadcasts an **end-of-transmission (EOT)** marker
  (repeated ×3 — ESP-NOW is lossy). Both ends play a **Roger beep** (the classic
  "over" cue). If all EOT packets are lost, the listener still ends the
  transmission after 500 ms of silence.

### Audio controls (app-local — never touch global `vol`)

| Control | Keys | Range | Notes |
|---------|------|-------|-------|
| RX volume | `[+]` / `[-]` | 0–150 % | Local playback trim. 100 % = full clean decoded level; >100 % boosts (turns yellow). Capped at 150 % — more hard-clips into distortion and can brown out the board. |
| TX mic gain | `[o]` / `[p]` | 0–37.5 dB | ES7210 ADC gain, applied live. **The clean way to be louder** on the far end — a hotter source beats boosting the already-full RX signal. Watch the mic-level bar: if it pegs red on normal speech, back off a step. |

Both reset to defaults (volume 100 %, gain 30 dB) each launch.

### Keys

| Key | Action |
|-----|--------|
| `[space]` | Talk / listen (PTT toggle) |
| `[+]` / `[-]` | RX volume |
| `[o]` / `[p]` | TX mic gain |
| `[,]` / `[.]` | Change channel |
| `[q]` | Quit |

### Notes

- Half-duplex: while transmitting you cannot hear incoming audio (expected for a
  walkie-talkie).
- Mic (I2S1) and speaker (I2S0) are separate I2S peripherals and stay installed
  for the whole session — no driver churn on PTT toggle (which crashed under live
  ESP-NOW DMA).
- No SD usage — no GDMA concerns.

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

The **voice** channel (`ev`) uses a separate frame on the same broadcast MAC:

```
type(1) + kind(1) + seq(1) + g722[160]
```

- `type` = `0x02` (voice)
- `kind` = `0x00` voice frame · `0x01` end-of-transmission (Roger) marker
- `g722` = 160 bytes = one 20 ms G.722 frame (16 kHz, 64 kbps). Ignored for EOT.

Any device implementing G.722 Mode 1 at 16 kHz on `type 0x02` can interoperate.

---

## Notes

- All four commands operate on the 2.4 GHz ISM band. Channel must match on all peers.
- BLE and WiFi share one antenna — stop any BLE command before using ESP-NOW commands.
- SD writes in `es` follow the GDMA rule: promiscuous is paused briefly during each flush.
- `ec` contacts saved to SD persist across reboots; without SD, contacts are session-only.
