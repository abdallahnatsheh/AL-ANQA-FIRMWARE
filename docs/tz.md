---
title: Timezone
parent: System
nav_order: 6
---

# Timezone

## `tz` — Set Device Timezone

Sets the timezone used by the status bar clock and `wguard` session log timestamps. Saved to NVS — survives reboots.

```
CMD> tz              # open interactive preset picker (recommended)
CMD> tz list         # same as above
CMD> tz +3           # set UTC+3 directly
CMD> tz -5:30        # set UTC-5:30 directly
CMD> tz <posix>      # raw POSIX TZ string (power-user)
CMD> tz status       # show current timezone + current time
```

---

## Interactive Picker (recommended)

Running `tz` with no arguments opens a scrollable list of 31 presets. The current timezone is highlighted.

```
[TZ PICKER]
────────────────────────────────
  UTC-8   US Pacific
  UTC-7   US Mountain
  UTC-6   US Central
> UTC-5   US Eastern          ← current
  UTC-4   Atlantic / Caracas
  UTC-3   Argentina / Sao Paulo
────────────────────────────────
[UP/DN] move  [Enter] select  [q] cancel
```

| Key | Action |
|-----|--------|
| Trackball ↑ / ↓ | Move selection |
| `w` / `s` | Move selection (keyboard) |
| Enter | Apply selected timezone |
| `q` | Cancel — keep current timezone |

---

## Preset List

All 31 built-in presets with their POSIX strings:

| Label | UTC Offset | POSIX string |
|-------|-----------|--------------|
| American Samoa | UTC−11 | `UTC+11` |
| Hawaii | UTC−10 | `HST10` |
| Alaska | UTC−9 (DST) | `AKST9AKDT,M3.2.0,M11.1.0` |
| US Pacific | UTC−8 (DST) | `PST8PDT,M3.2.0,M11.1.0` |
| US Mountain | UTC−7 (DST) | `MST7MDT,M3.2.0,M11.1.0` |
| US Central | UTC−6 (DST) | `CST6CDT,M3.2.0,M11.1.0` |
| US Eastern | UTC−5 (DST) | `EST5EDT,M3.2.0,M11.1.0` |
| Atlantic / Caracas | UTC−4 | `UTC+4` |
| Argentina / Sao Paulo | UTC−3 | `UTC+3` |
| Cape Verde / Azores | UTC−1 | `UTC+1` |
| London / Reykjavik | UTC+0 (BST) | `GMT0BST,M3.5.0/1,M10.5.0` |
| Paris / Berlin | UTC+1 (CEST) | `CET-1CEST,M3.5.0,M10.5.0/3` |
| Cairo / South Africa | UTC+2 | `UTC-2` |
| Jerusalem (IST/IDT) | UTC+2 (DST) | `IST-2IDT,M3.4.4/2,M10.5.0/2` |
| Moscow / Istanbul | UTC+3 | `UTC-3` |
| Nairobi / Riyadh | UTC+3 | `UTC-3` |
| Tehran | UTC+3:30 | `UTC-3:30` |
| Dubai / Baku | UTC+4 | `UTC-4` |
| Kabul | UTC+4:30 | `UTC-4:30` |
| Karachi / Islamabad | UTC+5 | `UTC-5` |
| New Delhi / Mumbai | UTC+5:30 | `UTC-5:30` |
| Kathmandu | UTC+5:45 | `UTC-5:45` |
| Dhaka / Almaty | UTC+6 | `UTC-6` |
| Yangon (Myanmar) | UTC+6:30 | `UTC-6:30` |
| Bangkok / Jakarta | UTC+7 | `UTC-7` |
| Beijing / Singapore | UTC+8 | `UTC-8` |
| Tokyo / Seoul | UTC+9 | `UTC-9` |
| Adelaide | UTC+9:30 (DST) | `ACST-9:30ACDT,M10.1.0,M4.1.0/3` |
| Sydney / Melbourne | UTC+10 (DST) | `AEST-10AEDT,M10.1.0,M4.1.0/3` |
| Noumea / Vladivostok | UTC+11 | `UTC-11` |
| Auckland / Fiji | UTC+12 (DST) | `NZST-12NZDT,M9.5.0,M4.1.0/3` |

Presets marked **(DST)** include automatic daylight saving time rules.

---

## Direct Offset

If your timezone is not in the preset list, set it directly by UTC offset:

```
CMD> tz +3        # UTC+3 (no DST)
CMD> tz -5        # UTC-5 (no DST)
CMD> tz +5:30     # UTC+5:30 (India)
CMD> tz +5:45     # UTC+5:45 (Kathmandu)
CMD> tz -3:30     # UTC-3:30 (Newfoundland)
```

Simple offsets are converted to a valid POSIX string automatically. No DST rules are applied — the clock stays at the fixed offset year-round.

---

## Raw POSIX String

For full DST control, pass a POSIX TZ string directly:

```
CMD> tz EST5EDT,M3.2.0,M11.1.0       # US Eastern with DST
CMD> tz CET-1CEST,M3.5.0,M10.5.0/3  # Central Europe with DST
```

POSIX TZ format: `STD offset DST,start,end` where:
- `STD` = standard timezone abbreviation
- `offset` = hours west of UTC (note: **sign is inverted** — `EST5` means UTC−5)
- `DST` = DST abbreviation
- `start/end` = `M<month>.<week>.<day>/<time>` transition rules

---

## Notes

- The timezone applies to the **status bar clock** and **wguard session log timestamps**
- Saved to NVS — restored automatically on every boot
- `tz status` shows the active POSIX string and the current local time
