# Plan: Port AL-ANQA firmware from LilyGo T-Deck → LilyGo T-Pager (via a board-abstraction layer)

## Context

AL-ANQA is a mature ESP32-S3 pentest firmware built entirely for the **LilyGo T-Deck / T-Deck Plus**. The user wants to bring it to the **LilyGo T-Lora Pager** — a newer ESP32-S3 handheld. The device is the same MCU family, so the *radio/attack/crypto engine* (the actual value) is portable with zero logic change; the coupling is concentrated in the **presentation + input + power + audio drivers**, which are hardwired to T-Deck silicon.

Today there is **no board-abstraction layer**: pins live in one header (`core/system/utilities.h`) but display pins are *duplicated* in the LGFX config, audio I2S setup is copy-pasted across 5 files, and the only board conditional in the whole tree is `BOARD_TDECK_PLUS` (GPS gate). The goal of this plan is to introduce a thin **HAL (board layer)** on the T-Deck *without changing its behaviour*, then add the T-Pager as an additive `BOARD_TPAGER` path — so both boards build from one tree and app code stays board-agnostic.

**This document is a plan only. No code is changed here.** Execution stays on `feature/pentest-enhancements` (never `main` without an explicit ask); the user compiles/flashes on real hardware, CI is the compile-gate.

### Feasibility verdict: **YES — a bounded port, not a rewrite.**
The decisive fact is the T-Pager has a **full QWERTY keyboard**, so the typing-driven CLI paradigm survives. ~60–70% of the codebase (all `wifi/`, `bluetooth/`, crack engine, DuckyScript, SD app logic, parsers) ports with no change because it talks only to managers (`DisplayManager` / `SDCardManager` / `BatteryManager` / `InputHandling`), never to hardware.

### Locked decisions (from the user)
1. **Approach: HAL-first** (extract the board layer on the T-Deck, verify unchanged, then add T-Pager additively).
2. **Encoder horizontal nav: modifier + rotate** — encoder rotate = UP/DOWN, push = CLICK; **hold Sym while rotating = LEFT/RIGHT** for the 11 four-way UIs.
3. **Scope: port the existing suite first.** LoRa / NFC / IMU are **deferred** to a later effort (see "Deferred").
4. **UI: responsive helpers + hybrid redesign.** Build metric-derived layout helpers so apps stop using magic numbers; reflow everything through them; *and* redesign the handful of screens the widescreen aspect actually changes (status bar, home grid, tables, undercover cover). Not just "make it fit."
5. **Undercover disguise: redesign for landscape.** The cover was built to look like a portrait phone; on a 480×222 widescreen it must become a credible landscape look (list+editor two-pane notes / tablet-style home), not a stretched phone.

### The screen is a different SHAPE, not just size
T-Deck = 320×240 (1.33:1). T-Pager = **480×222 (≈2.16:1)** — **+160px wider, −18px shorter**. This is the crux of the UI work: the width is a resource to exploit (more table columns, two-pane views, roomier status bar), and the −18px height is the hazard (bottom-anchored layouts overflow). A portrait-phone look does not translate — hence the responsive-helper + selective-redesign strategy below.

---

## Hardware delta (what actually differs)

| Subsystem | T-Deck (current) | T-Pager | Driver work |
|---|---|---|---|
| MCU / PSRAM / flash | ESP32-S3 / 8MB / 16MB | ESP32-S3 / 8MB / 16MB | none — identical |
| Display | ST7789, 320×240, SPI | **ST7796, 480×222**, SPI | new LGFX config + UI reflow |
| Keyboard | I2C ASCII coprocessor @ `0x55` | **TCA8418** matrix scanner, I2C | **new driver + keymap engine** |
| Navigation | Trackball (4-way + click) | **Rotary encoder** (A40/B41/push7) | new adapter → `TrackballEvent` |
| Touch | GT911 capacitive | **none** | none — degrades to `isPresent()=false` |
| Audio | raw I2S DAC (spk) + ES7210 (mic) | **ES8311** (mic+spk integrated) | new codec driver |
| Power/battery | bare ADC divider (Pangodream, GPIO4) | **BQ25896 charger + BQ27220 fuel gauge** (I2C) | **new I2C fuel-gauge driver** |
| GPS | L76K/M10Q (Plus only, GPIO44/43) | **MIA-M10Q** built-in (TXD12/RXD4/PPS13) | widen GPS gate + repin |
| SD | SPI2 shared, CS=39 | SPI, CLK35/MOSI34/MISO33 | repin (verify bus topology) |
| RTC | none (NTP/GPS clock) | **PCF85063A** | optional bonus later |
| New radios | — | SX1262 LoRa, ST25R3916 NFC, BHI260AP IMU, DRV2605 haptics | **deferred** |

