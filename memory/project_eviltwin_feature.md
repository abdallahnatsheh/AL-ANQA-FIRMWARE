---
name: eviltwin-feature-implementation
description: "Evil Twin AP — MAC strategy, deauth algorithm, captive portal OS routes, credential table"
metadata: 
  node_type: memory
  type: project
  originSessionId: 375d410f-2458-450f-8ce0-724f0e36b6fe
---

**MAC strategy:** OPEN → clone exact BSSID + channel. WPA2 → random LA-MAC `(x & 0xFE) | 0x02` — deauth hits real AP only, our clients safe from own frames.

**Deauth:** 15 deauth + 15 disassoc via `esp_wifi_80211_tx()`, 8s interval. Fires only when `s_etClients == 0`. Client count via `WiFi.onEvent(ARDUINO_EVENT_WIFI_AP_STACONNECTED/DISCONNECTED)` — not `softAPgetStationNum()` (was broken).

**Captive portal routes:** `/generate_204` `/gen_204` `/connectivity-check.html` `/check_network_status.php` (Android) · `/hotspot-detect.html` `/library/test/success.html` (iOS/macOS) · `/ncsi.txt` `/connecttest.txt` `/redirect` (Windows) · `onNotFound` catchall.

**`handleRedirect()`:** 302 + `Location` + `Cache-Control: no-cache` + `Captive-Portal-URL` (RFC 8910) + HTML meta-refresh body. Empty body broke iOS/Windows — body is required.

**Windows:** no auto-popup. Shows toast "Additional sign-in required" (Win10) or "Sign in" in WiFi Settings (Win11).

**Keys during portal:** `[c]`=creds table (portal keeps running) · `[s]`=save to SD · `[p]`=SD template picker · `[t]`=cycle built-in templates · `[q]`=quit.

**Credentials:** `_creds[20]` in-memory + appends live to `/logs/eviltwin.csv`.
