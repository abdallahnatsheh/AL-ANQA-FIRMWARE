/**
 * @file   board_audio.cpp
 * @brief  Board audio HAL — codec bring-up. See board_audio.h.
 *
 * Bring-up order matches the known-good `test spk` / `ev` path on T-Pager:
 *   i2s_driver_install → i2s_set_pin → i2s_zero_dma_buffer → boardCodecBegin
 *   → (optional silence writes) → boardCodecUnmute when real audio is ready
 *
 * Do NOT toggle EXPANDS_AMP_EN here — amp stays on from boardPowerOn() just like
 * test spk / ev. Flipping the amp around I2S init was itself a source of blasts.
 */
#include "board_audio.h"

#if defined(BOARD_TPAGER)
#include <Wire.h>
#include "es8311.h"
#endif

void boardCodecBegin(uint32_t sampleRate) {
#if defined(BOARD_TPAGER)
    // Caller must already have i2s_zero_dma_buffer()'d. Leave DAC muted.
    es8311Begin(Wire, TPAGER_I2C_ADDR_ES8311, sampleRate);
#else
    (void)sampleRate;
#endif
}

void boardCodecUnmute() {
#if defined(BOARD_TPAGER)
    es8311SetMute(false);
#endif
}

void boardCodecEnd() {
#if defined(BOARD_TPAGER)
    es8311SetMute(true);
    i2s_zero_dma_buffer(I2S_NUM_0);
#endif
}
