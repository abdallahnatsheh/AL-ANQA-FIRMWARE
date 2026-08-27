/**
 * @file   es8311.cpp
 * @brief  Compact ES8311 codec driver — see es8311.h.
 *
 * Register sequence + coefficient table ported from Espressif's es8311.c
 * (public domain / Apache-2.0), adapted to Arduino Wire. Fixed configuration:
 * codec as I2S SLAVE, MCLK provided (256×fs), 16-bit, DAC (playback) mode.
 */
#include "es8311.h"

static TwoWire* s_wire = nullptr;
static uint8_t  s_addr = 0x18;

// ── ES8311 register map (subset used here) ──────────────────────────────────
#define R00 0x00  // reset / csm
#define R01 0x01  // clk manager: mclk src
#define R02 0x02  // clk: divider / multiplier
#define R03 0x03  // clk: adc fsmode/osr
#define R04 0x04  // clk: dac osr
#define R05 0x05  // clk: adc/dac divider
#define R06 0x06  // clk: bclk inv/div
#define R07 0x07  // clk: lrck div high
#define R08 0x08  // clk: lrck div low
#define R09 0x09  // dac serial port
#define R0A 0x0A  // adc serial port
#define R0B 0x0B
#define R0C 0x0C
#define R0D 0x0D  // power up/down
#define R0E 0x0E
#define R10 0x10
#define R11 0x11
#define R12 0x12  // enable dac
#define R13 0x13
#define R14 0x14  // dmic / pga
#define R15 0x15
#define R16 0x16
#define R17 0x17  // adc volume
#define R1B 0x1B
#define R1C 0x1C
#define R31 0x31  // dac mute
#define R32 0x32  // dac volume
#define R37 0x37  // dac ramp
#define R44 0x44  // gpio (dac2adc ref)
#define R45 0x45  // gp control
#define RFD 0xFD  // chip id1

struct Coeff {
    uint32_t mclk, rate;
    uint8_t pre_div, pre_multi, adc_div, dac_div, fs_mode, lrck_h, lrck_l, bclk_div, adc_osr, dac_osr;
};

