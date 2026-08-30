/**
 * @file   board_power.cpp
 * @brief  Board power-on HAL implementation. See board_power.h.
 */
#include "board_power.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <esp_wifi.h>

#if defined(BOARD_TPAGER)
// SensorLib is already a dependency (GT911 touch on the T-Deck). Its XL9555
// expander driver gates every peripheral rail on the T-Pager.
#include <IoExpanderXL9555.hpp>

// Kept at file scope so the expander object (and its cached output state) lives
// past boot — future T-Pager code (SD insert-detect on EXPANDS_SD_DETECT, rail
// toggles for sleep) can reuse it. The rails stay enabled in HW regardless, since
// enabling them writes the XL9555 output register.
static IoExpanderXL9555 s_xl9555;

void boardPowerOn() {
    // Shared I2C bus first — the expander, keyboard, RTC, gauge, codec all live here.
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    // Backlight: the AW9364 enable is a direct GPIO (not behind the expander), so
    // drive it unconditionally up front — a transient expander failure must not be
    // able to leave the screen dark. Full brightness on power-up (no PWM; see
    // lgfx_panel.h note).
    pinMode(BOARD_BL_PIN, OUTPUT);
    digitalWrite(BOARD_BL_PIN, HIGH);

    // The XL9555 gates every peripheral rail — retry a few times on a transient
    // I2C hiccup rather than bailing on the first miss (which would leave the whole
    // board dead with no recovery).
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; attempt++) {
        ok = s_xl9555.begin(Wire, TPAGER_I2C_ADDR_XL9555);
        if (!ok) delay(20);
    }
    if (!ok) {
        return;   // expander truly absent → the peripheral rails can't come up
    }

    // Enable every peripheral rail, in LilyGoLib's order. Driving these HIGH
    // powers the rail / releases the reset. Display reset is not broken out on
    // this board, so it is absent here.
    const uint8_t rails[] = {
        EXPANDS_KB_RST,   // release keyboard reset
        EXPANDS_LORA_EN,  // LoRa power
        EXPANDS_GPS_EN,   // GNSS power
        EXPANDS_DRV_EN,   // haptic
        EXPANDS_NFC_EN,   // NFC power
        EXPANDS_GPS_RST,  // release GNSS reset
        EXPANDS_KB_EN,    // keyboard power
        EXPANDS_GPIO_EN,  // external 12-pin socket power
        EXPANDS_SD_EN,    // SD card power (before sdCardManager.begin())
    };
    for (uint8_t pin : rails) {
        s_xl9555.pinMode(pin, OUTPUT);
        s_xl9555.digitalWrite(pin, HIGH);
    }
    // Amp EN starts LOW — enabled only during actual audio playback by
    // boardCodecBegin/End. Amp-always-on made the ES8311 codec's noise floor
    // (and any I2S-line garbage after driver uninstall) audible as continuous
    // hiss, even in download mode / device off. Set pin as OUTPUT with LOW.
    s_xl9555.pinMode(EXPANDS_AMP_EN, OUTPUT);
    s_xl9555.digitalWrite(EXPANDS_AMP_EN, LOW);

    // SD insert-detect is an expander input, not a GPIO.
    s_xl9555.pinMode(EXPANDS_SD_DETECT, INPUT);

    delay(20);   // let the rails settle before the ST7796 / SD / codec come up
}

void boardPowerOff() {
    // Amp off first so the speaker goes silent before anything else — otherwise
    // it would keep hissing right up to the moment BATFET disconnects.
    boardAmpEnable(false);

    // Backlight off so the screen goes dark immediately.
    pinMode(BOARD_BL_PIN, OUTPUT);
    digitalWrite(BOARD_BL_PIN, LOW);

    // BQ25896 REG09 ship mode: BATFET_DLY=0 (immediate) + BATFET_DIS=1 disconnects
    // the battery → true power-off (0 draw) when running on battery.
    Wire.beginTransmission(TPAGER_I2C_ADDR_BQ25896);
    Wire.write(0x09);
    if (Wire.endTransmission(false) == 0 &&
        Wire.requestFrom((int)TPAGER_I2C_ADDR_BQ25896, 1) == 1) {
        uint8_t r9 = Wire.read();
        r9 &= ~(1 << 3);   // BATFET_DLY = 0 → turn off now, not after ~10s
        r9 |=  (1 << 5);   // BATFET_DIS = 1 → ship mode
        Wire.beginTransmission(TPAGER_I2C_ADDR_BQ25896);
        Wire.write(0x09);
        Wire.write(r9);
        Wire.endTransmission();
    }

    // On battery the line above already cut power. On USB, VBUS keeps the system
    // alive, so deep-sleep to look "off"; the encoder push wakes it.
    delay(200);
    esp_sleep_enable_ext1_wakeup(1ULL << BOARD_ENCODER_PUSH, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void boardBleRadioPrepare() {
    // Fully release WiFi so the BT controller can grab its contiguous internal RAM.
    esp_wifi_set_promiscuous(false);
    wifi_mode_t wm;
    if (esp_wifi_get_mode(&wm) == ESP_OK && wm != WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);              // esp_wifi_stop() + esp_wifi_deinit()
        vTaskDelay(pdMS_TO_TICKS(150));   // let the freed RAM coalesce before BLE init
    }
}

void boardAmpEnable(bool on) {
    // Amp is behind the XL9555; only toggle if begin() succeeded (pinMode was set).
    s_xl9555.digitalWrite(EXPANDS_AMP_EN, on ? HIGH : LOW);
}

#else  // ── T-Deck / T-Deck-Plus ──────────────────────────────────────────────

void boardPowerOn() {
    // Behaviourally identical to the old inline block at the top of
    // DisplayManager::init(): assert the single peripheral-power GPIO, then bring
    // up the shared I2C bus (keyboard + GT911 touch reuse this Wire instance).
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
}

void boardPowerOff() {
    // No software battery cut on the T-Deck — deep-sleep, wake on the trackball click.
    esp_sleep_enable_ext1_wakeup(1ULL << BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

void boardBleRadioPrepare() {}   // T-Deck keeps warm-STA — no BLE-vs-WiFi RAM issue.

void boardAmpEnable(bool on) { (void)on; }

#endif
