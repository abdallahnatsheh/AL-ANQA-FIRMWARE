/**
 * @file   pins.h  (T-Lora Pager variant)
 * @brief  LilyGo T-Lora Pager pin map (ESP32-S3).
 *
 * Authoritative GPIO map from the LilyGo wiki + LilyGoLib (variants/tlora_pager).
 * See docs/plans/t-pager-port.md for the full hardware delta.
 *
 * ⚠️ Phase 0 SCAFFOLD: pins are confirmed, but the T-Pager env does NOT fully
 * build/boot yet — the drivers these pins feed (XL9555 power bring-up, TCA8418
 * keyboard, encoder adapter, ES8311 audio, BQ27220 fuel gauge, ST7796 panel)
 * arrive in Phase 2. Nothing here is wired into a live boot path until then.
 *
 * KEY DIFFERENCE vs the T-Deck: there is no single power-on GPIO. An XL9555 I2C
 * GPIO expander (0x20) gates every peripheral power rail (display, keyboard,
 * LoRa, GPS, NFC, audio-amp, SD). board powerOn() (Phase 2) must init I2C +
 * the expander and enable rails in order before anything else comes up.
 */
#pragma once

// ── Shared I2C bus (kbd, RTC, IMU, audio, fuel gauge, charger, haptic, expander) ──
#define BOARD_I2C_SDA          3
#define BOARD_I2C_SCL          2

// ── Shared SPI bus (display, LoRa, NFC, SD) ──
#define BOARD_SPI_MOSI         34
#define BOARD_SPI_MISO         33
#define BOARD_SPI_SCK          35

// ── Display ST7796 (480x222) — no RST/BUSY broken out ──
#define BOARD_TFT_CS           38
#define BOARD_TFT_DC           37
#define BOARD_TFT_BACKLIGHT    42
#define BOARD_BL_PIN           42

// ── SD card (shared SPI; power + insert-detect via XL9555 expander) ──
#define BOARD_SDCARD_CS        21

// ── Rotary encoder (replaces the trackball) ──
#define BOARD_ENCODER_A        40
#define BOARD_ENCODER_B        41
#define BOARD_ENCODER_PUSH     7

// ── Keyboard TCA8418 matrix scanner (I2C 0x34; power+reset via expander) ──
#define BOARD_KEYBOARD_INT     6
#define BOARD_KEYBOARD_BL      46

// ── Audio ES8311 codec (I2C 0x18 + I2S) ──
#define BOARD_I2S_WS           18
#define BOARD_I2S_BCK          11
#define BOARD_I2S_MCLK         10
#define BOARD_I2S_DOUT         45
#define BOARD_I2S_DIN          17

// ── GPS MIA-M10Q (built in) ──
#define BOARD_GPS_TX_PIN       12   // module TX  -> MCU RX
#define BOARD_GPS_RX_PIN       4    // module RX  <- MCU TX
#define BOARD_GPS_PPS_PIN      13
#define BOARD_GPS_BAUD         9600

// ── LoRa SX1262 (deferred feature; pins confirmed for later) ──
#define RADIO_CS_PIN           36
#define RADIO_RST_PIN          47
#define RADIO_BUSY_PIN         48
#define RADIO_DIO1_PIN         14

// ── NFC ST25R3916 (deferred) ──
#define BOARD_NFC_CS           39
#define BOARD_NFC_IRQ          5

// ── IMU BHI260AP (deferred) / RTC PCF85063A (deferred) ──
#define BOARD_IMU_INT          8
#define BOARD_RTC_INT          1

// ── Free UART1 on the 12-pin socket (these are the T-Deck's GPS pins, free here) ──
#define BOARD_UART1_TX         43
#define BOARD_UART1_RX         44

// ── XL9555 expander pin assignments (linear 0-15: port = pin/8, bit = pin%8) ──
//    These gate the peripheral power rails. Enabled (driven HIGH) in order by
//    boardPowerOn() at the very start of boot. Confirmed from LilyGoLib
//    LilyGo_LoRa_Pager.cpp. Display reset is NOT broken out on this board.
#define EXPANDS_DRV_EN      0    // DRV2605 haptic enable
#define EXPANDS_AMP_EN      1    // audio amplifier enable
#define EXPANDS_KB_RST      2    // keyboard reset (release = HIGH)
#define EXPANDS_LORA_EN     3    // LoRa power
#define EXPANDS_GPS_EN      4    // GNSS power
#define EXPANDS_NFC_EN      5    // NFC power
#define EXPANDS_GPS_RST     7    // GNSS reset (release = HIGH)
#define EXPANDS_NRF_CE      9    // nRF24 shield CE (12-pin socket)
#define EXPANDS_KB_EN       10   // keyboard power
#define EXPANDS_GPIO_EN     11   // external 12-pin socket power
#define EXPANDS_SD_DETECT   12   // SD card insert-detect (INPUT)
#define EXPANDS_SD_EN       14   // SD card power

// ── I2C device addresses ──
#define TPAGER_I2C_ADDR_ES8311    0x18
#define TPAGER_I2C_ADDR_XL9555    0x20   // power-rail expander — gates everything
#define TPAGER_I2C_ADDR_BHI260AP  0x28
#define TPAGER_I2C_ADDR_TCA8418   0x34   // keyboard
#define TPAGER_I2C_ADDR_PCF85063A 0x51   // RTC
#define TPAGER_I2C_ADDR_DRV2605   0x5A   // haptic
#define TPAGER_I2C_ADDR_BQ27220   0x55   // fuel gauge
#define TPAGER_I2C_ADDR_BQ25896   0x6B   // charger

// ── Boot / strapping ──
#define BOARD_BOOT_PIN         0

// ── Compatibility shim: the T-Deck exposes a single BOARD_POWERON GPIO. The
//    T-Pager has none (power is via the XL9555 expander). Defined as -1 so any
//    residual unconditional reference compiles; Phase 2's board powerOn()
//    replaces the digitalWrite(BOARD_POWERON, HIGH) path entirely. ──
#define BOARD_POWERON          -1
