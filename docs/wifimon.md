---
title: WiFi Monitor
parent: WiFi
nav_order: 2
---

# WiFi Monitor

## `wifimon` / `wm` — Promiscuous 802.11 Monitor

```
CMD> wm             # channel-hop across all 2.4 GHz channels
CMD> wm 6           # lock to channel 6
CMD> wm 11          # lock to channel 11
```

Press `q` to stop.

---

## Two Views

Switch between views with `[v]`.

### Nets View (default)
Shows every AP in range:
```
BSSID             CH RSSI Cli SSID
AA:BB:CC:DD:EE:FF  6  -42   3 HomeNetwork
11:22:33:44:55:66  1  -71   0 <hidden>
```

### Clients View
Shows every device seen — associated or probing:
```
  MAC               Vendor   Type  RSSI  AP
> AA:BB:CC:DD:EE:FF Apple    Phone  -55  HomeNet
  11:22:33:44:55:66 Samsung  Phone  -71  ---
```

Use trackpad **UP/DOWN** to move the cursor. Press `[d]` to targeted-deauth the selected client.

---

## Keys

| Key | Action |
|-----|--------|
| `[v]` | Toggle Nets ↔ Clients view |
| `[h]` | Toggle channel hop on/off |
| `[1]`–`[9]` | Lock to channel 1–9 |
| `[0]` | Cycle channels 10–13 |
| `[s]` | Toggle raw PCAP capture on/off |
| `[p]` | Toggle probe logger on/off |
| `[d]` | Deauth selected client (Clients view) |
| `[q]` | Quit |

---

## Raw PCAP Capture

`[s]` toggles raw 802.11 frame capture. Auto-starts on launch if SD is present.

- File naming:
  - Clock synced (NTP/GPS): `/apps/wifimon/001_20260604_143022.cap`
  - No clock: `/apps/wifimon/001.cap`
  - Counter always increments — **never overwrites** previous sessions
- Format: libpcap linktype 105 (LINKTYPE_IEEE802_11) — open directly in Wireshark or aircrack-ng
- Drop counter shown on screen as `1234 frm -N` when ring overflows (file stays valid)

---

## Probe Logger

`[p]` toggles passive probe request logging. Starts **OFF** — press `[p]` to begin.

Devices broadcast **probe requests** to reconnect to remembered networks. Every directed probe (non-wildcard) is logged:

```csv
time_ms,mac,vendor,ssid,rssi
18420,AA:BB:CC:DD:EE:FF,Apple,Marriott_Guest,-65
18501,AA:BB:CC:DD:EE:FF,Apple,United_WiFi,-63
18890,11:22:33:44:55:66,Samsung,CorpVPN,-71
```

- Saved to `/apps/wifimon/probes.csv` (append mode — survives across sessions)
- Deduplicated in RAM: same MAC+SSID pair logged only once per session
- Stats row shows `Log:N` (unique pairs) when active
- Works without SD card — silently stays off if no SD

**Use case:** A device probing for `Marriott_Guest` + `CorpVPN` reveals travel patterns and corporate affiliation. Combine with `et` to create a matching fake AP for auto-connect.

---

## Targeted Deauth

In Clients view, use trackpad to select a client and press `[d]`:

1. Promiscuous stops
2. Switches to APSTA mode
3. Injects AP→STA + STA→AP deauth + disassoc × 5 rounds
4. Returns to monitor mode

Status banner shows result (green = ok, red = fail).

---

## Channel Modes

| Mode | Behaviour |
|------|-----------|
| `wm` / `wm 0` | Hops 1→2→…→13, ~200 ms per channel. Sees all traffic but may miss short bursts. |
| `wm <ch>` | Locks to one channel. Every frame on that channel captured with no gaps. |

---

## SD Layout

```
/apps/wifimon/001.cap                     ← no NTP at capture time
/apps/wifimon/002_20260604_143022.cap     ← NTP/GPS synced
/apps/wifimon/003_20260604_151800.cap     ← NTP/GPS synced
/apps/wifimon/probes.csv                     ← probe harvest (all sessions, appended)
```

---

## Notes

- BLE and WiFi share one antenna — stop any BLE command before starting `wm`
- SD writes pause promiscuous for ~5 ms (GDMA rule) — brief gap in capture, no data loss
- Unassociated clients expire after 90 s of silence
