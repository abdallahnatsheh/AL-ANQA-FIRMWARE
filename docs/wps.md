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

Logged to `/apps/wps/wps.csv`.

## 2. Candidate-PIN generator + attack sheet (automatic)
Runs **9 WPS PIN algorithms** on the BSSID (the same ones OneShot/WPSpin use): `pin24`, `pin28`, `pin32`, `DLink`, `DLink+1`, `ASUS`, `Airocon`, plus the `12345670` / `00000000` statics. It shows the top candidates on screen and writes **`/apps/wps/attack_NNN.txt`** — the full PIN list **plus ready reaver/pixiewps commands** for a laptop:

```
sudo reaver -i mon0 -b <bssid> -c <ch> -vv        # full PIN brute
sudo reaver -i mon0 -b <bssid> -c <ch> -K 1 -vv   # pixie-dust
44479879   # pin24
...        # + the other algorithm candidates
```

The PINs are **display/export only** — the ESP32 can't test them on-device, so you run the sheet on a laptop with an injection-capable adapter (AR9271/RT3070). One of the algorithm PINs cracks many older/vendor routers instantly.

## 3. Live WPS-handshake sniff (automatic)
While the screen is open it also **sniffs the unencrypted WPS EAP-WSC handshake**; if a full M1/M2/M3 goes on-air it saves **`/apps/wps/pixie_NNN.txt`** (a pixiewps command with PKE/PKR/E-Hash1/2/nonces).

> **Be honest about this one:** a *usefully crackable* Pixie-Dust exchange requires the attacker to be the WPS **registrar** (so the AP reveals its weak-RNG hashes), and the ESP32 **can't be a registrar** — so it can't *trigger* a crackable handshake. This sniff only pays off in the narrow case of passively catching someone *else's* external-registrar attack against a weak AP. The reliable wins are the recon, the PIN sheet, and PBC.

## 3. `[p]` — push-button connect
Press `p` to attempt a WPS **push-button** connect. Press the router's physical WPS button; on success the T-Deck recovers the **SSID + PSK** → `/apps/wps/creds.csv`.

`[q]` stops.

## Honest limit
The **ESP32 cannot run the WPS authentication itself** — the closed WiFi stack won't let you supply a PIN, act as a WPS registrar, or associate outside its own connect flow (`esp_wps_config_t` has no PIN field; registrar mode is unsupported). No ESP32 firmware overcomes this. So `wps` does the real on-device work — **recon + handshake capture + PBC** — and hands the PIN/Pixie-Dust crack to `pixiewps`/`reaver` offline. Intel laptop Wi-Fi often can't inject for Reaver either; use an AR9271/RT3070 adapter for the offline attack.
