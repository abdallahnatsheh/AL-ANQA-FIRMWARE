---
name: handshake-capture-module
description: "WPA2 handshake capture (ws command) — pcap save strategy, crack flow, SD layout"
metadata: 
  node_type: memory
  type: project
  originSessionId: 375d410f-2458-450f-8ce0-724f0e36b6fe
---

`ws <idx|bssid> [ch]` — deauth target, sniff EAPOL M1+M2, save libpcap to SD, optionally crack on-device.

**pcap strategy (GDMA-safe):** Open `SD.open()` BEFORE WiFi setup. Buffer M1+M2 raw frames (max 2×256 B) in `WpaHandshake.m1Raw/m2Raw`. All `pcap.write()` calls happen AFTER `WiFi.mode(WIFI_STA)` teardown via `finalizePcap` lambda. No real-time SD writes during capture — FatFS corrupts under APSTA+promiscuous.

**SD layout:** `/logs/hs/<BSSID>.cap` (libpcap, LINKTYPE_IEEE802_11=105). Compatible with aircrack-ng and hashcat (`hcxtools pcapng` conversion). If cracked: `/logs/cracked.csv` (SSID,password,timestamp).

**Wordlist selection:** If `/wordlist.txt` exists on SD, user sees `[1] SD wordlist / [2] Built-in (100 pwds)`. Built-in list covers common passwords only. Press `1` or `2` to choose.

**On-device crack:** PBKDF2(SSID, pass, 4096, 32) → PRF-512 → KCK → HMAC-SHA1 MIC verify against captured M2. Shows progress bar. Cracked password saved to `/logs/cracked.csv`.

**applyIfEnabled removed:** MAC changer is irrelevant for `ws` — deauth frames spoof SA = target BSSID, not the device MAC. See [[esp32s3-wifi-sd-gdma-constraint]].

**Deauth in ws:** Uses same raw 802.11 injection as `da`. Requires promiscuous mode + APSTA. M1 appears within seconds; M2 requires client reconnect after deauth.