> ⚠️ Correction to a mid-exploration guess: T-Pager power is **BQ25896 + BQ27220**, *not* AXP2101. The `BatteryManager` internals must be reimplemented as a BQ27220 I2C fuel-gauge read (not XPowersLib).

---

## Confirmed T-Pager hardware reference (from LilyGo wiki + LilyGoLib)

**The single biggest finding: an XL9555 I2C GPIO expander gates every peripheral power rail.** Unlike the T-Deck's one `BOARD_POWERON` GPIO, on the T-Pager the display, keyboard, LoRa, GPS, NFC, audio amp and SD are each powered/reset through the expander. **Nothing comes up until I2C + the XL9555 are initialised and the rails enabled in order.** This is a new, load-bearing boot sequence.

### GPIO map (authoritative)
| Subsystem | Pins |
|---|---|
| **Shared SPI** (display, LoRa, NFC, **SD**) | MOSI 34 · MISO 33 · SCK 35 |
| **Shared I2C** (kbd, RTC, IMU, audio, gauge, charger, haptic, expander) | SDA 3 · SCL 2 |
| Display ST7796 | CS 38 · DC 37 · BL 42 (no RST/BUSY broken out) |
| LoRa SX1262 | CS 36 · RST 47 · BUSY 48 · DIO1 14 |
| Rotary encoder | A 40 · B 41 · push 7 |
| Keyboard TCA8418 | INT 6 · backlight 46 (power+reset via expander) |
| Audio ES8311 | I2S WS 18 · SCK 11 · MCLK 10 · DOUT 45 · DIN 17 |
| GPS MIA-M10Q | TX 12 · RX 4 · PPS 13 |
| SD card | **CS 21** (shared SPI; power + insert-detect via expander) |
| NFC ST25R3916 | CS 39 · IRQ 5 |
| IMU BHI260AP | INT 8 |
| RTC PCF85063A | INT 1 |
| Free UART1 (12-pin socket) | TX 43 · RX 44 (note: these are the T-Deck's GPS pins — free here) |

### I2C address table
| Device | Addr | | Device | Addr |
|---|---|---|---|---|
| ES8311 codec | 0x18 | | TCA8418 keyboard | 0x34 |
| XL9555 expander | 0x20 | | DRV2605 haptic | 0x5A |
| BHI260AP IMU | 0x28 | | BQ27220 fuel gauge | 0x55 |
| PCF85063A RTC | 0x51 | | BQ25896 charger | 0x6B |

### XL9555 expander power-rail map
Haptic enable · **audio-amp enable** · **LoRa power** · **GNSS power** · **NFC power** · **keyboard power + reset** · **SD-card power + insert-detect**. (Exact bit numbers differ slightly between LilyGo sources — confirm against `LilyGoLib` `variants/tlora_pager` at implementation time.)

**Authoritative reference for init sequences + exact register/bit details: [Xinyuan-LilyGO/LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib)** (its `variants/tlora_pager` + `hal_interface.h` is LilyGo's own 50-function HAL — mine it for the ST7796 orientation, ES8311 codec init, BQ27220 config, and the XL9555 enable order; do not copy wholesale, it's LVGL-based).

---

## The HAL design (board layer)

Introduce a capability-driven board layer. App code and managers gate on **capability flags**, never on board identity.

```
al-anqa-firmware/core/board/
  board.h              ← single include; selects a variant by -DBOARD_* and
                          defines capability flags (BOARD_HAS_GPS / _TOUCH /
                          _TRACKBALL / _ENCODER / _LORA / _NFC …)
  tdeck/
    pins.h             ← extracted from utilities.h (T-Deck/Plus pins)
    lgfx_panel.h       ← current LGFX_T-Deck.h (ST7789, 320×240)
    metrics.h          ← SCREEN_WIDTH/HEIGHT/LINE_HEIGHT/status-bar heights
  tpager/
    pins.h             ← TCA8418 I2C, encoder GPIOs, ES8311, BQ pins, SD 35/34/33
    lgfx_panel.h       ← new: Panel_ST7796, 222×480 native → rotate to 480×222
    metrics.h          ← SCREEN_WIDTH=480, SCREEN_HEIGHT=222, retuned constants
  layout.h             ← NEW responsive helpers (board-agnostic; read metrics):
                          rowY(n), footerY(k)=SCREEN_HEIGHT-k, colX(i,ncols),
                          rightX(w)=SCREEN_WIDTH-w, rowsPerPage(), charCols()=SCREEN_WIDTH/6
```
`layout.h` is the anti-magic-number layer: screens describe position *relatively* (`rowY(3)`, `footerY(26)`, `colX(2,5)`) and both boards render correctly with no per-app constants. This is the "adapt apps to the device" idea applied to the UI.

`board.h` switches once:
```cpp
#if defined(BOARD_TPAGER)
  #include "tpager/pins.h"
  #define BOARD_HAS_GPS 1
  #define BOARD_HAS_ENCODER 1
  // (no BOARD_HAS_TOUCH, no BOARD_HAS_TRACKBALL)
#else            // T-Deck / T-Deck-Plus
  #include "tdeck/pins.h"
  #define BOARD_HAS_TOUCH 1
  #define BOARD_HAS_TRACKBALL 1
  #if defined(BOARD_TDECK_PLUS)
    #define BOARD_HAS_GPS 1
  #endif
#endif
```

**Principle:** push every hardware divergence *down* into `core/board/` + the four manager reimplementations; keep the ~99 app files talking only to the manager APIs. The existing `#ifdef BOARD_TDECK_PLUS` GPS gate is the proven template — this generalises it.

---

## Execution plan (phased)

### Phase 0 — Build scaffold
- `platformio.ini`: clone `[env:T-Deck-Plus]` → `[env:T-Pager]`; keep `board = esp32s3box`, `platform = espressif32@7.0.1`, `default_16MB.csv`; swap `-DBOARD_TDECK_PLUS=1` → `-DBOARD_TPAGER=1`. Add the new env to the CI matrix in `.github/workflows/build.yml`.
- Create `core/board/board.h` + the `tdeck/` variant by **moving** (not rewriting) `utilities.h` pins and `LGFX_T-Deck.h`. Redirect includes. **Success = T-Deck / T-Deck-Plus build byte-for-byte equivalent** (no behaviour change). This is the safety net for everything after.

### Phase 1 — HAL extraction on the T-Deck (no behaviour change)
Extract the seams so the T-Pager can plug in without touching apps:
1. **Metrics**: move `SCREEN_WIDTH/HEIGHT/LINE_HEIGHT` and `promptHeight/outputY` from `core/display/display_manager.h:7-13` into `core/board/<variant>/metrics.h`.
2. **Display**: `DisplayManager` public API (`display_manager.h:17-59`) is already board-agnostic — only `init()` rotation/invert (`display_manager.cpp:17-32`) and the LGFX panel file are board-specific. Keep API; parameterise the panel behind `board.h`.
3. **Input seam** (already clean — `core/input/input_handling.{h,cpp}`): the whole app tree uses only `getKeyboardInput()` / `getTrackballEvent()` (69 + ~30 consumers, all insulated). Keep the interface; the T-Pager will supply a different `input_handling` implementation body. **Preserve inside it**: background pollers (`input_handling.cpp:92-97`), `PowerSave`/`LockScreen` `updateActivity` calls, the `LockScreenManager::intercept()` return-path (`:153`), backspace hold-repeat synthesis, `KEY_AUTOCOMPLETE=0x27`, and the panic-key hook.
4. **AudioManager (new)**: consolidate the 5 copy-pasted I2S setups (`diagnostics/speaker/test_speaker.cpp`, `diagnostics/mic/test_mic.cpp`, `ui/notifications/notification_manager.cpp` ×2, `radio/espnow/espvoice/espvoice.cpp`) behind one manager with a board-specific codec backend. On T-Deck the backend is the current raw-I2S-DAC + ES7210 path (unchanged output).
5. **BatteryManager**: already isolated (`core/system/battery_manager.*`, API `getPct/getVolts/isCharging`). No change on T-Deck; the internals become a board backend.
6. **Pin-literal cleanup**: replace hardcoded `SD.begin(39)` with `BOARD_SDCARD_CS` in `fun/buddy/buddy.cpp:969`, `bluetooth/attacks/fast_pair/fast_pair.cpp:78`, `bluetooth/attacks/ble_spam/ble_spam.cpp:41`, `bluetooth/input/ble_keyboard/ble_keyboard.cpp:641`.

*Gate after Phase 1: both T-Deck envs still compile and behave identically.*

### Phase 2 — T-Pager drivers (`core/board/tpager/` + manager backends)
0. **Board power bring-up (NEW — must be first).** Add an `XL9555` expander driver (I2C 0x20) and a board `powerOn()` that runs at the very start of `setup()`: `Wire.begin(3,2)` → init XL9555 → enable rails in order (keyboard, display, LoRa/GPS/NFC/audio as needed) + release keyboard/GNSS resets → *then* init display/SD/inputs. Nothing else works until this exists. Replaces the T-Deck's single `BOARD_POWERON` HIGH in `display_manager.cpp:18`. Mine the exact enable order from LilyGoLib.
1. **Display** `lgfx_panel.h`: `Panel_ST7796`, memory 222×480, rotation to 480×222 landscape, backlight PWM on BL=42, CS=38/DC=37, shared SPI (MOSI34/MISO33/SCK35). No RST/BUSY pins.
2. **Keyboard (TCA8418)**: new driver producing the **same ASCII contract** the T-Deck coprocessor hides. Owns: matrix scan → keymap, Shift/Sym modifier state (the T-Deck did this in silicon), `0x27` for the autocomplete chord, the panic key, and press/release → synthetic backspace-repeat. Reuse an existing TCA8418 library (e.g. Adafruit_TCA8418 / LilyGo's example) for the scan layer only.
3. **Encoder → `TrackballEvent` adapter**: rotate = `TBALL_UP`/`TBALL_DOWN`, push (GPIO7) = `TBALL_CLICK`, **Sym-held + rotate = `TBALL_LEFT`/`TBALL_RIGHT`** (locked decision). Route through `LockScreenManager::interceptTrackball()` and call `updateActivity()` exactly as the trackball did.
4. **Direct-GPIO leak sites** (bypass the input seam — must be board-aware): HID-forwarders `usb/hid/usb_keyboard.cpp:56,193`, `bluetooth/input/ble_keyboard/ble_keyboard.cpp:257,305,394,463`, `usb/bad_usb/bad_usb.cpp:238,1422`; tpad-gesture reads `ui/lockscreen/lockscreen_manager.cpp:328`, `radio/espnow/espchat/espchat_ui.cpp:1109,1228`; NES d-pad `games/nes/nes_emulator.cpp:108`. Remap each to the encoder/keyboard under `#if BOARD_HAS_ENCODER`.
5. **Power backend**: reimplement `BatteryManager` internals as a **BQ27220** fuel-gauge I2C read (percent/voltage) + **BQ25896** charge-status; keep the public API. Route the T-Pager power-on through the PMU rather than a bare GPIO10.
6. **Audio backend**: **ES8311** codec init (playback + mic) over the shared I2C/I2S; retune the I2S pin config (keep the `mck_io_num` discipline — see the GPIO0 gotcha memory).
7. **GPS**: widen every `#ifdef BOARD_TDECK_PLUS` to `#if defined(BOARD_HAS_GPS)` in `hardware/gps/gps_manager.{h,cpp}` + the `utilities.h`/`pins.h` GPS block; add MIA-M10Q pins (TXD12/RXD4). The existing `initModule` M10Q autodetect likely covers it.
8. **SD + SPI topology (CONFIRMED shared)**: SD is CS=21 on the **same** SPI bus as display+LoRa+NFC (MOSI34/MISO33/SCK35), and its power + insert-detect are behind the XL9555. So the GDMA rule + `flushSPI()` DMA-drain + CS-deassert discipline **all stay fully load-bearing** (more devices on the bus, not fewer). Repin CS; deassert LoRa **and NFC** CS before SD transactions (T-Deck only had to fend off LoRa CS — now NFC CS shares too). Enable SD power via the expander before mount.

### Phase 3 — UI for 480×222 (responsive helpers + hybrid redesign)
Two sub-phases: first make everything *correct and portable* (3a), then make the screens the aspect actually changes *good* (3b). Changing the two metrics defines auto-fixes ~321 width/height-relative references and width-relative idioms (`SCREEN_WIDTH-20`, `fillRect(0,y,SCREEN_WIDTH,…)`); the helpers below absorb the rest.

**Phase 3a — responsive-helper layer + reflow through it (correctness pass, ~15–20 files)**
Introduce `core/board/layout.h` (see HAL design) and migrate magic numbers to it:
- **Height shrink −18px is the sharper hazard**: bottom-anchored footer Y-literals overflow. Replace `210/212/214` in `wifi/attacks/dpwo/dpwo.cpp`, `226/230` in `wifi/intel/netspy.cpp`, `214` in `wifi/attacks/wps/wps.cpp` with `footerY(k)`.
- **Rows-per-page constants** (`7/8/10/13` in bmon/scanwifi/ssh/etc.) → `rowsPerPage()`.
- **Column-X literals** tuned to 320 (bmon `CX_*` `bmon.cpp:409-414`, dpwo `DP_COL_*`, netspy) → `colX(i,ncols)`.
- **6px-grid COLS** frozen at 52 (`text_editor.cpp:36`, `ssh_client.cpp:47`) → `charCols()` (≈80 at 480).
- **Fixed sprites**: pwn HUD (`pwn.cpp:1309` `BOX_W/H`, `RX=226`), csidetect radar (`csidetect.cpp:72`), buddy (`BUDDY_X_CENTER=77`) → re-center via `rightX()`/derived centers.

*Outcome of 3a: every screen fits and nothing overflows on either board — but tables still look 320-ish with a right-side gap.*

**Phase 3b — widescreen redesign (exploit the shape, the screens that matter)**
- **Status bar** (`display_manager.cpp:143-223`): the right cluster (WGuard/GPS/BT/battery) is packed to the old 320 edge leaving a 160px gap. Re-space across 480, and use the room for fuller info (longer SSID/IP, larger clock). Fix the badge-X duplicated in `setEcActive/setMwActive/setCcActive`.
- **Tables gain columns** (`wm`, `bmon`, `netspy`, `dpwo`, `isoscan`): 480÷6 ≈ 80 chars/row vs 53 — show fuller data (full MAC **and** vendor, extra fields that were truncated) instead of just spacing out the old columns. This is where the width pays off.
- **Two-pane where it helps** (selective, not everywhere): list-on-left + detail-on-right for `netspy`/`bmon`/`ble_info`/`sw` — the shorter height shows fewer rows, so a persistent side detail panel beats a full-screen detail overlay.
- **Editor / SSH terminal**: ~80 columns is a genuinely better terminal — verify wrap/scroll math against `charCols()`.
- **Home launcher grid** (`home_ui.cpp:39-45`): the 4×2 grid at 480×222 gives very wide, short cells. Redesign to suit the widescreen (wider icon+label cells, or 5×2/6×2) rather than stretch 4×2. Grid cells are already metric-derived (`home_ui.cpp:87-88`) so the mechanics are there; it's a layout choice.

**Phase 3c — undercover disguise landscape redesign (its own workstream)**
The cover (`cover_kit` canvas + `home_ui` + `notes_ui`) was designed to read as a **portrait phone**; stretched to 480×222 it reads as a broken phone and *loses disguise credibility* — the whole point of the feature. Redesign it for landscape:
- New disguise mockup first (the project's convention: an HTML mockup approved before build, e.g. the prior `~/Downloads/trex-undercover-notes.html`; **real px = mockup/2** — a 480×222 device means a 960×444 mockup). Target a credible widescreen look: **landscape Notes with a list+editor two-pane**, and a tablet/PDA-style home.
- Rebuild `notes_ui.cpp:231-243` and `home_ui.cpp` layout against the approved mockup (the sprite/font/passphrase/dim-wake plumbing in `cover_kit` is reused unchanged; only the drawing changes).
- Input: the disguise loses touch (no GT911) — it now drives via keyboard + encoder (already supported fallbacks), so the two-pane nav maps to encoder U/D + Sym+rotate L/R + click.
- The `cover_statusbar.h` shorter bar (`COVER_SB_H=22`) is retained/retuned for landscape.

### Phase 4 — Feature gating
- Introduce board conditionals in `command_manager.cpp setupCommands()` (currently fully unconditional): gate `test touch` off on T-Pager (`#if BOARD_HAS_TOUCH`); leave a slot for future `#if BOARD_HAS_LORA/NFC` commands (deferred). Add a "Radio/RF" category string for when they land.
- `TouchManager`: no driver change — `isPresent()` returns false on T-Pager and all 3 consumers (`home_ui`, `notes_ui`, `touch_test`) already degrade to keyboard/encoder. `test touch` becomes a "not present" readout. (The undercover cover no longer *relies* on touch anyway after the Phase 3c landscape redesign, which is keyboard+encoder driven.)

### Phase 5 — Bring-up on hardware (user)
Order of first-boot checks on the real T-Pager: power-on rail → display init/orientation → **keyboard char map** (type every key, verify ASCII + Sym chords + autocomplete `0x27`) → **encoder nav** (U/D, click, Sym+rotate = L/R) → SD mount → battery % (BQ27220) → WiFi scan (`sw`) → one attack end-to-end (e.g. `wm` or `sw`→`cw`) → lock screen PIN + idle-sleep (confirm the input-path hooks survived the driver swap).

---

## Deferred (not in this plan)
- **LoRa scanner/logger** (SX1262) — the long-pending feature the T-Pager hardware finally justifies. First candidate for the follow-up.
- **NFC** (ST25R3916 read/write/emulate) — **planned:** [docs/plans/nfc-ultimate-tpager.md](nfc-ultimate-tpager.md) (CLI-first `nfc`/`nm`, Flipper-HF Phase 1 → lab-depth Phase 2+).
- **IMU gestures** (BHI260AP), **haptics** (DRV2605), **hardware RTC** (PCF85063A).

---

## New dependencies / libraries
| Need | Candidate | Notes |
|---|---|---|
| **XL9555 expander** | LilyGoLib / a generic XL9555 lib | **NEW & critical** — gates all peripheral power; needed before anything boots |
| ST7796 panel | **LovyanGFX** (already a dep) | Supports ST7796 natively — new config file, no new lib |
| TCA8418 scan | **Adafruit_TCA8418** or LilyGo's example | I2C 0x34; scan layer only, the keymap/modifier engine is ours |
| ES8311 codec | Espressif ES8311 driver / LilyGo example | I2C 0x18; new AudioManager backend (mic may be PDM — verify) |
| BQ27220 + BQ25896 | LilyGoLib / a BQ27220 Arduino lib | I2C 0x55 / 0x6B; `BatteryManager` backend, replaces Pangodream ADC |
| SX1262 LoRa (deferred) | **RadioLib** (already a dep) | Already in `lib_deps`; CS36/RST47/BUSY48/DIO1-14 when LoRa work starts |
| ST25R3916 NFC (deferred → planned) | LilyGo ST25R3916-fork + NFC-RFAL-fork | See [nfc-ultimate-tpager.md](nfc-ultimate-tpager.md); CS39 / **IRQ GPIO5** (not wiki Quick Start INT=1) |
| RTC / IMU / haptic (deferred bonus) | PCF85063A 0x51 / BHI260AP 0x28 / DRV2605 0x5A | Nice-to-haves, not needed for the core port |

Everything else (WiFi/BLE/NimBLE/mbedTLS/crack/SD/DuckyScript) is unchanged. **`Xinyuan-LilyGO/LilyGoLib` is the reference implementation** for every init sequence above.

## Effort sizing (rough, relative)
| Phase | Size | Nature |
|---|---|---|
| 0 scaffold | S | mechanical file moves + env clone |
| 1 HAL extraction (T-Deck) | M | careful, zero-behaviour-change; AudioManager consolidation is the bulk |
| 2 T-Pager drivers | **L** | keyboard keymap engine + power/audio codecs = the real new code |
| 3a reflow via helpers | M | broad, mechanical, ~15–20 files |
| 3b widescreen redesign | M | tables/status/two-pane/home grid |
| 3c undercover landscape | **L** | mockup + rebuild the cover UI |
| 4 feature gating | S | localized to `setupCommands` + capability flags |
| 5 bring-up | M | on-device iteration, mostly keyboard/display/power |

Critical-path / longest-pole items: **TCA8418 keymap engine**, **power+audio codec drivers**, **undercover landscape redesign**.

## RAM / flash budget
- **PSRAM**: the full-screen cover sprite grows 320×240×2 = 154KB → **480×222×2 = 213KB** (+59KB). T-Pager has 8MB PSRAM — fine, but confirm no double-buffering blowout when the undercover redesign adds a second pane sprite.
- **Flash**: 16MB / `default_16MB.csv` — same partitions; new codec/keyboard drivers are small. The T-Pager build won't carry GT911/trackball code (gated out), roughly offsetting the new drivers.
- **DRAM**: watch the Weather/TLS DRAM pressure already noted for the home launcher (HTTP-not-HTTPS workaround) — unchanged, but the larger cover sprite is internal-PSRAM so it doesn't touch DRAM.

## SPI / GDMA topology (CONFIRMED)
Both boards share one SPI bus across display + LoRa + SD (+ NFC on the T-Pager). So the full GDMA discipline carries over unchanged: `flushSPI()` drains LGFX DMA before SD I/O, `SDCardManager::begin()` deasserts other CS lines first (now **LoRa CS36 *and* NFC CS39**, vs just LoRa on the T-Deck), and the "no SD writes while WiFi is APSTA/promiscuous" rule + `ScopedPromiscPause` stay load-bearing. Extra T-Pager wrinkle: **SD power and insert-detect are behind the XL9555 expander** — enable SD power via the expander before mount, and the insert-detect line is an expander read, not a GPIO.

## Phase dependency graph
```
0 scaffold ─▶ 1 HAL extraction (T-Deck, verify identical) ─▶ 2 T-Pager drivers ─┬─▶ 3a reflow ─▶ 3b redesign
                                                                                └─▶ 5 bring-up (needs 2 to boot)
3a ─▶ 3c undercover landscape        4 feature gating can land any time after 0
```
Rule: **never start Phase 2 until Phase 1 proves the T-Deck is byte-for-byte unchanged.** That invariant is the whole safety of HAL-first.

## Risk register
| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **XL9555 power sequence wrong** → nothing boots (blank screen/dead kbd) | Med | **High** | Port LilyGoLib's exact enable order first; bring up expander + rails before any peripheral; it's the gate for the whole board |
| TCA8418 keymap wrong/incomplete (Sym/Shift/autocomplete `0x27`) | High | High | Build a `test keymap` diagnostic that echoes raw scan + resolved char; iterate on HW |
| −18px height overflows footers | Med | Med | `footerY()` helper + a grep for bare Y-literals ≥200 |
| Power driver misreads % (BQ27220 needs config/seal handling) | Med | Med | Validate against charge/discharge; fall back to voltage estimate |
| Undercover redesign scope creep | Med | Med | Gate on an approved mockup first (project convention) |
| Direct-GPIO leak sites missed (HID apps read trackball pins) | Med | Med | The Explore inventory lists all ~6; grep `BOARD_TBOX`/`BOARD_BOOT_PIN` before sign-off |
| HAL extraction regresses the T-Deck | Low | High | Per-phase compile gate + flash-and-parity check on real T-Deck |

## Documentation & memory updates (part of "done")
- `CLAUDE.md` hardware/architecture sections gain a T-Pager column; the `BOARD_TDECK_PLUS`-only notes become capability-flag notes.
- Docs site (`docs/`, bilingual EN/AR via jekyll-polyglot) — a board-support page; note the polyglot build gotchas.
- `man`/`hlp`/autocomplete + README: board-gated commands (touch off on T-Pager; LoRa/NFC placeholders).
- Save a memory once the board layer lands (the HAL seam is a durable architectural fact).

## Rollback & safety
- HAL-first means the T-Deck path is the reference; if a T-Pager change misbehaves, the board is isolated behind `#if BOARD_TPAGER` and can't regress the T-Deck.
- All work on `feature/pentest-enhancements`; **no PR/merge to `main` without an explicit ask.**
- `git diff --cached` sensitive-data scan before every commit — keep real neighbour SSIDs/MACs/cracked passwords out of commits and HW-run notes.

## Honesty / hard truths
- **Keyboard keymap is the deepest new work** — the T-Deck coprocessor resolves Sym/Shift → ASCII in silicon; the TCA8418 driver must reproduce *every* mapping the firmware never sees.
- **The undercover disguise is a genuine redesign**, not a reflow — a widescreen can't fake a portrait phone; it becomes a landscape/tablet disguise or it isn't believable.
- **No touch on the T-Pager** — that's a permanent capability loss, not a bug; the affected UIs already have keyboard/encoder paths.
- **Real MITM limits, GDMA rule, single-radio honesty** all carry over unchanged — the port doesn't change what the hardware can/can't do.

## Residual unknowns (resolve during implementation, from LilyGoLib/HW — the big ones are now answered)
Pins, I2C addresses, SPI topology, and the power-expander model are **confirmed** (see the hardware reference above). What's left is detail best read from `LilyGoLib` source or confirmed on the bench:
- [ ] Exact **XL9555 bit→rail** numbering + the precise enable **order/timing** (LilyGo sources disagree by a bit or two).
- [ ] **ST7796 orientation** constant for 480×222 landscape (rotation + invert) — try LilyGoLib's value first.
- [ ] **TCA8418 keymap** — the full 6×10 matrix → character/label map incl. Sym/Shift/Fn layers (the keymap is ours to build; LilyGo's example is the reference).
- [ ] **ES8311 mic path** — codec ADC vs a separate **PDM** mic (one source says PDM); affects `test mic`/espvoice.
- [ ] **BQ27220** — whether it needs a config/seal sequence for an accurate %; else fall back to voltage.
- [ ] **Encoder detents** — 1 vs 4 counts/detent (tune the step in the adapter).

## Verification
- **Compile gate (CI + user):** after Phase 1, `env:T-Deck` and `env:T-Deck-Plus` must still build green and behave identically (the HAL extraction changed no behaviour). After Phase 2+, `env:T-Pager` compiles. *Do not run `pio` in-session — the user builds/flashes; CI is the gate.*
- **Behaviour parity on T-Deck:** flash a post-HAL build to the T-Deck and confirm no regressions (display, input, audio, battery, one attack) before trusting the T-Pager path.
- **T-Pager bring-up:** the Phase 5 checklist, on hardware, by the user — power rail → display → keyboard char map (incl. Sym chords + autocomplete `0x27`) → encoder nav (U/D, click, Sym+rotate L/R) → SD mount → battery % → WiFi scan → one attack end-to-end → lock-screen PIN + idle-sleep (proves the input-path hooks survived the driver swap).
- **Diagnostics to build for bring-up:** a `test keymap` echo (raw scan + resolved char) and reuse `i2cscan`/`test` for codec/PMU/encoder probing.
