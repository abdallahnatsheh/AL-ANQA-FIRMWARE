---
name: hiddenssid feature
description: Hidden SSID reveal via deauth+sniff — command design, IRAM_ATTR chain, SD persist, scan table integration
type: project
---

`hiddenssid/hs <idx|bssid> [ch] [silent]` — forces clients off a hidden AP, sniffs the probe response/assoc request they send on reconnect to recover the SSID.

**Why:** Hidden APs broadcast no SSID in beacons. Deauth makes clients reconnect, which carries the SSID in plaintext in the management frame.

**How to apply:** Any changes to this module must preserve these invariants:

- `snifferCb` is `IRAM_ATTR`. It calls `WiFiMonitor::extractSSID()` which is also `IRAM_ATTR` — entire callback chain must stay in IRAM or ESP32 crashes with cache exception.
- Filter is subtype 5 (probe response) OR subtype 0 (assoc request) — subtype 4 (probe request) uses FF:FF:FF:FF:FF:FF as BSSID so the BSSID filter never matches; don't add it back.
- Sniffer is stopped (`rx_cb = nullptr`, promiscuous off) the moment SSID is found — saves power, no cleanup race.
- SD save uses `_wf.refreshHiddenCache()` + `isHiddenKnown()` before `appendLine` — prevents duplicate rows on repeated runs against same AP.
- Scan table: `wifi_functions.cpp` `loadHiddenCache()` called after every scan. Known-hidden entry renders as `~name` in `TFT_CYAN`; unknown stays `<hidden>` in `0x7BEF` grey.
- File: `/logs/hidden_ssids.csv` — format `BSSID,SSID,channel`, up to 64 entries in RAM cache.
- `loadHiddenCache()` uses fixed `char buf[96]` + `strtok` — no Arduino `String` heap alloc per line.
