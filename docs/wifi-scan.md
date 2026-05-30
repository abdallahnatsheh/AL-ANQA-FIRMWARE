---
title: Scan & Connect
parent: WiFi
nav_order: 1
---

# Scan & Connect

---

## `scanwifi` / `sw` — Scan WiFi Networks

```
CMD> sw
```

Scans all 2.4 GHz networks and displays a paginated table with index, SSID, RSSI, auth type, and WPS flag.

| Key | Action |
|-----|--------|
| `l` / `a` | Next / previous page |
| `u` | Re-scan |
| `q` | Quit |

The scan result is cached — use `show wifi` to view it again without rescanning.

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

## `clearwifi` / `clrw` — Erase Saved Credentials

```
CMD> clrw
```

Erases all saved WiFi passwords from NVS (non-volatile storage). The next connection to a known network will prompt for the password again.
