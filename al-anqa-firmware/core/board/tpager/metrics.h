/**
 * @file   metrics.h  (T-Lora Pager variant)
 * @brief  Screen geometry + text-layout constants for the 480x222 ST7796.
 *
 * ⚠️ Phase 0 SCAFFOLD. The width/height are correct (480x222 landscape); the
 * status-bar height + text grid are carried over from the T-Deck for now and get
 * retuned in Phase 3 (responsive helpers + widescreen redesign). The −18px height
 * vs the T-Deck is the layout hazard the reflow pass addresses.
 */
#pragma once

#include <stdint.h>

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 222
#define LINE_HEIGHT   14

static const uint16_t promptHeight = 30;   // WIP — Phase 3b status-bar redesign
static const uint16_t promptY      = 0;
static const uint16_t outputY      = promptY + promptHeight + 8;   // 38

// ── Undercover / home cover layout profile (landscape PDA) ───────────────────
// Apps MUST gate on these (or BOARD_HAS_TOUCH / BOARD_HAS_ENCODER), never on the
// raw board identity. Adding a new board = fill in a metrics.h profile here.
#define UI_SB_H                 22
#define UI_APPBAR_H             26   // compact for 222 px height
#define UI_BTN_H                28
#define UI_BTN_Y                (SCREEN_HEIGHT - 36)
#define UI_CALC_DISP_H          36
#define UI_CLOCK_MID_Y          (UI_SB_H + UI_APPBAR_H + 58)
#define UI_CLOCK_ADJ_Y          (UI_SB_H + UI_APPBAR_H + 88)
#define UI_CAL_ROW_H            22
#define COVER_HOME_SIDE_BY_SIDE 1    // left hero + right 4x2 grid
#define COVER_HOME_HERO_W       188
#define COVER_HOME_ICON_W       44
#define COVER_HOME_G_MARGIN     8
#define COVER_NOTES_TWO_PANE    1    // left list | right editor/preview
#define COVER_NOTES_LIST_W      200
#define COVER_NOTES_CARD_H      42
#define COVER_NOTES_SEARCH_Y    46
#define COVER_NOTES_SEARCH_H    22
#define COVER_NOTES_APPBAR_Y    (UI_SB_H + 4)
#define COVER_NOTES_DOC_TOP     (UI_SB_H + 28)
#define COVER_NOTES_FAB_R       18
