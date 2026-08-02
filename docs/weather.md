---
title: Weather
parent: System
nav_order: 12
---

# Weather

## `weather` / `wx` — Current conditions (Open-Meteo, no API key)

Fetches current weather for the [Home Launcher](home) hero from the
[Open-Meteo](https://open-meteo.com) API — **free and keyless** (no API key, so
nothing secret is stored on the SD card).

> **Weather cannot be known offline.** It is live server data. GPS/location only
> tells the query *where* you are — the actual temperature and conditions come
> from the network. So this needs **WiFi** (`cw`).

```
CMD> wx                        # show status (location / units / last reading)
CMD> wx loc <lat> <lon>        # set location (when there is no GPS fix)
CMD> wx units metric|imperial  # °C or °F
CMD> wx now                    # fetch immediately and show the result
```

On a **T-Deck Plus with a GPS fix this works with zero configuration** — no key,
no location to set.

---

## Location — automatic, best source first

`wx now` picks the **best available** location; you normally never set anything:

1. **GPS fix** *(most accurate)* — on the T-Deck **Plus**, used the moment GPS has
   a fix.
2. **Manual `wx loc <lat> <lon>`** — an explicit override, if you set one.
3. **WiFi IP geolocation** *(coarse, automatic)* — when there's no GPS/manual
   location, the device looks up its approximate location from its public IP via
   `ip-api.com` (free, no key). Cached after the first lookup.

So on a Plus you get GPS automatically, and on the base board it auto-locates over
WiFi — **`wx loc` is only an optional override, never required**.

> **Privacy note:** IP geolocation sends your public IP to `ip-api.com`. A GPS fix
> or a manual `wx loc` avoids that third-party call entirely.

---

## Configuration file — `/config/weather.conf`

**Self-seeded on first boot** as a commented template. Since Open-Meteo is keyless,
there's nothing secret to store — only (optionally) a location:

```ini
# weather.conf — Al-Anqa weather (Open-Meteo, no API key), shown in the `home` launcher
# On the T-Deck Plus with a GPS fix, location is automatic.
# Otherwise set your coordinates below (or run: wx loc <lat> <lon>), then reboot.
# lat=31.9539
# lon=35.9106
units=metric   # or: imperial
```

- Inline `#` comments and surrounding whitespace are stripped by the parser.
- Setting values with `wx loc` / `wx units` rewrites this file.
- **No API key** means no plaintext secret on the removable card — a small OPSEC
  win over key-based weather APIs.

---

## How it fetches (GDMA-safe)

The request uses **plain HTTP** (`WiFiClient`) — Open-Meteo serves the same
keyless response over HTTP, and a weather reading needs no confidentiality. HTTPS
was dropped because the TLS handshake's ~30–40 KB internal-DRAM allocation fails
under the Home launcher, where a full-screen PSRAM sprite and the VLW fonts are
already resident: weather fetched fine from the CLI (`wx now`) but failed inside
the undercover Home app. Plain HTTP has no such memory cost, so it works in both.

The request runs **only** from `wx now` and **Home-launcher entry** — never from
the background input loop. This is deliberate: `netspy`/`isoscan` run
associated-with-promiscuous, and any fetch while promiscuous is live would
corrupt the filesystem/WiFi engine (the [GDMA rule](../troubleshooting)). The
launcher is a benign cover screen with WiFi idle, so fetching there is safe.

The last successful reading is cached in RAM and shown even after WiFi drops
(until reboot). Refresh interval is 15 minutes.

See also: [Home Launcher](home), [Timezone](tz), [WiFi](../wifi).
