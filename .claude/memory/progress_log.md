---
name: Progress Log
description: Recent session changes + not-yet-built list
type: project
---

## Session 2026-06-13
- **ssh/sc** — interactive SSH client, built + first-connect working. LibSSH-ESP32 (`ewpa/LibSSH-ESP32`, real lib_deps). Runs in a 50KB FreeRTOS task; reuses `cw` STA conn; password auth + PTY shell. Colour terminal (ANSI-16, scrollback ring 120 lines, trackpad scroll, scrollbar). KNOWN: HW-SHA concurrency caveat (can't disable on precompiled core) → suspect for connect-phase crashes. `/apps/ssh/` reserved for known_hosts/keys (not written yet). Full ref: [[project_ssh_client]].
- **espchat PIN fix** — known contacts skip PIN (reuse stored LMK+channel); private always inits via ecCoreInitWithLmk (fixed empty-PIN→public fallthrough). PIN = first-time pairing only.
- **espvoice diagnostics** — drp/heap on stats line, esp32_exception_decoder, RX mic-DMA drain (crash still under watch).
- Memory cleanups: bmon marked implemented; next_steps/MEMORY reconciled.

## Session 2026-06-12
- **espvoice/ev** — ESP-NOW half-duplex walkie-talkie, field-tested working (both directions + sound). HD voice via **G.722** (16kHz/64kbps Mode 1), vendored public-domain `lib/libg722/` (sippy/libg722, auto-discovered by LDF, NOT in lib_deps). Wire: `EvMsg{type=0x02,kind,seq,g722[160]}`=163B, kind 0=voice 1=EOT. PTT = **TOGGLE** (keyboard has no key-up). Walkie signaling: `RECEIVING <mac>` tag + Roger beep both ends (EOT ×3 + 500ms silence fallback). App-local audio: RX vol 0-150% (`+/-`, capped — higher hard-clips/brownout), TX mic gain 0-37.5dB (`o/p`, clean louder) — neither touches global vol. Channel `,/.`
- **CRASH FIX** — installing/uninstalling I2S drivers on every PTT toggle while ESP-NOW DMA live = crash (brownout). Fix: install BOTH I2S ports once at start, keep resident (mic=I2S1, speaker=I2S0, separate peripherals), gate by PTT state. Also explains "300% volume → crash": clipped full-scale audio at high gain browns out the rail.
- **mictest/mt** — ES7210 mic diagnostic: live level meter, VAD, record 3s + replay. Fixed 2x-slow/devil-voice (ALL_LEFT delivers 2 int16/sample — must de-dup `raw[2*i]`).
- **Board-gate correction** — mic+speaker are on BOTH T-Deck and T-Deck Plus; only GPS is Plus-only. Removed wrong `#ifdef BOARD_TDECK_PLUS` from mictest+espvoice. (Old peripherals memory wrongly said mic=I2S_NUM_0 — it's I2S_NUM_1.)
- Docs/man/README/CLAUDE.md all updated for ev + mt.

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
