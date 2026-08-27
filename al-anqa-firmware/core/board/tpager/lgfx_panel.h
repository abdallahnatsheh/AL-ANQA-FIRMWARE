/**
 * @file   lgfx_panel.h  (T-Lora Pager variant)
 * @brief  LovyanGFX panel config for the T-Pager's ST7796 480x222 SPI display.
 *
 * Orientation constants are now CONFIRMED against LilyGoLib's LilyGo_LoRa_Pager.cpp
 * (st7796_init_list + rotation_config), not guessed:
 *   - invert = true          (init list issues 0x21 INVON)
 *   - rgb_order = false/BGR   (MADCTL 0x36 = 0x48 → BGR bit set)
 *   - native portrait 222x480, the 222 axis centred in the ST7796's 320-wide
 *     controller RAM → offset_x = 49 (49 + 222 + 49 = 320). LilyGo's landscape
 *     rotation is MADCTL 0xE8 with offset_y=49; here we keep native-portrait
 *     geometry and let DisplayManager::init() setRotation() to 480x222 landscape
 *     (LGFX swaps col/row start across rotation).
 *   - AW9364 backlight on BL=42 (a 1-wire pulse-count LED driver, NOT plain PWM).
 *     Light_PWM below drives full brightness on power-up; a proper AW9364 dimmer
 *     is a later refinement (see the T-Pager hardware memory).
 * Bench-verify: if the image is shifted 49px or mirrored, adjust offset axis /
 * rotation to match rotation_config in LilyGo_LoRa_Pager.cpp.
 *
 * The class is named LGFX to match the type DisplayManager expects; only one
 * variant's lgfx_panel.h is ever compiled (selected by the LGFX_T-Deck.h
 * forwarder), so there is no clash with the T-Deck's LGFX.
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <AW9364LedDriver.hpp>   // SensorLib — vetted AW9364 pulse-dimmer
#include "pins.h"                // BOARD_BL_PIN

// Native (unrotated) panel geometry. DisplayManager rotates to 480x222 landscape.
static const uint32_t TFT_WIDTH = 222;
static const uint32_t TFT_HEIGHT = 480;

// LovyanGFX light backend for the AW9364 1-wire pulse-counted backlight driver.
// The AW9364 has 16 brightness steps (0=off); each fast LOW->HIGH pulse steps the
// level. This maps LovyanGFX's 0..255 setBrightness() onto those 16 steps, so every
// existing dim/blank/sleep path (PowerSaveManager, tft.sleep()) now actually works.
class Light_AW9364 : public lgfx::ILight {
  AW9364LedDriver _drv;
public:
  bool init(uint8_t /*brightness*/) override {
    _drv.begin(BOARD_BL_PIN);
    _drv.setBrightness(MAX_BRIGHTNESS_STEPS);   // full at boot (splash must stay lit;
    return true;                                // LovyanGFX calls init(0) — ignore it)
  }
  void setBrightness(uint8_t b) override {
    uint8_t step = ((uint16_t)b * MAX_BRIGHTNESS_STEPS + 127) / 255;   // 0..255 -> 0..16
    if (b > 0 && step == 0) step = 1;   // any nonzero level stays visible, never off
    noInterrupts();                     // ~30us: keep the pulse train glitch-free
    _drv.setBrightness(step);
    interrupts();
  }
};

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  Light_AW9364 _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;               // WIP — confirm vs LilyGoLib
      cfg.freq_write = 40000000;      // WIP — ST7796 tolerant; raise after bring-up
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 35;
      cfg.pin_mosi = 34;
      cfg.pin_miso = 33;
      cfg.pin_dc = 37;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 38;
      cfg.pin_rst = -1;               // display reset is via the XL9555 expander (not connected as a GPIO)
      cfg.pin_busy = -1;
      cfg.memory_width  = 320;        // ST7796 controller RAM is 320x480
      cfg.memory_height = 480;
      cfg.panel_width   = TFT_WIDTH;  // visible 222 (portrait), centred in the 320 RAM
      cfg.panel_height  = TFT_HEIGHT; // visible 480
      cfg.offset_x = 49;              // (320 - 222) / 2 = 49  (from rotation_config)
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;              // 0x21 INVON in st7796_init_list
      cfg.rgb_order = false;          // MADCTL 0x48 → BGR
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel_instance.config(cfg);
    }

    _panel_instance.setLight(&_light_instance);   // AW9364 backlight → setBrightness()

    setPanel(&_panel_instance);
  }
};
