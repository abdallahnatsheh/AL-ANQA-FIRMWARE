/**
 * @file   board_power.h
 * @brief  Board power-on HAL — brings up the peripheral power rails + I2C bus.
 *
 * Called once at the very start of DisplayManager::init() (the first thing in
 * setup()), before the display / SD / inputs come up.
 *
 *  - T-Deck / T-Deck-Plus: drive the single BOARD_POWERON GPIO HIGH, then Wire.begin.
 *  - T-Pager: init the shared I2C bus + the XL9555 expander and enable every
 *    peripheral rail in order (nothing on the board powers up until this runs).
 *
 * The T-Deck path is behaviourally identical to the old inline
 * pinMode/digitalWrite/Wire.begin block it replaces.
 */
#pragma once

// Bring up board power rails and the shared I2C bus. Safe to call once at boot.
void boardPowerOn();

// Release WiFi before bringing up the BLE controller. T-Pager only: after a heavy
// WiFi command (karma/pwn) the idle WiFi driver still holds the contiguous internal
// DRAM the BT controller needs, so NimBLEDevice::init() aborts → crash. WIFI_OFF
// (esp_wifi_stop+deinit) frees it; a later WiFi command re-inits cleanly. No-op on
// T-Deck (keeps its warm-STA behaviour) and when WiFi is already down.
void boardBleRadioPrepare();

// Power the device off. On the T-Pager this puts the BQ25896 PMU into ship mode
// (BATFET disconnect) → true power-off on battery; on USB (where VBUS keeps the
// system alive) it falls back to deep sleep. On the T-Deck there is no software
// battery cut, so it deep-sleeps. Does not return.
void boardPowerOff();

// Speaker amp enable (XL9555 EXPANDS_AMP_EN). T-Pager only; no-op on T-Deck.
// Use to keep the amp off while I2S/codec are brought up so the speaker never
// hears floating DAC / garbage DMA.
void boardAmpEnable(bool on);
