---
title: Scan & Connect
parent: WiFi
nav_order: 1
---

# Scan & Connect

---

## `scanwifi` / `sw` — WiFi Manager

```
CMD> sw          # interactive WiFi manager (scans on entry)
CMD> sw on       # turn the radio on (STA)
CMD> sw off      # turn the radio off (disassociate, idle)
```

`sw` opens an interactive **WiFi manager**: a scrollable list of nearby 2.4 GHz networks (index, SSID, RSSI, security, WPS) with a live **status line** at the top showing the current connection (SSID · IP · RSSI) or `Not connected` / `Radio OFF`.

| Key | Action |
|-----|--------|
| Trackball ↑ / ↓ | Move the selection |
| Trackball click / `Enter` | Connect to the highlighted network |
| `d` | Disconnect |
| `f` | Forget the highlighted network (removes it from NVS **and** `/wpa_supplicant.conf`) |
| `o` | Toggle the radio on / off |
| `l` / `a` | Page down / up |
| `u` | Re-scan |
| `q` | Quit |

Connecting reuses the same flow as `cw` (password prompt if unknown, then saved to NVS + `/wpa_supplicant.conf`). **Radio "off" leaves WiFi in an idle STA state** — it never uses `WIFI_OFF`, so it stays GDMA-safe and ready to re-scan.

The scan result is cached — use `show wifi` to view it again without rescanning. The scan index (`#`) is still the one `cw` / `da` / `et` / `hs` / `ws` use.

### Security column

Each network is tagged by the AP's advertised authentication mode:

| Tag | Meaning | Colour |
|-----|---------|--------|
| `OPEN` | No encryption | Magenta |
| `WEP` | Legacy WEP | Red |
| `WPA` / `WPA2` | WPA / WPA2-PSK | Grey |
| `WPA3` | Pure WPA3 (SAE only) — not downgradeable | Green |
| `WPA3/TD` | **Transition mode** — WPA2 **and** WPA3 both accepted | **Yellow** |

`WPA3/TD` (yellow) marks a WPA3-capable AP that still accepts WPA2, which makes it a candidate for a **WPA3 transition-mode downgrade** — the target class for the planned `wpa3down` / `w3d` attack. A pure WPA3 (SAE-only) AP shows plain `WPA3` (green). The classification comes straight from the ESP32 scan record's `authmode` (no extra probing / no transmit).

---

## `connectwifi` / `cw` — Connect to a Network

```
CMD> cw <index>    # connect by scan index from last sw
CMD> cw <ssid>     # connect by SSID name
```

Connects to a network by index or SSID name. Password is resolved automatically from NVS first, then from `/wpa_supplicant.conf` on the SD card. You are only prompted if the password is not found.

On a successful connection the network is saved to `/wpa_supplicant.conf`.

> For credential management and Linux sync see the [WiFi Credentials](wifi-credentials) guide.

---

## `wp clear` — Erase Saved Credentials

```
CMD> wp clear
```

Erases all saved WiFi passwords from NVS (non-volatile storage). The next connection to a known network will prompt for the password again. (Formerly the standalone `clearwifi` / `clrw` command — now folded into `wifipass`; see the [WiFi Credentials](wifi-credentials) guide.)
