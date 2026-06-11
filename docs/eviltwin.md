---
title: Evil Twin
parent: WiFi Attacks
grand_parent: WiFi
nav_order: 2
---

# Evil Twin

## `eviltwin` / `et`

Clones a nearby AP and runs a captive portal to capture credentials. Run `et` to open the network picker.

```
CMD> et
```

### Strategy by target type

| Target auth | MAC strategy | Effect |
|-------------|-------------|--------|
| Open network | Clone real AP's exact MAC + channel | Devices reconnect seamlessly |
| WPA/WPA2 | Random locally-administered MAC + deauth real AP | Portal captures credentials |

**Adaptive deauth** — deauth bursts fire every 8 seconds and pause automatically while a portal client is connected so the credential form can be submitted.

### Portal pages

Two built-in templates (Google login, Router firmware update) ship in firmware. Drop additional `.html` files into `/apps/eviltwin/portal/` on the SD card.

Press **`p`** to open the portal picker — it lists **all** built-in and SD templates across pages (`n`/`p` to page, number key to select). The currently-active page is highlighted, and a green on-screen notice confirms each switch. Switching restarts the captive-portal DNS so the "Sign in to network" popup keeps firing on new clients.

**Portal compatibility** — the credential grabber is path- and field-agnostic, so almost any captive-portal HTML works unmodified:

- Accepts form submissions to `/post` *or* `/get`, via **GET or POST** (also catches odd paths via the fallback handler).
- Recognizes username fields named `email`, `user`, `username`, `uname`, `login`, `account`, `phone`, `identifier`, `id`, `name`, … and password fields named `password`, `pass`, `pwd`, `psw`, `passcode`, `pin`. Non-credential fields (`remember`, `csrf`, `token`, …) are ignored.

> If a portal still shows no creds, check that its form actually submits the entered values (some "prank" pages have no real form). You do **not** need to edit the HTML for field names.

### Credential storage

Captured credentials are buffered **in RAM during the session** (up to 30) — they are deliberately *not* written to SD while the AP/deauth radio is active, because concurrent WiFi + SD DMA can corrupt the card (ESP32-S3 GDMA rule). Each capture records a timestamp.

They are flushed to `/apps/eviltwin/creds.csv` (append-only, timestamps preserved):

- automatically when you quit (`q`), in the safe window after the radio is torn down, and
- on demand with **`s`** (a mid-session checkpoint — pauses the monitor radio around the write).

### Keys

| Key | Action |
|-----|--------|
| `p` | Pick portal page (built-in + all SD templates, paginated) |
| `c` | View captured credentials (portal keeps running) |
| `s` | Checkpoint captured credentials to SD |
| `q` | Stop Evil Twin (auto-saves remaining creds) |
