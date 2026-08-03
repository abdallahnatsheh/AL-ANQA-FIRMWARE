---
title: NES Emulator
lang: en
parent: System
nav_order: 11
---

# NES Emulator — `game` / `gm`
{: .no_toc }

<span class="label label-yellow">EXP</span>

Play NES games on the T-Deck, built on the vendored **[Anemoia-ESP32](https://github.com/Shim06/Anemoia-ESP32)** core (GPL-3.0, © Shim06; see `NOTICES` #20). Supports **mappers 0–4 + 069** — roughly 90% of the commercial library.

1. TOC
{:toc}

---

## Usage

```
CMD> gm            # open the ROM library picker
CMD> gm nova.nes   # boot a ROM directly from /apps/nes/roms/
```

Put your `.nes` files in `/apps/nes/roms/` on the SD card. Bare `gm` opens a retro **library picker** (trackball or `W`/`S` to scroll, scrollbar shows position, `Enter` to load).

---

## Controls

| Input | NES button |
|-------|-----------|
| `WASD` / trackball | D-pad |
| `k` | B |
| `l` | A |
| `Space` | Select |
| `Enter` / trackball-click | Start |
| `e` | Save state |
| `r` | Load state |
| `q` | Back to ROM library (press again there to exit to CLI) |

- **Save states:** one slot per ROM at `/apps/nes/states/<CRC32>.state` — `e` saves, `r` loads (a 1.5 s toast confirms).
- **Audio** plays through the I2S speaker; `vol` adjusts it live.
- **Auto-lock is suppressed** for the whole session, so the screen won't lock mid-game (an explicit `lock` or the panic key still work).

---

## Legal

Use only ROMs you are legally entitled to run. Open-source / homebrew titles — e.g. [Nova the Squirrel](https://github.com/NovaSquirrel/NovaTheSquirrel) (GPL-3.0 code, MMC1) — are a fully-legal way to test the emulator.

---

## SD layout

| Path | Contents |
|------|----------|
| `/apps/nes/roms/<name>.nes` | your ROMs |
| `/apps/nes/states/<CRC32>.state` | save states (one per ROM) |

Not yet built: second controller, in-game menu, battery-backed SRAM persistence.
