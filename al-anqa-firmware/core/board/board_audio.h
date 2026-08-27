/**
 * @file   board_audio.h
 * @brief  Board audio HAL for the shared I2S speaker path.
 *
 * The T-Pager speaker/mic run through an ES8311 codec that needs an MCLK, an APLL
 * clock, and an I2C init; the T-Deck drives I2S directly with none of that. These
 * two macros + boardCodecBegin() let every audio app (notifications, hidden_ssid
 * beep, NES, espvoice, test spk) stay board-agnostic.
 *
 * On the T-Deck the macros expand to the EXACT values the apps used before
 * (false / I2S_PIN_NO_CHANGE) and boardCodecBegin() is a no-op, so T-Deck audio is
 * byte-for-byte unchanged.
 *
 * Usage — in the app's i2s setup:
 *     cfg.use_apll      = BOARD_I2S_USE_APLL;   // (was: false)
 *     pins.mck_io_num   = BOARD_I2S_MCK_PIN;    // (was: I2S_PIN_NO_CHANGE)
 *     ... i2s_driver_install() / i2s_set_pin() ...
 *     boardCodecBegin(sampleRate);              // right after i2s_set_pin()
 *     boardCodecUnmute();                       // once DMA is primed / before play
 *     ... play ...
 *     boardCodecEnd();                          // before i2s_driver_uninstall()
 */
#pragma once
#include "board.h"          // BOARD_* pins (BOARD_I2S_MCLK on the T-Pager)
#include <driver/i2s.h>     // I2S_PIN_NO_CHANGE
#include <stdint.h>

#if defined(BOARD_TPAGER)
#  define BOARD_I2S_USE_APLL  true                 // clean 256*fs MCLK for the ES8311
#  define BOARD_I2S_MCK_PIN   BOARD_I2S_MCLK       // ES8311 needs MCLK routed out
#else
#  define BOARD_I2S_USE_APLL  false
#  define BOARD_I2S_MCK_PIN   I2S_PIN_NO_CHANGE
#endif

// Bring up the audio codec for a just-installed I2S TX channel at `sampleRate`.
// T-Pager: inits ES8311 and leaves DAC muted. Caller MUST i2s_zero_dma_buffer()
// first (same order as test spk / ev). Call boardCodecUnmute() when ready to play.
// T-Deck: no-op.
void boardCodecBegin(uint32_t sampleRate);

// Unmute the DAC after boardCodecBegin(). T-Deck: no-op.
void boardCodecUnmute();

// Mute + scrub DMA before tearing I2S down. T-Deck: no-op.
void boardCodecEnd();
