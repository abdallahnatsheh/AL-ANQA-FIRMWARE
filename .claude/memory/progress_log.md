---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

## Session 2026-05-29
- **buddy** — NimBLE v2.x name fix (`enableScanResponse`+`setName`); `onConnect` sets `s_connected` immediately; stale bond detection (reasons 0x05/0x06); cleanup removes `init("T-REX")` — field tested, working
- **btkbd** — auto-bond-delete on auth fail; stale bond UI — field tested, working
- **ble_spam** — `bsRestoreStack` no longer re-inits; Android wait 10s per cycle; `spamAll` Android slot 8s
- **fast_pair** — `scan()` rewritten: FreeRTOS task removed, `start(0)` + millis() 5s loop; `spam()` wait 10s; hijack prompt cursor fix (promptY saved before poll loop)
- **wifi_functions** — `readPassword()` cursor fix: inputY saved before loop, redraws at fixed Y

## Session 2026-05-25 (summary)
Lock screen write-block + unlock auto-redraw; backspace hold-repeat; btkbd/bk BLE HID keyboard+mouse; buddy MITM bonding; WiFi wrong-password fix; NTP sync fix; status bar 3s live refresh; wguard WiFi isolation; `sdrm→rm` rename.

## Not Yet Built
- macwatch/mw — MAC proximity watchlist
- LoRa scanner — lorascan/ls
- bmon — passive BLE ad sniffer (iBeacon/Eddystone/cleartext, PCAP linktype 251)
- ES7210 mic, GT911 touchscreen (pins: project_future_peripherals.md)
