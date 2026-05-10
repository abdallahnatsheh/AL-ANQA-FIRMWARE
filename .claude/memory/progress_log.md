---
name: Progress Log
description: What is implemented vs what is not yet started
type: project
---

## Done
- Bug fixes (all 8), keyboard polling fix, command buffer 128b
- SD card manager, WiFi monitor, deauth attack, FreeRTOS task infra
- Evil twin + captive portal, power save manager, GPS manager (Plus only)
- TrackMe anti-tracking scanner, splash screen, hidden SSID reveal
- ARP scan, parallel port scan, top-26 scan, ICMP ping
- BLE scanner, cyberpunk UI headers, esp info pages, LoRa test
- UI overhaul complete (LINE_HEIGHT=14, perPage=10, font 1.0× — current state is final)
- Banner grabbing (b key in port scan results)
- MAC changer (`macchanger/mc`) — auto-randomize STA MAC on scan/connect
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
