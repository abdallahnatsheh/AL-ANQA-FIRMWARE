---
name: eviltwin feature implementation
description: Evil Twin AP command — smart MAC strategy, adaptive deauth, credential table, captive portal cross-platform detection
type: project
---

## What was built

`eviltwin` / `et` command — rogue AP with captive portal, two modes.

**Files:** `t-deck-cli/eviltwin.h` + `eviltwin.cpp`

---

## Design summary

**Mode 1 — Clone existing AP:**
- Fresh `WiFi.scanNetworks()` inline, paginated picker (5/page), OPEN green vs WPA
- `askDeauth()`: shows SSID/MAC/ch/security, explains strategy, `[y]/[n]/[q]`

**Smart MAC strategy:**
- OPEN → clone exact MAC + channel, seamless reconnect
- WPA2 → random locally-administered MAC (`_fakeMAC[0] = (x & 0xFE) | 0x02`), deauth hits real AP only

**Mode 2 — Custom SSID:** live keyboard input, no spoof, no deauth, ch 1

---

## Deauth algorithm

Burst-pause: 15 deauth + 15 disassoc via `esp_wifi_80211_tx()`, 8s pause, fires only when `s_etClients == 0`.
Client count via `WiFi.onEvent(ARDUINO_EVENT_WIFI_AP_STACONNECTED/DISCONNECTED)` (not `softAPgetStationNum()` — that was wrong).

---

## Captive portal

DNS wildcard `"*"` → 192.168.4.1. WebServer port 80.

**OS detection routes:** `/generate_204`, `/gen_204`, `/connectivity-check.html`, `/check_network_status.php` (Android), `/hotspot-detect.html`, `/library/test/success.html`, `/success.html` (iOS/macOS), `/ncsi.txt`, `/connecttest.txt`, `/redirect` (Windows), `/canonical.html`, `/success.txt`, `/detected.html` (Firefox/Linux), `onNotFound`.

**handleRedirect():** 302 + `Location` + `Cache-Control: no-cache` + `Captive-Portal-URL` (RFC 8910) + HTML meta-refresh body. Empty body was the original bug — Windows/iOS need the body fallback.

**handleRoot():** adds `Cache-Control` and `Captive-Portal-URL` headers.

**Windows note:** Does NOT auto-popup a browser. Shows toast "Additional sign-in required" in system tray (Win10) or "Sign in" button in WiFi Settings (Win11). User must click it. Alternatively open any HTTP URL in browser.

**Templates:** built-in Google clone (0) or Router page (1), `[t]` cycles. `[p]` opens SD picker from `/evilportal/` (stops/restarts server).

---

## Credential capture

In-memory: `CapturedCred _creds[20]` (ET_MAX_CREDS=20), also appends to `/logs/eviltwin.csv` on each capture.

**Keys during portal:**
- `[c]` → credential table (portal stays running — reads in-memory array only, no server stop)
- `[s]` → save all in-memory creds to SD (rewrites file cleanly)
- `[p]` → SD template picker (stops/restarts server)
- `[t]` → cycle built-in templates
- `[q]` → quit

**Creds table separator fix:** use `dm.getCursorY()` after header println, then `setCursor(4, sepY + 3)` before loop — was drawn at `outputY + LINE_HEIGHT + 1` (1px above first cred text, visually overlapping).

---

## Pending features

- WPA2 handshake capture (EAPOL → `.cap` on SD)
- WPS detection flag in scan output
- Banner grabber
- BLE GATT enumeration
