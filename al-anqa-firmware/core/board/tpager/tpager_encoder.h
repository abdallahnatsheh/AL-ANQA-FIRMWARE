/**
 * @file   tpager_encoder.h
 * @brief  T-Lora Pager rotary encoder → TrackballEvent adapter.
 *
 * Maps the encoder to the same TrackballEvent contract the T-Deck trackball
 * produced, so every UI that calls getTrackballEvent() works unchanged:
 *   - rotate           → UP / DOWN
 *   - push (GPIO7)      → CLICK
 *   - hold Sym + rotate → LEFT / RIGHT   (the locked 4-way nav decision;
 *                         Sym-held state comes from the keyboard driver)
 *
 * Only compiled/#included on the T-Pager (guarded).
 */
#pragma once

#include "input_handling.h"   // TrackballEvent

void           tpagerEncoderBegin();
TrackballEvent tpagerEncoderRead();
