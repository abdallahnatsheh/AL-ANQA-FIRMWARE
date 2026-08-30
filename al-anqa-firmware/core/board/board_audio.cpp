/**
 * @file   board_audio.cpp
 * @brief  Board audio HAL — codec bring-up. See board_audio.h.
 *
 * Bring-up order matches the known-good `test spk` / `ev` path on T-Pager:
 *   i2s_driver_install → i2s_set_pin → i2s_zero_dma_buffer → boardCodecBegin
 *   → (optional silence writes) → boardCodecUnmute when real audio is ready
 *
 * Amp EN (EXPANDS_AMP_EN via XL9555) starts LOW at boot (boardPowerOn) and is
 * toggled HIGH/LOW HERE around actual playback. Amp-always-on made the ES8311
 * codec's noise floor + I2S-line garbage after uninstall audible as continuous
 * hiss, even in download mode. Safe order — enable AFTER codec is init'd +
 * muted (silent DAC → no blast), disable BEFORE codec/I2S teardown (mute first
 * → let DAC settle → cut amp → then it's safe to uninstall).
 */
#include "board_audio.h"

#if defined(BOARD_TPAGER)
#include <Wire.h>
#include "es8311.h"
#include "board_power.h"            // boardAmpEnable()
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>          // vTaskDelay — settle windows around amp toggling
#endif

void boardCodecBegin(uint32_t sampleRate) {
#if defined(BOARD_TPAGER)
    // Caller must already have i2s_zero_dma_buffer()'d. ES8311 init leaves DAC
    // muted, so both I2S output and codec DAC are producing silence right now.
    es8311Begin(Wire, TPAGER_I2C_ADDR_ES8311, sampleRate);
    // Safe to power the amp on now — silent input → no blast.
    boardAmpEnable(true);
    vTaskDelay(pdMS_TO_TICKS(3));  // brief amp warm-up before unmute
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
    // Mute FIRST so the DAC output ramps to zero before anything else changes.
    es8311SetMute(true);
    i2s_zero_dma_buffer(I2S_NUM_0);
    // Let the DAC's analog stage settle to zero before cutting the amp; without
    // this, cutting amp while a non-zero sample is still on the analog output
    // produces an audible click.
    vTaskDelay(pdMS_TO_TICKS(10));
    // Amp OFF — now the speaker is deaf to anything that happens next
    // (I2S driver uninstall, pins going floating, deep-sleep, download mode).
    // This is the fix for continuous hiss even when the device is idle/off.
    boardAmpEnable(false);
    vTaskDelay(pdMS_TO_TICKS(3));  // let AMP_EN transition complete before I2S uninstall
    // Full codec power-down per ES8311 User Guide §9.1 + §9.2: mute alone leaves
    // the analog reference/bias circuits running, so the DAC outputs a small bias
    // voltage the amp catches on any transition. Collapsing the codec to standby
    // guarantees a defined (silent) state until the next boardCodecBegin().
    es8311PowerDown();
#endif
}
