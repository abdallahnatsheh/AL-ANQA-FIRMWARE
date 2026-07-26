#ifndef CONFIG_H
#define CONFIG_H

// T-REX firmware NES config — replaces Anemoia's sketch-root config.h
// No TFT_eSPI pins (we use LovyanGFX via callback).
// No GPIO controller pins (we use T-Deck keyboard + trackball).

#define VIDEO_STANDARD  1   // 1 = NTSC
#define FRAMESKIP           // skip every other frame for performance

// I2S audio — T-Deck hardware pins
#define I2S_BCLK_PIN    7
#define I2S_LRC_PIN     5
#define I2S_DOUT_PIN    6

// SCREEN_SWAP_BYTES: do NOT define — LovyanGFX handles byte-swapping internally
// #define SCREEN_SWAP_BYTES

// ROM backend: LRU cache (reads from SD into a RAM LRU cache)
// FLASH backend is not used (would wear the flash)

// ControllerTypes.h not needed (we use keyboard + trackball)

#endif
