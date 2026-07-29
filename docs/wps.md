---
title: WPS
---

# WPS — one-command recon + handshake capture — `wps`

Everything WPS in a single screen. **Own networks only.**

```
CMD> sw            # scan first (WPS APs are flagged)
CMD> wps           # list WPS-enabled APs with their index
CMD> wps 3         # the all-in-one screen for AP #3
```

`wps <idx>` gives you, in one place:

## 1. Recon (automatic)
Captures the AP's beacon and decodes its **WPS Information Element**:
- **Version**, **AP-Setup-Locked** state (red if locked), **config methods** (PBC/Display/Keypad/Label)
- **Device-info leak** — the **Manufacturer / Model / Device Name** the router advertises (real fingerprinting)
- A **candidate PIN** computed from the BSSID (ComputePIN)

Logged to `/apps/wps/wps.csv`. The PIN is display-only — validate it with Reaver on a laptop.

## 2. Live WPS-handshake sniff (automatic)
While the screen is open it **sniffs the WPS EAP-WSC handshake** — the M1/M2/M3 messages are sent **unencrypted**, so when a WPS connection happens on-air (a client enrolling, or you press the button) the T-Deck captures the crypto material and shows `M1 OK  M2 OK  M3 OK`.

On a complete handshake it writes **`/apps/wps/pixie_NNN.txt`** — a **ready-to-run pixiewps command** with the PKE/PKR, E-Hash1/2 and nonces filled in:

```
pixiewps -e <PKE> -r <PKR> -s <E-Hash1> -z <E-Hash2> -n <E-Nonce> -m <R-Nonce> -b <enrollee-mac>
```

Run that on a laptop and, on a Pixie-Dust-vulnerable router, it recovers the PIN → password **in seconds, offline**. The T-Deck is the on-air capture half of the attack chain.

> A WPS exchange has to actually occur for M1–M3 to be caught — trigger it (connect a phone to the router via WPS, or press the WPS button) while `wps <idx>` is running.

## 3. `[p]` — push-button connect
Press `p` to attempt a WPS **push-button** connect. Press the router's physical WPS button; on success the T-Deck recovers the **SSID + PSK** → `/apps/wps/creds.csv`.

`[q]` stops.

## Honest limit
The **ESP32 cannot run the WPS authentication itself** — the closed WiFi stack won't let you supply a PIN, act as a WPS registrar, or associate outside its own connect flow (`esp_wps_config_t` has no PIN field; registrar mode is unsupported). No ESP32 firmware overcomes this. So `wps` does the real on-device work — **recon + handshake capture + PBC** — and hands the PIN/Pixie-Dust crack to `pixiewps`/`reaver` offline. Intel laptop Wi-Fi often can't inject for Reaver either; use an AR9271/RT3070 adapter for the offline attack.
