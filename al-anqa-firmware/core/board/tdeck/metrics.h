/**
 * @file   metrics.h  (T-Deck / T-Deck-Plus variant)
 * @brief  Screen geometry + text-layout constants for the 320x240 ST7789.
 *
 * Extracted verbatim from core/display/display_manager.h so the board layer owns
 * screen geometry. Values are byte-for-byte identical to the pre-HAL T-Deck.
 * Consumers reach these through board.h (which display_manager.h includes).
 */
#pragma once

#include <stdint.h>

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define LINE_HEIGHT   14

static const uint16_t promptHeight = 30;
static const uint16_t promptY      = 0;
static const uint16_t outputY      = promptY + promptHeight + 8;   // 38

// ── Undercover / home cover layout profile ───────────────────────────────────
// Apps MUST gate on these (or BOARD_HAS_TOUCH / BOARD_HAS_ENCODER), never on the
// raw board identity. Adding a new board = fill in a metrics.h profile here.
#define UI_SB_H                 22
#define UI_APPBAR_H             32
#define UI_BTN_H                34
#define UI_BTN_Y                (SCREEN_HEIGHT - 44)
#define UI_CALC_DISP_H          46
#define UI_CLOCK_MID_Y          (UI_SB_H + UI_APPBAR_H + 78)
#define UI_CLOCK_ADJ_Y          (UI_SB_H + UI_APPBAR_H + 102)
#define UI_CAL_ROW_H            26
#define COVER_HOME_SIDE_BY_SIDE 0    // 0 = stacked hero above 4x2 grid
#define COVER_HOME_HERO_W       0    // unused when stacked
#define COVER_HOME_ICON_W       50
#define COVER_HOME_G_MARGIN     14
#define COVER_NOTES_TWO_PANE    0    // 0 = full-width list / full-width editor
#define COVER_NOTES_LIST_W      SCREEN_WIDTH
#define COVER_NOTES_CARD_H      58
#define COVER_NOTES_SEARCH_Y    60
#define COVER_NOTES_SEARCH_H    26
#define COVER_NOTES_APPBAR_Y    (UI_SB_H + 8)
#define COVER_NOTES_DOC_TOP     (UI_SB_H + 34)
#define COVER_NOTES_FAB_R       22
