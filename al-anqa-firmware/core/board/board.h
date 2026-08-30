/**
 * @file      board.h
 * @brief     AL-ANQA board abstraction layer (HAL) — single include that selects
 *            a hardware variant by -DBOARD_* and exposes capability flags.
 *
 * App code and managers must gate on the capability flags below
 * (BOARD_HAS_TOUCH / _TRACKBALL / _ENCODER / _GPS / _LORA / _NFC …),
 * NEVER on the raw board identity macro. This is the seam that lets one
 * source tree build for the T-Deck / T-Deck-Plus and the T-Pager.
 *
 * Undercover / home UI layout is also board-agnostic: each variant's
 * metrics.h defines a COVER_* / UI_* profile (side-by-side home, notes
 * two-pane, chrome heights). Apps gate on those flags/values — adding a
 * new device means filling metrics.h + pins.h, not scattering BOARD_XXX.
 *
 * Variant pin headers live in core/board/<variant>/pins.h.
 * The LGFX panel config is selected separately (see core/display/LGFX_T-Deck.h
 * forwarder) so LovyanGFX is only pulled into files that actually draw.
 */
#pragma once

#if defined(BOARD_TPAGER)
  // ── LilyGo T-Lora Pager (ESP32-S3, ST7796 480x222, TCA8418 kbd, encoder) ──
  #include "tpager/pins.h"
  #include "tpager/metrics.h"
  #define BOARD_HAS_GPS       1   // MIA-M10Q built in
  #define BOARD_HAS_ENCODER   1   // rotary encoder (no trackball)
  #define BOARD_HAS_NFC       1   // ST25R3916 HF (13.56 MHz), SPI CS=39 IRQ=5,
                                  // powered via XL9555 EXPANDS_NFC_EN. Gates
                                  // the `nfc`/`nm` command tree + RFAL link.
  // no BOARD_HAS_TOUCH   (no GT911 on the T-Pager)
  // no BOARD_HAS_TRACKBALL
#else
  // ── LilyGo T-Deck / T-Deck-Plus (ESP32-S3, ST7789 320x240, trackball) ──
  #include "tdeck/pins.h"
  #include "tdeck/metrics.h"
  #define BOARD_HAS_TOUCH        1   // GT911 capacitive
  #define BOARD_HAS_TRACKBALL    1   // 4-way + click trackball
  #define BOARD_HAS_ES7210_MIC   1   // ES7210 mic array on I2S_NUM_1 (test mic / espvoice)
                                     // T-Pager uses an integrated ES8311 codec instead —
                                     // its mic backend is Phase 2 work, so this flag is unset there.
  #if defined(BOARD_TDECK_PLUS)
    #define BOARD_HAS_GPS     1   // L76K/M10Q on the Plus only
  #endif
#endif
