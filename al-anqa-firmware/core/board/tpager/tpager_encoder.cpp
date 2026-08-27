/**
 * @file   tpager_encoder.cpp
 * @brief  Rotary encoder decode for the T-Lora Pager. See tpager_encoder.h.
 *
 * Interrupt-driven full-step quadrature decoder (Ben Buxton's state table). The
 * ISR captures every A/B transition and accumulates completed detents into
 * s_delta, so consumers that poll getTrackballEvent() slowly (e.g. netspy's ~30ms
 * loop) never miss steps — a polled decoder did, which read as "encoder dead".
 */
#include "tpager_encoder.h"

#if defined(BOARD_TPAGER)

#include "pins.h"
#include "tpager_keyboard.h"   // tpagerSymHeld()
#include <Arduino.h>

#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20

// Kept in DRAM (not flash rodata) so the IRAM ISR can read it without a cache miss.
static const DRAM_ATTR uint8_t ttable[7][4] = {
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

static volatile uint8_t s_state = R_START;
static volatile int8_t  s_delta = 0;      // accumulated detents (+CW / -CCW)
static bool     s_pushLast = true;        // active-low, pulled up → idle HIGH
static uint32_t s_pushMs   = 0;

static void IRAM_ATTR encISR() {
    uint8_t pinstate = (uint8_t)((digitalRead(BOARD_ENCODER_B) << 1) | digitalRead(BOARD_ENCODER_A));
    s_state = ttable[s_state & 0x0F][pinstate];
    uint8_t dir = s_state & 0x30;
    if      (dir == DIR_CW  && s_delta <  100) s_delta++;
    else if (dir == DIR_CCW && s_delta > -100) s_delta--;
}

void tpagerEncoderBegin() {
    pinMode(BOARD_ENCODER_A,    INPUT_PULLUP);
    pinMode(BOARD_ENCODER_B,    INPUT_PULLUP);
    pinMode(BOARD_ENCODER_PUSH, INPUT_PULLUP);
    s_pushLast = digitalRead(BOARD_ENCODER_PUSH);
    s_state = R_START;
    s_delta = 0;
    attachInterrupt(digitalPinToInterrupt(BOARD_ENCODER_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BOARD_ENCODER_B), encISR, CHANGE);
}

TrackballEvent tpagerEncoderRead() {
    // Push button (active-low), debounced — fire on the press edge.
    bool p = digitalRead(BOARD_ENCODER_PUSH);
    if (p != s_pushLast && (millis() - s_pushMs) > 30) {
        s_pushMs   = millis();
        s_pushLast = p;
        if (p == LOW) return TBALL_CLICK;
    }

    // One accumulated detent per call (drain toward zero under a brief IRQ lock).
    if (s_delta != 0) {
        noInterrupts();
        bool cw = (s_delta > 0);
        if (cw) s_delta--; else s_delta++;
        interrupts();
        if (cw) return tpagerSymHeld() ? TBALL_RIGHT : TBALL_DOWN;
        else    return tpagerSymHeld() ? TBALL_LEFT  : TBALL_UP;
    }
    return TBALL_NONE;
}

#endif // BOARD_TPAGER
