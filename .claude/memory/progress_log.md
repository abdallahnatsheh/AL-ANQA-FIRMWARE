---
name: Progress Log
description: What is implemented vs what is not yet started
type: project
---

## Session 2026-05-10
- MAC changer bug fix #1 — removed `esp_wifi_set_mode(WIFI_MODE_NULL)` trick that was corrupting scan subsystem (scan returned 0 results after any `mc` command)
- MAC changer bug fix #2 — re-added `applyIfEnabled()` in `connectToWiFiCommand()` after `WiFi.mode(WIFI_STA)` because `WiFi.disconnect(true)` deinits driver and wipes custom MAC
- Banner grabber overhaul — protocol-aware probing, 120ms animated spinner inlined in read loop, HTTP Server header extraction (384-byte buffer, `\nServer:` line search)
- OS fingerprinting — TTL via lwip raw ICMP (`raw_pcb`, `ip4_current_header()`) + SSH/HTTP banner analysis + port-presence fallback (RDP/SMB/MSRPC → Windows)
- Power save screen-off tier — 5 min inactivity → brightness 0, keypress wakes to 128; `set screenoff <s>` + `set screenoffmode on|off`
- README + project memory updated; `feature/wpa-man-helpfix` merged into `feature/pentest-enhancements`

## Done
- Bug fixes (all 8), keyboard polling fix, command buffer 128b
- SD card manager, WiFi monitor, deauth attack, FreeRTOS task infra
- Evil twin + captive portal, power save manager, GPS manager (Plus only)
- TrackMe anti-tracking scanner, splash screen, hidden SSID reveal
- ARP scan, parallel port scan, top-31 common ports scan, ICMP ping
- BLE scanner, cyberpunk UI headers, esp info pages, LoRa test
- UI overhaul complete (LINE_HEIGHT=14, perPage=10, font 1.0× — current state is final)
- Banner grabber (b key in port scan) — protocol-aware probing (HTTP active, TLS detect, MySQL binary, Redis PING), animated spinner (120ms update), HTTP Server header extraction, 55-entry service name table
- OS fingerprinting in port scan — TTL via lwip raw ICMP (≤64=Linux/macOS, ≤128=Windows), SSH/HTTP banner analysis, port-presence fallback (RDP 3389 / SMB 445 / MSRPC 135 → Windows)
- Power save screen-off tier — 5 min inactivity → brightness 0; keypress restores to 128. New psv subcommands: `set screenoff <sec>`, `set screenoffmode on|off`
- MAC changer (`macchanger/mc`) — randomize or set STA MAC; hooks into scan/connect/deauth/wpasniff; fixed scan-empty regression (removed mode-reset trick); fixed connect not spoofing (re-apply after WiFi.disconnect deinit)
- WPA2 handshake capture + on-device crack (`wpasniff/ws`, then `c`) — EAPOL sniff → pcap, deauth every 4s, `c` cracks with SD wordlist (unlimited) or built-in top-100; mbedtls PBKDF2+PRF512+HMAC-SHA1; saves to `/logs/cracked.csv`
- MAC changer hooked into deauth + handshake capture (applyIfEnabled before softAP)
- `getNetworkSSID()` added to WiFiFunctions
- Help command sub-pagination (5 cmds/sub-page) — fixes WiFi category overflow (9 cmds → 2 sub-pages)
- Man pages (`man/mn <cmd>`) — all 29 commands; SYNTAX/ABOUT/STEPS/KEYS/NOTE/FILES/WARNING labels in grey; l/a browse adjacent pages; short name lookup works

## Not Yet Built
- WPS flag in scanwifi output
- macwatch / mw — MAC proximity watchlist
- BLE GATT enumeration (`bleinfo/bi`)
- LoRa scanner (`lorascan/ls`)
- BadUSB / DuckyScript (`badusb/bu`)
- Trackpad gestures, touchscreen, microphone
