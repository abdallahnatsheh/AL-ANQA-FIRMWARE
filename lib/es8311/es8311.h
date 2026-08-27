/**
 * @file   es8311.h
 * @brief  Minimal ES8311 codec driver (I2C register init) for DAC → speaker.
 *
 * The T-Pager's speaker path runs through an ES8311 codec (I2C 0x18). Raw I2S to
 * its pins produces nothing until the codec is configured, and it REQUIRES an
 * MCLK. This is a compact, Arduino-Wire port of Espressif's es8311.c register
 * sequence + clock-coefficient table (the register writes are IDF-independent, so
 * this works with the legacy driver/i2s.h API this project uses — unlike
 * LilyGoLib's codec, which needs the IDF 5.x i2s_std driver).
 *
 * Usage (T-Pager speaker): configure I2S as master TX with an MCLK output on
 * BOARD_I2S_MCLK, then es8311Begin(Wire, 0x18, sampleRate). MCLK is assumed to be
 * 256 × sampleRate (the ESP32 legacy I2S default when mck_io_num is set). The
 * external speaker amp is enabled separately via the XL9555 (EXPANDS_AMP_EN).
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>

// Configure the ES8311 for 16-bit I2S-slave audio at sampleRate (Hz).
// enableAdc = true also brings up the ADC (mic) path (the T-Pager routes its mic
// through the ES8311 ADC on I2S DIN). Returns false if the chip isn't found or
// the rate has no MCLK=256*fs coeff.
// Leaves the DAC MUTED — prime I2S with silence (i2s_zero_dma_buffer / silent
// writes) then call es8311SetMute(false). boardCodecBegin() does this for you.
bool es8311Begin(TwoWire& wire, uint8_t addr, uint32_t sampleRate, bool enableAdc = false);

// ADC (mic) input gain via the analog PGA. Valid range 0..7 = 0/6/12/18/24/30/36/42 dB;
// values >7 are out of range and leave the mic silent (they are NOT louder). Clamped to 7.
void es8311SetMicGain(uint8_t gain_0_to_7);

// DAC output volume, 0..100 (maps to reg 0x32; ~75 ≈ 0 dB).
void es8311SetVolume(uint8_t vol);

void es8311SetMute(bool mute);