// Espressif hifi MCLK coefficient table.
static const Coeff kCoeff[] = {
    {12288000, 8000,  0x06,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {4096000,  8000,  0x02,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {2048000,  8000,  0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {11289600, 11025, 0x04,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {2822400,  11025, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {12288000, 12000, 0x04,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {3072000,  12000, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {12288000, 16000, 0x03,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {4096000,  16000, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {2048000,  16000, 0x01,0x02,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x20},
    {11289600, 22050, 0x02,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {5644800,  22050, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {12288000, 24000, 0x02,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {6144000,  24000, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {12288000, 32000, 0x03,0x02,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {8192000,  32000, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {11289600, 44100, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {5644800,  44100, 0x01,0x02,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {12288000, 48000, 0x01,0x01,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
    {6144000,  48000, 0x01,0x02,0x01,0x01,0x00,0x00,0xff,0x04,0x10,0x10},
};

static void wr(uint8_t reg, uint8_t val) {
    s_wire->beginTransmission(s_addr);
    s_wire->write(reg);
    s_wire->write(val);
    s_wire->endTransmission();
}
static uint8_t rd(uint8_t reg) {
    s_wire->beginTransmission(s_addr);
    s_wire->write(reg);
    s_wire->endTransmission(false);
    s_wire->requestFrom((int)s_addr, 1);
    return s_wire->available() ? s_wire->read() : 0;
}

static const Coeff* findCoeff(uint32_t mclk, uint32_t rate) {
    for (auto& c : kCoeff) if (c.mclk == mclk && c.rate == rate) return &c;
    return nullptr;
}

bool es8311Begin(TwoWire& wire, uint8_t addr, uint32_t sampleRate, bool enableAdc) {
    s_wire = &wire;
    s_addr = addr;

    const uint32_t mclk = sampleRate * 256;      // ESP32 legacy I2S MCLK = 256*fs
    const Coeff* c = findCoeff(mclk, sampleRate);
    if (!c) return false;

    // Probe the chip (any nonzero id register response ≈ present).
    (void)rd(RFD);

    // ── open() — slave, use_mclk, DAC ref on ──
    wr(R44, 0x08); wr(R44, 0x08);                 // I2C noise immunity (written twice)
    wr(R01, 0x30); wr(R02, 0x00); wr(R03, 0x10);
    wr(R16, 0x24); wr(R04, 0x10); wr(R05, 0x00);
    wr(R0B, 0x00); wr(R0C, 0x00); wr(R10, 0x1F);
    wr(R11, 0x7F); wr(R00, 0x80);
    wr(R00, rd(R00) & 0xBF);                       // slave mode
    wr(R01, 0x3F);                                 // mclk src = external, not inverted
    wr(R06, rd(R06) & ~0x20);                      // sclk not inverted
    wr(R13, 0x10); wr(R1B, 0x0A); wr(R1C, 0x6A);
    wr(R44, 0x58);                                 // internal ref (ADCL+DACR)

    // ── config_sample(rate) ──
    uint8_t regv = rd(R02) & 0x07;
    regv |= (uint8_t)((c->pre_div - 1) << 5);
    uint8_t datmp = (c->pre_multi == 8) ? 3 : (c->pre_multi == 4) ? 2 : (c->pre_multi == 2) ? 1 : 0;
    regv |= (uint8_t)(datmp << 3);
    wr(R02, regv);
    wr(R05, (uint8_t)(((c->adc_div - 1) << 4) | ((c->dac_div - 1) << 0)));
    wr(R03, (uint8_t)((rd(R03) & 0x80) | (c->fs_mode << 6) | c->adc_osr));
    wr(R04, (uint8_t)((rd(R04) & 0x80) | c->dac_osr));
    wr(R07, (uint8_t)((rd(R07) & 0xC0) | c->lrck_h));
    wr(R08, c->lrck_l);
    regv = rd(R06) & 0xE0;
    regv |= (c->bclk_div < 19) ? (uint8_t)(c->bclk_div - 1) : c->bclk_div;
    wr(R06, regv);

    // ── I2S format = normal, 16-bit ──
    wr(R09, (uint8_t)((rd(R09) & 0xFC) | 0x0C));   // normal fmt + 16-bit
    wr(R0A, (uint8_t)((rd(R0A) & 0xFC) | 0x0C));

    // ── start() — DAC (+ optional ADC) path ──
    wr(R00, 0x80);
    wr(R01, 0x3F);
    wr(R09, (uint8_t)(rd(R09) & 0xBF));            // unmute DAC serial (clear BIT6)
    if (enableAdc) wr(R0A, (uint8_t)(rd(R0A) & 0xBF));   // unmute ADC serial (mic)
    else           wr(R0A, (uint8_t)(rd(R0A) | 0x40));   // keep ADC serial muted
    wr(R17, 0xBF); wr(R0E, 0x02); wr(R12, 0x00); wr(R14, 0x1A);
    wr(R14, (uint8_t)(rd(R14) & ~0x40));           // digital mic off (analog ADC path)
    wr(R0D, 0x01); wr(R15, 0x40); wr(R37, 0x08); wr(R45, 0x00);

    es8311SetVolume(75);                           // ~0 dB DAC
    // Stay MUTED. Callers must prime I2S with silence (i2s_zero_dma_buffer /
    // silent writes) then es8311SetMute(false). Unmuting here with garbage DMA
    // dumps a blast of static into the amp (classic gm / NES start crackle).
    es8311SetMute(true);
    if (enableAdc) es8311SetMicGain(6);            // 36 dB — solid default for the analog mic
    return true;
}

void es8311SetVolume(uint8_t vol) {
    if (!s_wire) return;
    if (vol > 100) vol = 100;
    wr(R32, (uint8_t)((uint16_t)vol * 255 / 100));
}

void es8311SetMute(bool mute) {
    if (!s_wire) return;
    uint8_t regv = rd(R31) & 0x9F;
    wr(R31, mute ? (uint8_t)(regv | 0x60) : regv);
}

void es8311SetMicGain(uint8_t gain_0_to_7) {
    if (!s_wire) return;
    // Reg 0x16 = ADC PGA gain, valid 0..7 = 0/6/12/18/24/30/36/42 dB. Values >7 are
    // out of range and leave the PGA dead (silent mic) — NOT a louder setting.
    if (gain_0_to_7 > 7) gain_0_to_7 = 7;
    wr(R16, gain_0_to_7);
}
