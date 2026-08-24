---
title: Chromecast Control
lang: en
parent: Network Recon
nav_order: 11
---

# Chromecast Control — `cast` / `ca`
{: .no_toc }

Discover and control **Google Cast** devices on your Wi-Fi — Chromecast, Google TV, Nest displays, and cast-enabled TVs. Launch a video, cast a URL or a saved item, control playback, and **share photos/videos straight off the SD card**.

**Own devices only.**

1. TOC
{:toc}

---

## How it works

A Cast device exposes two control channels; `cast` uses both:

- **DIAL** (`:8008`, plain HTTP) — launches apps like YouTube. Light, no TLS. This is the "rickroll" path.
- **Cast v2** (`:8009`, TLS + protobuf) — real playback: load a media URL, play/pause/stop, volume, and now-playing status.

For **local files**, a Cast device can only *fetch from a URL* — so the T-Deck briefly becomes a small **HTTP server** and hands the device a `http://<t-deck-ip>:8123/<file>` URL to pull (with HTTP Range support so video can seek).

## Usage

You must be on the same Wi-Fi as the TV (`cw`).

```
CMD> cw MyWiFi password     # join the LAN
CMD> ca                     # scan (mDNS) + interactive picker
CMD> ca 192.168.1.42        # jump straight to a device's menu
CMD> ca rickroll 192.168.1.42
CMD> ca saved 192.168.1.42 "Big Buck Bunny"
CMD> ca share 192.168.1.42 photo.jpg
CMD> ca launch 192.168.1.42 https://example.com/clip.mp4
CMD> ca vol 192.168.1.42 30
CMD> ca stop 192.168.1.42
```

## Interactive mode

`ca` (no target) scans via mDNS, then opens a device picker → action menu. Everything is drivable by **trackball or keyboard**:

- **Trackball ↑/↓** or **`w` / `s`** — move the selection
- **Click** or **Enter** — activate
- **`1`–`9`** — quick-pick a row in one press
- **`u`** — rescan (device picker) · **`q`** — back one level

**Action menu:** Rickroll · Saved content · Share photo/video · Play/Pause/Stop · Volume ± · Mute · Now playing. Destructive actions (Rickroll, Stop) confirm first.

## Saved content — `/apps/cast/media.csv`

Self-seeds on first run. One entry per line, `name,target`:

```
# name,url-or-youtube-id
Rickroll,dQw4w9WgXcQ
Big Buck Bunny,https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4
My Stream,http://192.168.1.10:8080/stream.m3u8
```

An `http(s)` target plays via **Cast v2**; a bare id launches **YouTube via DIAL** (auto-detected, tagged `[url]` / `[yt]`).

## Share local files — `/apps/cast/share/`

Drop images (`.jpg .png .gif .webp .bmp`) and videos (`.mp4 .webm .m4v`) into `/apps/cast/share`, then **"Share photo/video (SD)…"** (or `ca share <ip> <file>`). The T-Deck serves the file and the TV fetches it. A live `requests / served KB` counter shows progress; `[q]` stops.

## Files

| Path | Contents |
|------|----------|
| `/apps/cast/devices.csv` | discovered Cast devices (`ip,name,model`) |
| `/apps/cast/media.csv` | saved content (`name,url-or-youtube-id`) |
| `/apps/cast/share/` | local photos/videos to cast |

## Honest limits

- **Cast v2 uses TLS** — the ~30–40&nbsp;KB handshake DRAM cost. If `:8009` fails, the DIAL path still works.
- **Sharing a video keeps the T-Deck busy** as the file server until you press `[q]` (images fetch once).
- **No transcoding** — the TV must natively support the codec (mp4/H.264, webm/VP8-9 play; mkv/avi/HEVC generally won't).
- DIAL YouTube launching works today but Google has narrowed DIAL over the years; Cast v2 is the durable path.
- **Own networks / own devices only.**
