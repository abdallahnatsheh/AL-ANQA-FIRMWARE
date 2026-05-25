---
name: wguard-implemented-reference
description: WGuard — structs, sizes, and details NOT covered in CLAUDE.md
---

See CLAUDE.md for full threat list, evil twin logic, rate limits, session files, GDMA, bg mode.

**Key structs / sizes (not in CLAUDE.md):**
```cpp
struct WgEvent  { uint32_t ts; uint8_t sev; int8_t rssi; char msg[44]; char detail[48]; };
struct WgCounter{ uint8_t mac[6]; uint32_t count; uint32_t winStart; uint32_t lastFired; };
WG_EVENT_MAX=128  WG_CTR_MAX=16  WG_BSSID_MAX=128
```
`_savedEvCount` is `uint8_t` — resets to 0 with ring on AUTO-SAVE, set to `_evCount` on CHECKPOINT/MANUAL/FINAL.

**`detail` CSV column (DFIR, log-only, never shown on screen):**
- Targeted deauth: `dst=AA:BB... Dth=42 v=Alfa`
- Broadcast deauth: `Dth=42 v=LA-MAC`
- Evil twin: `Dth=5 v=RPi`
- Handshake harvest: `Dth=20 EAP=3`
- Beacon flood: `Bcn=150` · Auth flood: `Ath=32` · Probe storm: `Prb=50 v=Espressif`

**OUI lookup (`lookupOui()`):** LA-MAC check first (`mac[0] & 0x02`). Table: Alfa, Hak5, Realtek, RPi (6 OUI blocks), Espressif (22 OUI blocks). Returns `nullptr` for unknown — caller omits `v=` field.

**Timestamps:** session-relative (`e.ts - _sessionStartMs`). Display clock also session-relative.

**`_assocCtr[]`** isolated from `_deauthCtr[]` — prevents victim reconnections inflating deauth storm counter.

**ISR clone detection:** `s_lastTargetTs` / `s_targetTsSeen` volatile statics. `_cloneFiredTs` = 60s cooldown.
