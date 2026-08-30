/**
 * @file   layout.h
 * @brief  Board-agnostic screen-layout helpers — anchors that read metrics
 *         so no app hard-codes a 320x240-era pixel literal.
 *
 * Every helper is `constexpr` and evaluates to the same integer literal it
 * used to be on the T-Deck (SCREEN_HEIGHT=240, LINE_HEIGHT=14, SCREEN_WIDTH=320)
 * — the byte-identical guarantee that makes the T-Deck the reference target
 * for the T-Pager port. On the T-Pager (SCREEN_HEIGHT=222, SCREEN_WIDTH=480)
 * the same expression produces the correct new position automatically.
 *
 * Design principle: apps describe position relatively (`layoutFooterY(30)`
 * = "30 px above the bottom") instead of absolutely (`210`). Bottom-anchored
 * helpers also preserve relative row spacing across boards for free:
 *   (SH - a) - (SH - b) = b - a       // SH cancels
 *
 * Deliberately narrow scope in this pass:
 *   - layoutFooterY / layoutRightX / layoutCharCols only.
 *   - rowY / colX / rowsPerPage are NOT added yet — they need per-screen
 *     tuning that Phase 3b delivers with the widescreen table redesigns.
 *     Adding them prematurely would just get rewritten.
 */
#pragma once

#include "board.h"   // brings in the active variant's metrics.h (SCREEN_*, LINE_HEIGHT)

// ── Bottom-anchored Y ────────────────────────────────────────────────────────
// `k` = pixels between the anchor and the bottom edge of the screen.
//   T-Deck  (240): layoutFooterY(30) -> 210, layoutFooterY(26) -> 214,
//                  layoutFooterY(10) -> 230
//   T-Pager (222): layoutFooterY(30) -> 192, layoutFooterY(26) -> 196,
//                  layoutFooterY(10) -> 212
constexpr int layoutFooterY(int k) { return SCREEN_HEIGHT - k; }

// ── Right-anchored X ─────────────────────────────────────────────────────────
// `w` = pixels between the anchor and the right edge of the screen.
constexpr int layoutRightX(int w)  { return SCREEN_WIDTH  - w; }

// ── Text-grid columns for the 6px Font0 grid (editor / SSH terminal) ─────────
// 8 px left/right margin, 6 px per glyph.
//   T-Deck  (320): 52   (preserves existing ED_COLS / COLS = 52 exactly)
//   T-Pager (480): 78   (SSH-sized terminal, editor gains ~26 chars/line)
constexpr int layoutCharCols()     { return (SCREEN_WIDTH - 8) / 6; }

// ── How many LINE_HEIGHT-tall data rows fit given header + footer sizes ──────
// Table screens follow the convention:
//   header rows (title / column headers / separator) | data rows | footer rows
// This returns the data-row count that fits — self-adapts to any board's height.
//   headerRows = rows of chrome above the data block
//   footerRows = rows of chrome below the data block (separators + hints)
//   T-Deck  (240, outputY=38): (240-38)/14 = 14 rows total → 14 - H - F data rows
//   T-Pager (222, outputY=38): (222-38)/14 = 13 rows total → 13 - H - F data rows
// Callers say what THEIR layout looks like; the helper answers how many rows fit.
// Single-return body — C++11 constexpr can't have multi-statement bodies.
constexpr int layoutMaxDataRows(int headerRows, int footerRows) {
    return (((SCREEN_HEIGHT - outputY) / LINE_HEIGHT - headerRows - footerRows) > 0)
         ?  ((SCREEN_HEIGHT - outputY) / LINE_HEIGHT - headerRows - footerRows)
         : 1;
}
